#include "SRCSerial_Internal.h"

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace srcserial
{
    namespace
    {
        HMODULE g_module = NULL;
        HANDLE g_port = INVALID_HANDLE_VALUE;
        CRITICAL_SECTION g_lock;
        bool g_lockInitialized = false;
        PortConfig g_config;
        Diagnostics g_diagnostics;
        long g_dtrState = 1;
        long g_rtsState = 1;
        long g_breakState = 0;
        volatile LONG g_cancelRequested = 0;
        FILE* g_log = NULL;
        const long MaxLogBytes = 5L * 1024L * 1024L;

        class ScopedLock
        {
        public:
            ScopedLock() { if (g_lockInitialized) EnterCriticalSection(&g_lock); }
            ~ScopedLock() { if (g_lockInitialized) LeaveCriticalSection(&g_lock); }
        private:
            ScopedLock(const ScopedLock&);
            ScopedLock& operator=(const ScopedLock&);
        };

        DWORD AddCounter(DWORD current, DWORD value)
        {
            const DWORD maximum = 0x7fffffffUL;
            return current >= maximum - (std::min)(value, maximum) ? maximum : current + value;
        }

        void LogLine(long level, const char* format, ...)
        {
            if (!g_log || g_config.logging < level || !format) return;
            const long position = std::ftell(g_log);
            if (position >= MaxLogBytes)
            {
                if (position == MaxLogBytes)
                {
                    std::fprintf(g_log, "log limit reached; further entries suppressed\n");
                    std::fflush(g_log);
                }
                return;
            }
            SYSTEMTIME now;
            GetLocalTime(&now);
            std::fprintf(g_log, "%04u-%02u-%02u %02u:%02u:%02u.%03u ",
                now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
                now.wSecond, now.wMilliseconds);
            va_list arguments;
            va_start(arguments, format);
            std::vfprintf(g_log, format, arguments);
            va_end(arguments);
            std::fputc('\n', g_log);
            std::fflush(g_log);
        }

        void LogTraffic(const char* direction, const unsigned char* data, DWORD count)
        {
            if (g_config.logging < 2) return;
            const std::string hex = FormatHex(data, count);
            std::string ascii;
            ascii.reserve(count);
            for (DWORD i = 0; i < count; ++i)
                ascii.push_back(data[i] >= 32 && data[i] <= 126 ?
                    static_cast<char>(data[i]) : '.');
            LogLine(2, "%s %lu bytes  %-48s  \"%s\"", direction,
                static_cast<unsigned long>(count), hex.c_str(), ascii.c_str());
        }

        void CloseLog()
        {
            if (g_log)
            {
                std::fclose(g_log);
                g_log = NULL;
            }
        }

        void OpenLog()
        {
            CloseLog();
            char modulePath[MAX_PATH] = { 0 };
            if (!g_module || !GetModuleFileNameA(g_module, modulePath, MAX_PATH)) return;
            char* slash = std::strrchr(modulePath, '\\');
            if (!slash) return;
            *slash = '\0';
            const std::string directory = std::string(modulePath) + "\\logs";
            CreateDirectoryA(directory.c_str(), NULL);
            SYSTEMTIME now;
            GetLocalTime(&now);
            char fileName[MAX_PATH] = { 0 };
            ::sprintf_s(fileName, "%s\\SRCSerial_%04u%02u%02u_%02u%02u%02u_%lu.log",
                directory.c_str(), now.wYear, now.wMonth, now.wDay, now.wHour,
                now.wMinute, now.wSecond, static_cast<unsigned long>(GetCurrentProcessId()));
            fopen_s(&g_log, fileName, "a");
        }

        void ClosePortNoLock()
        {
            InterlockedExchange(&g_cancelRequested, 1);
            if (g_port != INVALID_HANDLE_VALUE)
            {
                CancelIoEx(g_port, NULL);
                PurgeComm(g_port, PURGE_RXABORT | PURGE_RXCLEAR | PURGE_TXABORT | PURGE_TXCLEAR);
                CloseHandle(g_port);
                g_port = INVALID_HANDLE_VALUE;
            }
        }

        Status PortError(const char* operation, DWORD code)
        {
            g_diagnostics.lastWin32Error = code;
            LogLine(1, "ERROR %s failed with Win32 error %lu", operation,
                static_cast<unsigned long>(code));
            return Status::Win32Error(operation, code);
        }

        Status FailOpen(const Status& status)
        {
            ClosePortNoLock();
            CloseLog();
            return status;
        }

        DWORD ResolveTimeout(DWORD requested, DWORD configured)
        {
            return requested == MAXDWORD ? configured : requested;
        }

        bool HasElapsed(DWORD start, DWORD timeout)
        {
            return timeout != INFINITE && static_cast<DWORD>(GetTickCount() - start) >= timeout;
        }

        bool CancellationRequested()
        {
            return InterlockedCompareExchange(&g_cancelRequested, 0, 0) != 0;
        }

        Status DelayNoLock(DWORD durationMs)
        {
            const DWORD start = GetTickCount();
            while (!HasElapsed(start, durationMs))
            {
                if (CancellationRequested())
                    return PortError("serial operation cancelled", ERROR_OPERATION_ABORTED);
                const DWORD elapsed = static_cast<DWORD>(GetTickCount() - start);
                Sleep((std::min)(static_cast<DWORD>(10), durationMs - elapsed));
            }
            return Status::Ok();
        }

        void RecordCommErrors(DWORD errors)
        {
            if (errors & CE_FRAME) g_diagnostics.frameErrors = AddCounter(g_diagnostics.frameErrors, 1);
            if (errors & CE_RXPARITY) g_diagnostics.parityErrors = AddCounter(g_diagnostics.parityErrors, 1);
            if (errors & CE_OVERRUN) g_diagnostics.overrunErrors = AddCounter(g_diagnostics.overrunErrors, 1);
            if (errors & CE_RXOVER) g_diagnostics.bufferOverrunErrors = AddCounter(g_diagnostics.bufferOverrunErrors, 1);
            if (errors & CE_BREAK) g_diagnostics.breakCount = AddCounter(g_diagnostics.breakCount, 1);
            if (errors) LogLine(1, "communication error flags 0x%08lX", static_cast<unsigned long>(errors));
        }

        Status QueryQueuesNoLock(COMSTAT* stat)
        {
            if (!stat) return Status::Validation(ErrorInvalidParameter, "Queue status output is required");
            DWORD errors = 0;
            std::memset(stat, 0, sizeof(*stat));
            if (!ClearCommError(g_port, &errors, stat))
                return PortError("ClearCommError", GetLastError());
            RecordCommErrors(errors);
            LogLine(3, "queues RX=%lu TX=%lu", static_cast<unsigned long>(stat->cbInQue),
                static_cast<unsigned long>(stat->cbOutQue));
            return Status::Ok();
        }

        Status CompleteOverlappedNoLock(const char* operation, OVERLAPPED* overlapped,
            DWORD timeoutMs, DWORD* transferred, bool* timedOut)
        {
            *transferred = 0;
            *timedOut = false;
            const DWORD waitResult = WaitForSingleObject(overlapped->hEvent, timeoutMs);
            if (waitResult == WAIT_TIMEOUT)
            {
                *timedOut = true;
                CancelIoEx(g_port, overlapped);
                WaitForSingleObject(overlapped->hEvent, INFINITE);
                DWORD ignored = 0;
                GetOverlappedResult(g_port, overlapped, &ignored, FALSE);
                *transferred = static_cast<DWORD>(overlapped->InternalHigh);
                return Status::Ok();
            }
            if (waitResult != WAIT_OBJECT_0)
                return PortError("WaitForSingleObject", GetLastError());
            if (!GetOverlappedResult(g_port, overlapped, transferred, FALSE))
            {
                *transferred = static_cast<DWORD>(overlapped->InternalHigh);
                return PortError(operation, GetLastError());
            }
            return Status::Ok();
        }

        Status ReadChunkNoLock(DWORD maximum, std::vector<unsigned char>* output)
        {
            COMSTAT stat;
            Status status = QueryQueuesNoLock(&stat);
            if (!status.success) return status;
            const DWORD toRead = (std::min)(maximum, stat.cbInQue);
            if (toRead == 0) return Status::Ok();

            const size_t offset = output->size();
            output->resize(offset + toRead);
            OVERLAPPED overlapped = { 0 };
            overlapped.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
            if (!overlapped.hEvent)
            {
                output->resize(offset);
                return PortError("CreateEvent", GetLastError());
            }
            DWORD count = 0;
            bool timedOut = false;
            BOOL started = ReadFile(g_port, &(*output)[offset], toRead, NULL, &overlapped);
            status = Status::Ok();
            if (!started)
            {
                const DWORD error = GetLastError();
                if (error == ERROR_IO_PENDING)
                    status = CompleteOverlappedNoLock("ReadFile", &overlapped, 1000, &count, &timedOut);
                else
                    status = PortError("ReadFile", error);
            }
            else
                status = CompleteOverlappedNoLock("ReadFile", &overlapped, 1000,
                    &count, &timedOut);
            CloseHandle(overlapped.hEvent);
            if (!status.success || timedOut)
            {
                output->resize(offset);
                return timedOut ? PortError("ReadFile", ERROR_TIMEOUT) : status;
            }
            output->resize(offset + count);
            g_diagnostics.totalRxBytes = AddCounter(g_diagnostics.totalRxBytes, count);
            if (count) LogTraffic("RX", &(*output)[offset], count);
            return Status::Ok();
        }

        Status WriteBytesNoLock(const unsigned char* data, DWORD count, DWORD timeoutMs,
            DWORD* bytesWritten)
        {
            *bytesWritten = 0;
            if (count == 0) return Status::Ok();
            OVERLAPPED overlapped = { 0 };
            overlapped.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
            if (!overlapped.hEvent) return PortError("CreateEvent", GetLastError());

            DWORD written = 0;
            bool timedOut = false;
            Status status = Status::Ok();
            BOOL started = WriteFile(g_port, data, count, NULL, &overlapped);
            if (!started)
            {
                const DWORD error = GetLastError();
                if (error == ERROR_IO_PENDING)
                    status = CompleteOverlappedNoLock("WriteFile", &overlapped, timeoutMs, &written, &timedOut);
                else
                    status = PortError("WriteFile", error);
            }
            else
                status = CompleteOverlappedNoLock("WriteFile", &overlapped,
                    timeoutMs, &written, &timedOut);
            CloseHandle(overlapped.hEvent);
            *bytesWritten = written;
            g_diagnostics.totalTxBytes = AddCounter(g_diagnostics.totalTxBytes, written);
            if (written) LogTraffic("TX", data, written);
            if (!status.success) return status;
            if (timedOut || written != count) return PortError("WriteFile", ERROR_TIMEOUT);
            return Status::Ok();
        }

        Status ReadExactNoLock(DWORD requestedCount, DWORD timeoutMs,
            std::vector<unsigned char>* data, bool* timedOut)
        {
            data->clear();
            *timedOut = false;
            const DWORD start = GetTickCount();
            while (data->size() < requestedCount)
            {
                if (CancellationRequested()) return PortError("serial operation cancelled", ERROR_OPERATION_ABORTED);
                Status status = ReadChunkNoLock(requestedCount - static_cast<DWORD>(data->size()), data);
                if (!status.success) return status;
                if (data->size() >= requestedCount) break;
                if (timeoutMs == 0 || HasElapsed(start, timeoutMs))
                {
                    *timedOut = true;
                    break;
                }
                Sleep(1);
            }
            return Status::Ok();
        }

        bool EndsWith(const std::vector<unsigned char>& data,
            const std::vector<unsigned char>& suffix)
        {
            if (suffix.empty() || data.size() < suffix.size()) return false;
            return std::equal(suffix.rbegin(), suffix.rend(), data.rbegin());
        }

        Status ReadTerminatedNoLock(DWORD maximum,
            const std::vector<unsigned char>& terminator, DWORD timeoutMs,
            std::vector<unsigned char>* data, bool* timedOut)
        {
            data->clear();
            *timedOut = false;
            const DWORD start = GetTickCount();
            while (data->size() < maximum)
            {
                if (CancellationRequested()) return PortError("serial operation cancelled", ERROR_OPERATION_ABORTED);
                Status status = ReadChunkNoLock(1, data);
                if (!status.success) return status;
                if (EndsWith(*data, terminator)) break;
                if (timeoutMs == 0 || HasElapsed(start, timeoutMs))
                {
                    *timedOut = true;
                    break;
                }
                Sleep(1);
            }
            if (data->size() >= maximum && !EndsWith(*data, terminator))
                return Status::Validation(ErrorBufferTooSmall,
                    "Terminator was not received before the response array limit");
            return Status::Ok();
        }

        Status ReadUntilIdleNoLock(DWORD maximum, DWORD overallTimeoutMs,
            DWORD interByteTimeoutMs, std::vector<unsigned char>* data, bool* timedOut)
        {
            data->clear();
            *timedOut = false;
            const DWORD start = GetTickCount();
            DWORD lastByte = start;
            while (data->size() < maximum)
            {
                if (CancellationRequested()) return PortError("serial operation cancelled", ERROR_OPERATION_ABORTED);
                const size_t before = data->size();
                Status status = ReadChunkNoLock(maximum - static_cast<DWORD>(data->size()), data);
                if (!status.success) return status;
                if (data->size() != before) lastByte = GetTickCount();
                if (!data->empty() && HasElapsed(lastByte, interByteTimeoutMs)) break;
                if (overallTimeoutMs == 0 || HasElapsed(start, overallTimeoutMs))
                {
                    *timedOut = data->empty();
                    break;
                }
                Sleep(1);
            }
            return Status::Ok();
        }

        char UpperAscii(char value)
        {
            return static_cast<char>(std::toupper(static_cast<unsigned char>(value)));
        }
    }

    Status::Status() : success(true), code(0) {}

    Status Status::Ok()
    {
        return Status();
    }

    Status Status::Validation(long codeValue, const char* messageValue)
    {
        Status result;
        result.success = false;
        result.code = codeValue;
        result.message = messageValue ? messageValue : "";
        return result;
    }

    Status Status::Win32Error(const char* operation, DWORD codeValue)
    {
        Status result;
        result.success = false;
        result.code = static_cast<long>(codeValue);
        char* systemText = NULL;
        FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS, NULL, codeValue, 0,
            reinterpret_cast<char*>(&systemText), 0, NULL);
        result.message = operation ? operation : "Windows operation";
        result.message += " failed";
        if (systemText)
        {
            result.message += ": ";
            result.message += systemText;
            while (!result.message.empty() &&
                (result.message[result.message.size() - 1] == '\r' ||
                 result.message[result.message.size() - 1] == '\n'))
                result.message.erase(result.message.size() - 1);
            LocalFree(systemText);
        }
        return result;
    }

    PortConfig::PortConfig()
        : port("COM1"), baudRate(9600), dataBits(8), stopBits(1), parity(0),
          flowControl(0), dtrMode(-1), rtsMode(-1), readTimeoutMs(1000),
          writeTimeoutMs(1000), flushOnOpen(true), logging(0)
    {
    }

    Diagnostics::Diagnostics()
        : frameErrors(0), parityErrors(0), overrunErrors(0),
          bufferOverrunErrors(0), breakCount(0), rxBytesQueued(0),
          txBytesQueued(0), totalRxBytes(0), totalTxBytes(0), lastWin32Error(0)
    {
    }

    TransactionConfig::TransactionConfig()
        : flushBeforeWrite(true), responseMode(0), responseCount(0),
          overallTimeoutMs(1000), interByteTimeoutMs(20),
          preTransmitDelayMs(0), postTransmitDelayMs(0), retries(0)
    {
    }

    void Initialize(HMODULE module)
    {
        g_module = module;
        InitializeCriticalSection(&g_lock);
        g_lockInitialized = true;
    }

    void Shutdown()
    {
        if (!g_lockInitialized) return;
        {
            ScopedLock lock;
            ClosePortNoLock();
            CloseLog();
        }
        DeleteCriticalSection(&g_lock);
        g_lockInitialized = false;
        g_module = NULL;
    }

    std::string NormalizePortName(const char* port, Status* status)
    {
        if (status) *status = Status::Ok();
        if (!port)
        {
            if (status) *status = Status::Validation(ErrorInvalidParameter, "Port is required");
            return "";
        }
        std::string value(port);
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value[0]))) value.erase(0, 1);
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value[value.size() - 1]))) value.erase(value.size() - 1);
        for (size_t i = 0; i < value.size(); ++i) value[i] = UpperAscii(value[i]);
        if (value.compare(0, 4, "\\\\.\\") == 0) value.erase(0, 4);
        if (value.size() < 4 || value.compare(0, 3, "COM") != 0)
        {
            if (status) *status = Status::Validation(ErrorInvalidParameter, "Port must be COM1 through COM999");
            return "";
        }
        long number = 0;
        for (size_t i = 3; i < value.size(); ++i)
        {
            if (!std::isdigit(static_cast<unsigned char>(value[i])))
            {
                if (status) *status = Status::Validation(ErrorInvalidParameter, "Port must be COM1 through COM999");
                return "";
            }
            number = number * 10 + (value[i] - '0');
        }
        if (number < 1 || number > 999)
        {
            if (status) *status = Status::Validation(ErrorInvalidParameter, "Port must be COM1 through COM999");
            return "";
        }
        std::ostringstream stream;
        stream << "\\\\.\\COM" << number;
        return stream.str();
    }

    Status Start(const PortConfig& requested)
    {
        ScopedLock lock;
        ClosePortNoLock();
        CloseLog();
        g_diagnostics = Diagnostics();
        InterlockedExchange(&g_cancelRequested, 0);

        Status validation;
        const std::string path = NormalizePortName(requested.port.c_str(), &validation);
        if (!validation.success) return validation;
        if (requested.baudRate <= 0) return Status::Validation(ErrorInvalidParameter, "BaudRate must be positive");
        if (requested.dataBits < 5 || requested.dataBits > 8) return Status::Validation(ErrorInvalidParameter, "DataBits must be 5 through 8");
        if (requested.stopBits != 1 && requested.stopBits != 15 && requested.stopBits != 2) return Status::Validation(ErrorInvalidParameter, "StopBits must be 1, 15, or 2");
        if (requested.stopBits == 15 && requested.dataBits != 5) return Status::Validation(ErrorInvalidParameter, "1.5 stop bits requires 5 data bits");
        if (requested.stopBits == 2 && requested.dataBits == 5) return Status::Validation(ErrorInvalidParameter, "5 data bits cannot use 2 stop bits");
        if (requested.parity < 0 || requested.parity > 4) return Status::Validation(ErrorInvalidParameter, "Parity must be 0 through 4");
        if (requested.flowControl < 0 || requested.flowControl > 3) return Status::Validation(ErrorInvalidParameter, "FlowControl must be 0 through 3");
        if (requested.dtrMode < -1 || requested.dtrMode > 2 || requested.rtsMode < -1 || requested.rtsMode > 2)
            return Status::Validation(ErrorInvalidParameter, "DTRMode and RTSMode must be -1 through 2");
        if (requested.readTimeoutMs < 0 || requested.writeTimeoutMs < 0) return Status::Validation(ErrorInvalidParameter, "Configured timeouts cannot be negative");
        if (requested.logging < 0 || requested.logging > 3) return Status::Validation(ErrorInvalidParameter, "Logging must be 0 through 3");

        g_config = requested;
        g_config.dtrMode = requested.dtrMode < 0 ? (requested.flowControl == 3 ? 2 : 1) : requested.dtrMode;
        g_config.rtsMode = requested.rtsMode < 0 ? (requested.flowControl == 2 ? 2 : 1) : requested.rtsMode;
        if (requested.flowControl == 2 && g_config.rtsMode != 2)
            return Status::Validation(ErrorHandshakeConflict, "RTSMode must be automatic or handshake for RTS/CTS flow control");
        if (requested.flowControl == 3 && g_config.dtrMode != 2)
            return Status::Validation(ErrorHandshakeConflict, "DTRMode must be automatic or handshake for DTR/DSR flow control");

        if (g_config.logging) OpenLog();
        g_port = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL,
            OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
        if (g_port == INVALID_HANDLE_VALUE) return FailOpen(PortError("CreateFile", GetLastError()));
        if (!SetupComm(g_port, 65536, 65536))
            return FailOpen(PortError("SetupComm", GetLastError()));

        DCB dcb = { 0 };
        dcb.DCBlength = sizeof(dcb);
        if (!GetCommState(g_port, &dcb))
            return FailOpen(PortError("GetCommState", GetLastError()));
        dcb.BaudRate = static_cast<DWORD>(requested.baudRate);
        dcb.ByteSize = static_cast<BYTE>(requested.dataBits);
        const BYTE parityValues[] = { NOPARITY, ODDPARITY, EVENPARITY, MARKPARITY, SPACEPARITY };
        dcb.Parity = parityValues[requested.parity];
        dcb.fParity = requested.parity != 0;
        dcb.StopBits = requested.stopBits == 1 ? ONESTOPBIT :
            (requested.stopBits == 15 ? ONE5STOPBITS : TWOSTOPBITS);
        dcb.fBinary = TRUE;
        dcb.fOutxCtsFlow = FALSE;
        dcb.fOutxDsrFlow = FALSE;
        dcb.fDsrSensitivity = FALSE;
        dcb.fTXContinueOnXoff = TRUE;
        dcb.fOutX = FALSE;
        dcb.fInX = FALSE;
        dcb.fErrorChar = FALSE;
        dcb.fNull = FALSE;
        dcb.fAbortOnError = FALSE;
        dcb.XonChar = 0x11;
        dcb.XoffChar = 0x13;
        dcb.XonLim = 128;
        dcb.XoffLim = 128;
        dcb.fDtrControl = g_config.dtrMode == 0 ? DTR_CONTROL_DISABLE :
            (g_config.dtrMode == 1 ? DTR_CONTROL_ENABLE : DTR_CONTROL_HANDSHAKE);
        dcb.fRtsControl = g_config.rtsMode == 0 ? RTS_CONTROL_DISABLE :
            (g_config.rtsMode == 1 ? RTS_CONTROL_ENABLE : RTS_CONTROL_HANDSHAKE);
        if (requested.flowControl == 1) { dcb.fInX = TRUE; dcb.fOutX = TRUE; }
        else if (requested.flowControl == 2) dcb.fOutxCtsFlow = TRUE;
        else if (requested.flowControl == 3) dcb.fOutxDsrFlow = TRUE;
        if (!SetCommState(g_port, &dcb))
            return FailOpen(PortError("SetCommState", GetLastError()));

        DCB effective = { 0 };
        effective.DCBlength = sizeof(effective);
        if (!GetCommState(g_port, &effective))
            return FailOpen(PortError("GetCommState(effective)", GetLastError()));
        g_config.baudRate = static_cast<long>(effective.BaudRate);
        g_config.dataBits = effective.ByteSize;
        g_config.parity = effective.Parity;
        g_config.stopBits = effective.StopBits == ONESTOPBIT ? 1 :
            (effective.StopBits == ONE5STOPBITS ? 15 : 2);
        g_config.dtrMode = effective.fDtrControl == DTR_CONTROL_DISABLE ? 0 :
            (effective.fDtrControl == DTR_CONTROL_ENABLE ? 1 : 2);
        g_config.rtsMode = effective.fRtsControl == RTS_CONTROL_DISABLE ? 0 :
            (effective.fRtsControl == RTS_CONTROL_ENABLE ? 1 : 2);

        COMMTIMEOUTS timeouts = { 0 };
        if (!SetCommTimeouts(g_port, &timeouts))
            return FailOpen(PortError("SetCommTimeouts", GetLastError()));
        if (requested.flushOnOpen &&
            !PurgeComm(g_port, PURGE_RXCLEAR | PURGE_TXCLEAR))
            return FailOpen(PortError("PurgeComm", GetLastError()));

        g_config.port = path.substr(4);
        g_dtrState = g_config.dtrMode == 0 ? 0 : 1;
        g_rtsState = g_config.rtsMode == 0 ? 0 : 1;
        g_breakState = 0;
        LogLine(1, "opened %s at %ld,%ld,%ld,%ld flow=%ld DTR=%ld RTS=%ld",
            g_config.port.c_str(), g_config.baudRate, g_config.dataBits,
            g_config.parity, g_config.stopBits, g_config.flowControl,
            g_config.dtrMode, g_config.rtsMode);
        return Status::Ok();
    }

    Status Stop()
    {
        ScopedLock lock;
        ClosePortNoLock();
        LogLine(1, "serial port closed");
        CloseLog();
        return Status::Ok();
    }

    Status CancelPending()
    {
        InterlockedExchange(&g_cancelRequested, 1);
        const HANDLE port = g_port;
        if (port != INVALID_HANDLE_VALUE && !CancelIoEx(port, NULL))
        {
            const DWORD error = GetLastError();
            if (error != ERROR_NOT_FOUND && error != ERROR_INVALID_HANDLE)
                return Status::Win32Error("CancelIoEx", error);
        }
        return Status::Ok();
    }

    Status IsOpen(bool* open, std::string* port)
    {
        if (!open || !port) return Status::Validation(ErrorInvalidParameter, "Open and Port outputs are required");
        ScopedLock lock;
        *open = g_port != INVALID_HANDLE_VALUE;
        *port = *open ? g_config.port : "";
        return Status::Ok();
    }

    Status GetConfiguration(PortConfig* config, bool* open)
    {
        if (!config || !open) return Status::Validation(ErrorInvalidParameter, "Configuration outputs are required");
        ScopedLock lock;
        *open = g_port != INVALID_HANDLE_VALUE;
        *config = g_config;
        if (!*open) config->port.clear();
        return Status::Ok();
    }

    Status GetDiagnostics(Diagnostics* diagnostics, bool resetAfterRead)
    {
        if (!diagnostics) return Status::Validation(ErrorInvalidParameter, "Diagnostics output is required");
        ScopedLock lock;
        if (g_port != INVALID_HANDLE_VALUE)
        {
            COMSTAT stat;
            const Status status = QueryQueuesNoLock(&stat);
            if (!status.success) return status;
            g_diagnostics.rxBytesQueued = stat.cbInQue;
            g_diagnostics.txBytesQueued = stat.cbOutQue;
        }
        else
        {
            g_diagnostics.rxBytesQueued = 0;
            g_diagnostics.txBytesQueued = 0;
        }
        *diagnostics = g_diagnostics;
        if (resetAfterRead)
        {
            const DWORD rxQueued = g_diagnostics.rxBytesQueued;
            const DWORD txQueued = g_diagnostics.txBytesQueued;
            g_diagnostics = Diagnostics();
            g_diagnostics.rxBytesQueued = rxQueued;
            g_diagnostics.txBytesQueued = txQueued;
        }
        return Status::Ok();
    }

    Status GetBufferLength(DWORD* bytesAvailable)
    {
        if (!bytesAvailable) return Status::Validation(ErrorInvalidParameter, "BytesAvailable output is required");
        ScopedLock lock;
        *bytesAvailable = 0;
        if (g_port == INVALID_HANDLE_VALUE) return Status::Validation(ErrorNotOpen, "Serial port is not open");
        COMSTAT stat;
        const Status status = QueryQueuesNoLock(&stat);
        if (status.success) *bytesAvailable = stat.cbInQue;
        return status;
    }

    Status ReadBytes(DWORD requestedCount, DWORD timeoutMs, DWORD capacity,
        std::vector<unsigned char>* data, bool* timedOut)
    {
        if (!data || !timedOut) return Status::Validation(ErrorInvalidParameter, "Read output is required");
        ScopedLock lock;
        data->clear();
        *timedOut = false;
        if (g_port == INVALID_HANDLE_VALUE) return Status::Validation(ErrorNotOpen, "Serial port is not open");
        if (requestedCount > capacity) return Status::Validation(ErrorBufferTooSmall, "RequestedCount exceeds Data array capacity");
        if (requestedCount == 0) return ReadChunkNoLock(capacity, data);
        InterlockedExchange(&g_cancelRequested, 0);
        return ReadExactNoLock(requestedCount,
            ResolveTimeout(timeoutMs, static_cast<DWORD>(g_config.readTimeoutMs)),
            data, timedOut);
    }

    Status ReadString(DWORD maxChars, const std::vector<unsigned char>& terminator,
        DWORD timeoutMs, bool includeTerminator, std::string* text,
        DWORD* bytesRead, bool* timedOut)
    {
        if (!text || !bytesRead || !timedOut) return Status::Validation(ErrorInvalidParameter, "String read output is required");
        ScopedLock lock;
        text->clear();
        *bytesRead = 0;
        *timedOut = false;
        if (g_port == INVALID_HANDLE_VALUE) return Status::Validation(ErrorNotOpen, "Serial port is not open");
        if (maxChars == 0) return Status::Validation(ErrorInvalidParameter, "MaxChars must be positive");
        InterlockedExchange(&g_cancelRequested, 0);
        std::vector<unsigned char> data;
        Status status;
        const DWORD effective = ResolveTimeout(timeoutMs, static_cast<DWORD>(g_config.readTimeoutMs));
        if (terminator.empty())
            status = ReadUntilIdleNoLock(maxChars, effective, 1, &data, timedOut);
        else
            status = ReadTerminatedNoLock(maxChars, terminator, effective, &data, timedOut);
        if (!status.success) return status;
        *bytesRead = static_cast<DWORD>(data.size());
        if (std::find(data.begin(), data.end(), static_cast<unsigned char>(0)) != data.end())
            return Status::Validation(ErrorBinaryString, "Received NUL byte; use a binary or hex read action");
        size_t textLength = data.size();
        if (!includeTerminator && EndsWith(data, terminator)) textLength -= terminator.size();
        if (textLength) text->assign(reinterpret_cast<const char*>(&data[0]), textLength);
        return Status::Ok();
    }

    Status ReadUntilIdle(DWORD maximum, DWORD overallTimeoutMs,
        DWORD interByteTimeoutMs, std::vector<unsigned char>* data, bool* timedOut)
    {
        if (!data || !timedOut || maximum == 0)
            return Status::Validation(ErrorInvalidParameter, "Read outputs and positive MaxBytes are required");
        if (interByteTimeoutMs == 0)
            return Status::Validation(ErrorInvalidParameter, "InterByteTimeoutMs must be positive");
        ScopedLock lock;
        if (g_port == INVALID_HANDLE_VALUE) return Status::Validation(ErrorNotOpen, "Serial port is not open");
        InterlockedExchange(&g_cancelRequested, 0);
        return ReadUntilIdleNoLock(maximum,
            ResolveTimeout(overallTimeoutMs, static_cast<DWORD>(g_config.readTimeoutMs)),
            interByteTimeoutMs, data, timedOut);
    }

    Status WriteBytes(const unsigned char* data, DWORD count, DWORD timeoutMs,
        DWORD* bytesWritten)
    {
        if (!bytesWritten || (count && !data))
            return Status::Validation(ErrorInvalidParameter, "Write input/output is required");
        ScopedLock lock;
        *bytesWritten = 0;
        if (g_port == INVALID_HANDLE_VALUE) return Status::Validation(ErrorNotOpen, "Serial port is not open");
        InterlockedExchange(&g_cancelRequested, 0);
        return WriteBytesNoLock(data, count,
            ResolveTimeout(timeoutMs, static_cast<DWORD>(g_config.writeTimeoutMs)),
            bytesWritten);
    }

    Status Transaction(const unsigned char* request, DWORD requestCount,
        DWORD responseCapacity, const TransactionConfig& config,
        std::vector<unsigned char>* response, DWORD* bytesWritten,
        bool* timedOut, long* attempts)
    {
        if ((requestCount && !request) || !response || !bytesWritten || !timedOut || !attempts)
            return Status::Validation(ErrorInvalidParameter, "Transaction input/output is required");
        if (config.responseMode < 0 || config.responseMode > 2)
            return Status::Validation(ErrorInvalidParameter, "ResponseMode must be 0, 1, or 2");
        if (responseCapacity == 0)
            return Status::Validation(ErrorBufferTooSmall, "ResponseData array is required");
        if (config.responseMode == 1 && (config.responseCount == 0 || config.responseCount > responseCapacity))
            return Status::Validation(ErrorBufferTooSmall, "ResponseCount must fit the Response array");
        if (config.responseMode == 2 && config.terminator.empty())
            return Status::Validation(ErrorInvalidParameter, "Terminator is required for ResponseMode 2");
        if (config.interByteTimeoutMs == 0 || config.retries < 0 || config.retries > 100)
            return Status::Validation(ErrorInvalidParameter, "InterByteTimeoutMs must be positive and Retries must be 0 through 100");
        if (config.preTransmitDelayMs > 60000 || config.postTransmitDelayMs > 60000)
            return Status::Validation(ErrorInvalidParameter, "Transaction delays cannot exceed 60000 ms");

        ScopedLock lock;
        response->clear();
        *bytesWritten = 0;
        *timedOut = false;
        *attempts = 0;
        if (g_port == INVALID_HANDLE_VALUE) return Status::Validation(ErrorNotOpen, "Serial port is not open");
        InterlockedExchange(&g_cancelRequested, 0);
        const DWORD responseTimeout = ResolveTimeout(config.overallTimeoutMs,
            static_cast<DWORD>(g_config.readTimeoutMs));
        for (long attempt = 0; attempt <= config.retries; ++attempt)
        {
            *attempts = attempt + 1;
            response->clear();
            *bytesWritten = 0;
            *timedOut = false;
            if (config.flushBeforeWrite &&
                !PurgeComm(g_port, PURGE_RXABORT | PURGE_RXCLEAR))
                return PortError("PurgeComm", GetLastError());
            if (config.preTransmitDelayMs)
            {
                Status delayStatus = DelayNoLock(config.preTransmitDelayMs);
                if (!delayStatus.success) return delayStatus;
            }
            Status status = WriteBytesNoLock(request, requestCount,
                static_cast<DWORD>(g_config.writeTimeoutMs), bytesWritten);
            if (!status.success) return status;
            if (config.postTransmitDelayMs)
            {
                Status delayStatus = DelayNoLock(config.postTransmitDelayMs);
                if (!delayStatus.success) return delayStatus;
            }
            if (config.responseMode == 0)
                status = ReadUntilIdleNoLock(responseCapacity, responseTimeout,
                    config.interByteTimeoutMs, response, timedOut);
            else if (config.responseMode == 1)
                status = ReadExactNoLock(config.responseCount,
                    responseTimeout, response, timedOut);
            else
                status = ReadTerminatedNoLock(responseCapacity, config.terminator,
                    responseTimeout, response, timedOut);
            if (!status.success || !*timedOut) return status;
            LogLine(1, "transaction attempt %ld timed out", attempt + 1);
        }
        return Status::Ok();
    }

    Status Flush(long mask)
    {
        ScopedLock lock;
        if (g_port == INVALID_HANDLE_VALUE) return Status::Validation(ErrorNotOpen, "Serial port is not open");
        if (mask < 1 || mask > 3) return Status::Validation(ErrorInvalidParameter, "FlushMask must be 1, 2, or 3");
        DWORD flags = 0;
        if (mask & 1) flags |= PURGE_RXABORT | PURGE_RXCLEAR;
        if (mask & 2) flags |= PURGE_TXABORT | PURGE_TXCLEAR;
        return PurgeComm(g_port, flags) ? Status::Ok() : PortError("PurgeComm", GetLastError());
    }

    Status DrainTransmit(DWORD timeoutMs, bool* timedOut)
    {
        if (!timedOut) return Status::Validation(ErrorInvalidParameter, "TimedOut output is required");
        ScopedLock lock;
        *timedOut = false;
        if (g_port == INVALID_HANDLE_VALUE) return Status::Validation(ErrorNotOpen, "Serial port is not open");
        InterlockedExchange(&g_cancelRequested, 0);
        const DWORD start = GetTickCount();
        for (;;)
        {
            if (CancellationRequested()) return PortError("serial operation cancelled", ERROR_OPERATION_ABORTED);
            COMSTAT stat;
            const Status status = QueryQueuesNoLock(&stat);
            if (!status.success) return status;
            if (stat.cbOutQue == 0) return Status::Ok();
            if (timeoutMs == 0 || HasElapsed(start, timeoutMs))
            {
                *timedOut = true;
                return Status::Ok();
            }
            Sleep(1);
        }
    }

    Status SetControlLines(long dtr, long rts, long breakState)
    {
        ScopedLock lock;
        if (g_port == INVALID_HANDLE_VALUE) return Status::Validation(ErrorNotOpen, "Serial port is not open");
        InterlockedExchange(&g_cancelRequested, 0);
        if ((dtr < -1 || dtr > 1) || (rts < -1 || rts > 1) || (breakState < -1 || breakState > 1))
            return Status::Validation(ErrorInvalidParameter, "DTR, RTS, and Break must be -1, 0, or 1");
        if (rts != -1 && g_config.rtsMode == 2) return Status::Validation(ErrorHandshakeConflict, "RTS is controlled by handshake mode");
        if (dtr != -1 && g_config.dtrMode == 2) return Status::Validation(ErrorHandshakeConflict, "DTR is controlled by handshake mode");
        if (dtr != -1 && !EscapeCommFunction(g_port, dtr ? SETDTR : CLRDTR)) return PortError("EscapeCommFunction(DTR)", GetLastError());
        if (rts != -1 && !EscapeCommFunction(g_port, rts ? SETRTS : CLRRTS)) return PortError("EscapeCommFunction(RTS)", GetLastError());
        if (breakState != -1 && !EscapeCommFunction(g_port, breakState ? SETBREAK : CLRBREAK)) return PortError("EscapeCommFunction(BREAK)", GetLastError());
        if (dtr != -1) g_dtrState = dtr;
        if (rts != -1) g_rtsState = rts;
        if (breakState != -1) g_breakState = breakState;
        return Status::Ok();
    }

    Status PulseControlLine(long line, long state, DWORD durationMs, long restoreState)
    {
        if (line < 0 || line > 2 || state < 0 || state > 1 ||
            restoreState < -1 || restoreState > 1 || durationMs > 60000)
            return Status::Validation(ErrorInvalidParameter, "Line must be 0..2, states -1..1, and duration <= 60000 ms");
        ScopedLock lock;
        if (g_port == INVALID_HANDLE_VALUE) return Status::Validation(ErrorNotOpen, "Serial port is not open");
        InterlockedExchange(&g_cancelRequested, 0);
        if ((line == 0 && g_config.dtrMode == 2) || (line == 1 && g_config.rtsMode == 2))
            return Status::Validation(ErrorHandshakeConflict, "Selected line is controlled by handshake mode");
        const long previous = line == 0 ? g_dtrState : (line == 1 ? g_rtsState : g_breakState);
        const DWORD setCode = line == 0 ? (state ? SETDTR : CLRDTR) :
            (line == 1 ? (state ? SETRTS : CLRRTS) : (state ? SETBREAK : CLRBREAK));
        if (!EscapeCommFunction(g_port, setCode)) return PortError("EscapeCommFunction(pulse)", GetLastError());
        const Status delayStatus = DelayNoLock(durationMs);
        const long restore = restoreState < 0 ? previous : restoreState;
        const DWORD restoreCode = line == 0 ? (restore ? SETDTR : CLRDTR) :
            (line == 1 ? (restore ? SETRTS : CLRRTS) : (restore ? SETBREAK : CLRBREAK));
        if (!EscapeCommFunction(g_port, restoreCode)) return PortError("EscapeCommFunction(restore)", GetLastError());
        if (line == 0) g_dtrState = restore;
        else if (line == 1) g_rtsState = restore;
        else g_breakState = restore;
        return delayStatus;
    }

    Status GetLineStatus(long* cts, long* dsr, long* dcd, long* ring)
    {
        if (!cts || !dsr || !dcd || !ring) return Status::Validation(ErrorInvalidParameter, "Line status outputs are required");
        ScopedLock lock;
        *cts = *dsr = *dcd = *ring = 0;
        if (g_port == INVALID_HANDLE_VALUE) return Status::Validation(ErrorNotOpen, "Serial port is not open");
        DWORD status = 0;
        if (!GetCommModemStatus(g_port, &status)) return PortError("GetCommModemStatus", GetLastError());
        *cts = (status & MS_CTS_ON) ? 1 : 0;
        *dsr = (status & MS_DSR_ON) ? 1 : 0;
        *dcd = (status & MS_RLSD_ON) ? 1 : 0;
        *ring = (status & MS_RING_ON) ? 1 : 0;
        return Status::Ok();
    }

    Status DecodeEscapes(const char* text, std::vector<unsigned char>* bytes)
    {
        if (!bytes) return Status::Validation(ErrorInvalidParameter, "Escape output is required");
        bytes->clear();
        if (!text) return Status::Ok();
        for (size_t i = 0; text[i]; ++i)
        {
            const unsigned char value = static_cast<unsigned char>(text[i]);
            if (value != '\\') { bytes->push_back(value); continue; }
            const char next = text[++i];
            if (!next) return Status::Validation(ErrorInvalidParameter, "Trailing backslash in escaped text");
            if (next == 'r') bytes->push_back('\r');
            else if (next == 'n') bytes->push_back('\n');
            else if (next == 't') bytes->push_back('\t');
            else if (next == '\\') bytes->push_back('\\');
            else if (next == 'x')
            {
                const char high = text[++i];
                const char low = high ? text[++i] : 0;
                if (!high || !low || !std::isxdigit(static_cast<unsigned char>(high)) ||
                    !std::isxdigit(static_cast<unsigned char>(low)))
                    return Status::Validation(ErrorInvalidParameter, "\\x escapes require exactly two hexadecimal digits");
                const char pair[] = { high, low, 0 };
                bytes->push_back(static_cast<unsigned char>(std::strtoul(pair, NULL, 16)));
            }
            else return Status::Validation(ErrorInvalidParameter, "Unsupported escape sequence");
        }
        return Status::Ok();
    }

    Status DecodeHex(const char* text, std::vector<unsigned char>* bytes)
    {
        if (!bytes) return Status::Validation(ErrorInvalidParameter, "Hex output is required");
        bytes->clear();
        if (!text) return Status::Ok();
        int high = -1;
        for (size_t i = 0; text[i]; ++i)
        {
            const unsigned char value = static_cast<unsigned char>(text[i]);
            if (std::isspace(value) || value == ',' || value == ':' || value == '-') continue;
            if (value == '0' && (text[i + 1] == 'x' || text[i + 1] == 'X') && high < 0)
            {
                ++i;
                continue;
            }
            if (!std::isxdigit(value))
                return Status::Validation(ErrorInvalidHex, "Hex text contains an invalid character");
            const int nibble = std::isdigit(value) ? value - '0' : UpperAscii(value) - 'A' + 10;
            if (high < 0) high = nibble;
            else
            {
                bytes->push_back(static_cast<unsigned char>((high << 4) | nibble));
                high = -1;
            }
        }
        if (high >= 0) return Status::Validation(ErrorInvalidHex, "Hex text must contain complete byte pairs");
        return Status::Ok();
    }

    std::string FormatHex(const unsigned char* data, size_t count)
    {
        if (!data || count == 0) return "";
        std::ostringstream stream;
        stream << std::uppercase << std::hex << std::setfill('0');
        for (size_t i = 0; i < count; ++i)
        {
            if (i) stream << ' ';
            stream << std::setw(2) << static_cast<unsigned int>(data[i]);
        }
        return stream.str();
    }
}
