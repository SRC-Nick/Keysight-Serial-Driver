#include "SRCSerial_Internal.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace srcserial
{
    namespace
    {
        HMODULE g_module = NULL;
        HANDLE g_port = INVALID_HANDLE_VALUE;
        CRITICAL_SECTION g_lock;
        bool g_lockInitialized = false;
        long g_flowControl = 0;
        DWORD g_readTimeoutMs = 1000;
        DWORD g_writeTimeoutMs = 1000;
        FILE* g_log = NULL;

        class ScopedLock
        {
        public:
            ScopedLock() { if (g_lockInitialized) EnterCriticalSection(&g_lock); }
            ~ScopedLock() { if (g_lockInitialized) LeaveCriticalSection(&g_lock); }
        private:
            ScopedLock(const ScopedLock&);
            ScopedLock& operator=(const ScopedLock&);
        };

        void Log(const char* text)
        {
            if (!g_log || !text) return;
            SYSTEMTIME now;
            GetLocalTime(&now);
            std::fprintf(g_log, "%04u-%02u-%02u %02u:%02u:%02u.%03u %s\n",
                now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
                now.wSecond, now.wMilliseconds, text);
            std::fflush(g_log);
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
            std::string directory = std::string(modulePath) + "\\logs";
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
            if (g_port != INVALID_HANDLE_VALUE)
            {
                PurgeComm(g_port, PURGE_RXABORT | PURGE_RXCLEAR | PURGE_TXABORT | PURGE_TXCLEAR);
                CloseHandle(g_port);
                g_port = INVALID_HANDLE_VALUE;
            }
        }

        DWORD ResolveTimeout(DWORD requested, DWORD configured)
        {
            return requested == MAXDWORD ? configured : requested;
        }

        bool HasElapsed(DWORD start, DWORD timeout)
        {
            return timeout != INFINITE && static_cast<DWORD>(GetTickCount() - start) >= timeout;
        }

        Status ReadAvailableNoLock(DWORD maximum, std::vector<unsigned char>* output)
        {
            COMSTAT stat = { 0 };
            DWORD errors = 0;
            if (!ClearCommError(g_port, &errors, &stat))
                return Status::Win32Error("ClearCommError", GetLastError());
            const DWORD toRead = (std::min)(maximum, stat.cbInQue);
            if (toRead == 0) return Status::Ok();
            const size_t offset = output->size();
            output->resize(offset + toRead);
            DWORD count = 0;
            if (!ReadFile(g_port, &(*output)[offset], toRead, &count, NULL))
            {
                output->resize(offset);
                return Status::Win32Error("ReadFile", GetLastError());
            }
            output->resize(offset + count);
            return Status::Ok();
        }

        bool EndsWith(const std::vector<unsigned char>& data, const std::vector<unsigned char>& suffix)
        {
            if (suffix.empty() || data.size() < suffix.size()) return false;
            return std::equal(suffix.rbegin(), suffix.rend(), data.rbegin());
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

    Status Status::Validation(long value, const char* text)
    {
        Status result;
        result.success = false;
        result.code = value;
        result.message = text ? text : "Invalid parameter";
        return result;
    }

    Status Status::Win32Error(const char* operation, DWORD value)
    {
        Status result;
        result.success = false;
        result.code = static_cast<long>(value);
        char* systemText = NULL;
        const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
        FormatMessageA(flags, NULL, value, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            reinterpret_cast<LPSTR>(&systemText), 0, NULL);
        result.message = operation ? operation : "Windows serial operation";
        if (systemText)
        {
            result.message += ": ";
            result.message += systemText;
            while (!result.message.empty() &&
                (result.message[result.message.size() - 1] == '\r' || result.message[result.message.size() - 1] == '\n'))
                result.message.erase(result.message.size() - 1);
            LocalFree(systemText);
        }
        return result;
    }

    PortConfig::PortConfig()
        : port("COM1"), baudRate(9600), dataBits(8), stopBits(1), parity(0),
          flowControl(0), readTimeoutMs(1000), writeTimeoutMs(1000),
          flushOnOpen(true), logging(false)
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
        std::string text = port ? port : "";
        while (!text.empty() && std::isspace(static_cast<unsigned char>(text[0]))) text.erase(0, 1);
        while (!text.empty() && std::isspace(static_cast<unsigned char>(text[text.size() - 1]))) text.erase(text.size() - 1);
        std::string candidate = text;
        std::transform(candidate.begin(), candidate.end(), candidate.begin(), UpperAscii);
        const std::string prefix = "\\\\.\\";
        if (candidate.compare(0, prefix.size(), prefix) == 0) candidate.erase(0, prefix.size());
        if (candidate.size() < 4 || candidate.compare(0, 3, "COM") != 0)
        {
            if (status) *status = Status::Validation(ErrorInvalidParameter, "Port must be COM followed by a positive number");
            return "";
        }
        for (size_t i = 3; i < candidate.size(); ++i)
        {
            if (!std::isdigit(static_cast<unsigned char>(candidate[i])))
            {
                if (status) *status = Status::Validation(ErrorInvalidParameter, "Port must be COM followed by a positive number");
                return "";
            }
        }
        const long number = std::strtol(candidate.c_str() + 3, NULL, 10);
        if (number <= 0 || number > 999)
        {
            if (status) *status = Status::Validation(ErrorInvalidParameter, "COM port number must be between 1 and 999");
            return "";
        }
        if (status) *status = Status::Ok();
        return prefix + candidate;
    }

    Status Start(const PortConfig& config)
    {
        ScopedLock lock;
        ClosePortNoLock();
        CloseLog();
        if (config.logging) OpenLog();

        Status validation;
        const std::string path = NormalizePortName(config.port.c_str(), &validation);
        if (!validation.success) return validation;
        if (config.baudRate <= 0) return Status::Validation(ErrorInvalidParameter, "BaudRate must be positive");
        if (config.dataBits < 5 || config.dataBits > 8) return Status::Validation(ErrorInvalidParameter, "DataBits must be 5 through 8");
        if (config.stopBits != 1 && config.stopBits != 15 && config.stopBits != 2) return Status::Validation(ErrorInvalidParameter, "StopBits must be 1, 15, or 2");
        if (config.parity < 0 || config.parity > 4) return Status::Validation(ErrorInvalidParameter, "Parity must be 0 through 4");
        if (config.flowControl < 0 || config.flowControl > 3) return Status::Validation(ErrorInvalidParameter, "FlowControl must be 0 through 3");
        if (config.readTimeoutMs < 0 || config.writeTimeoutMs < 0) return Status::Validation(ErrorInvalidParameter, "Configured timeouts cannot be negative");

        g_port = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (g_port == INVALID_HANDLE_VALUE) return Status::Win32Error("CreateFile", GetLastError());

        DCB dcb = { 0 };
        dcb.DCBlength = sizeof(dcb);
        if (!GetCommState(g_port, &dcb))
        {
            Status result = Status::Win32Error("GetCommState", GetLastError());
            ClosePortNoLock(); return result;
        }
        dcb.BaudRate = static_cast<DWORD>(config.baudRate);
        dcb.ByteSize = static_cast<BYTE>(config.dataBits);
        const BYTE parityValues[] = { NOPARITY, ODDPARITY, EVENPARITY, MARKPARITY, SPACEPARITY };
        dcb.Parity = parityValues[config.parity];
        dcb.fParity = config.parity != 0;
        dcb.StopBits = config.stopBits == 1 ? ONESTOPBIT : (config.stopBits == 15 ? ONE5STOPBITS : TWOSTOPBITS);
        dcb.fBinary = TRUE;
        dcb.fOutxCtsFlow = FALSE; dcb.fOutxDsrFlow = FALSE;
        dcb.fInX = FALSE; dcb.fOutX = FALSE;
        dcb.fDtrControl = DTR_CONTROL_ENABLE;
        dcb.fRtsControl = RTS_CONTROL_ENABLE;
        if (config.flowControl == 1) { dcb.fInX = TRUE; dcb.fOutX = TRUE; }
        else if (config.flowControl == 2) { dcb.fOutxCtsFlow = TRUE; dcb.fRtsControl = RTS_CONTROL_HANDSHAKE; }
        else if (config.flowControl == 3) { dcb.fOutxDsrFlow = TRUE; dcb.fDtrControl = DTR_CONTROL_HANDSHAKE; }
        if (!SetCommState(g_port, &dcb))
        {
            Status result = Status::Win32Error("SetCommState", GetLastError());
            ClosePortNoLock(); return result;
        }

        COMMTIMEOUTS timeouts = { 0 };
        timeouts.ReadIntervalTimeout = MAXDWORD;
        timeouts.WriteTotalTimeoutConstant = static_cast<DWORD>(config.writeTimeoutMs);
        if (!SetCommTimeouts(g_port, &timeouts))
        {
            Status result = Status::Win32Error("SetCommTimeouts", GetLastError());
            ClosePortNoLock(); return result;
        }
        SetupComm(g_port, 65536, 65536);
        if (config.flushOnOpen && !PurgeComm(g_port, PURGE_RXCLEAR | PURGE_TXCLEAR))
        {
            Status result = Status::Win32Error("PurgeComm", GetLastError());
            ClosePortNoLock(); return result;
        }
        g_flowControl = config.flowControl;
        g_readTimeoutMs = static_cast<DWORD>(config.readTimeoutMs);
        g_writeTimeoutMs = static_cast<DWORD>(config.writeTimeoutMs);
        Log("serial port opened");
        return Status::Ok();
    }

    Status Stop()
    {
        ScopedLock lock;
        ClosePortNoLock();
        Log("serial port closed");
        CloseLog();
        return Status::Ok();
    }

    Status GetBufferLength(DWORD* bytesAvailable)
    {
        if (!bytesAvailable) return Status::Validation(ErrorInvalidParameter, "BytesAvailable output is required");
        ScopedLock lock;
        *bytesAvailable = 0;
        if (g_port == INVALID_HANDLE_VALUE) return Status::Validation(ErrorNotOpen, "Serial port is not open");
        COMSTAT stat = { 0 }; DWORD errors = 0;
        if (!ClearCommError(g_port, &errors, &stat)) return Status::Win32Error("ClearCommError", GetLastError());
        *bytesAvailable = stat.cbInQue;
        return Status::Ok();
    }

    Status ReadBytes(DWORD requestedCount, DWORD timeoutMs, DWORD capacity, std::vector<unsigned char>* data, bool* timedOut)
    {
        if (!data || !timedOut) return Status::Validation(ErrorInvalidParameter, "Read output is required");
        ScopedLock lock;
        data->clear(); *timedOut = false;
        if (g_port == INVALID_HANDLE_VALUE) return Status::Validation(ErrorNotOpen, "Serial port is not open");
        if (requestedCount > capacity) return Status::Validation(ErrorBufferTooSmall, "RequestedCount exceeds Data array capacity");
        if (requestedCount == 0) return ReadAvailableNoLock(capacity, data);

        const DWORD effectiveTimeout = ResolveTimeout(timeoutMs, g_readTimeoutMs);
        const DWORD start = GetTickCount();
        while (data->size() < requestedCount)
        {
            Status result = ReadAvailableNoLock(requestedCount - static_cast<DWORD>(data->size()), data);
            if (!result.success) return result;
            if (data->size() >= requestedCount) break;
            if (effectiveTimeout == 0 || HasElapsed(start, effectiveTimeout)) { *timedOut = true; break; }
            Sleep(1);
        }
        return Status::Ok();
    }

    Status ReadString(DWORD maxChars, const std::vector<unsigned char>& terminator, DWORD timeoutMs,
        bool includeTerminator, std::string* text, DWORD* bytesRead, bool* timedOut)
    {
        if (!text || !bytesRead || !timedOut) return Status::Validation(ErrorInvalidParameter, "String read output is required");
        ScopedLock lock;
        text->clear(); *bytesRead = 0; *timedOut = false;
        if (g_port == INVALID_HANDLE_VALUE) return Status::Validation(ErrorNotOpen, "Serial port is not open");
        if (maxChars == 0) return Status::Validation(ErrorInvalidParameter, "MaxChars must be positive");
        const DWORD effectiveTimeout = ResolveTimeout(timeoutMs, g_readTimeoutMs);
        const DWORD start = GetTickCount();
        std::vector<unsigned char> data;
        while (data.size() < maxChars)
        {
            const size_t before = data.size();
            Status result = ReadAvailableNoLock(maxChars - static_cast<DWORD>(data.size()), &data);
            if (!result.success) return result;
            if (!terminator.empty() && EndsWith(data, terminator)) break;
            if (terminator.empty() && !data.empty())
            {
                COMSTAT stat = { 0 }; DWORD errors = 0;
                if (!ClearCommError(g_port, &errors, &stat)) return Status::Win32Error("ClearCommError", GetLastError());
                if (stat.cbInQue == 0) break;
            }
            if (data.size() >= maxChars) break;
            if (effectiveTimeout == 0 || HasElapsed(start, effectiveTimeout)) { *timedOut = data.empty() || !terminator.empty(); break; }
            if (data.size() == before) Sleep(1);
        }
        *bytesRead = static_cast<DWORD>(data.size());
        if (std::find(data.begin(), data.end(), static_cast<unsigned char>(0)) != data.end())
            return Status::Validation(ErrorBinaryString, "Received NUL byte; use SRCSerial_readBytes for binary data");
        size_t textLength = data.size();
        if (!includeTerminator && EndsWith(data, terminator)) textLength -= terminator.size();
        if (textLength != 0)
            text->assign(reinterpret_cast<const char*>(&data[0]), textLength);
        return Status::Ok();
    }

    Status WriteBytes(const unsigned char* data, DWORD count, DWORD timeoutMs, DWORD* bytesWritten)
    {
        if (!bytesWritten || (count && !data)) return Status::Validation(ErrorInvalidParameter, "Write input/output is required");
        ScopedLock lock;
        *bytesWritten = 0;
        if (g_port == INVALID_HANDLE_VALUE) return Status::Validation(ErrorNotOpen, "Serial port is not open");
        COMMTIMEOUTS original = { 0 };
        if (!GetCommTimeouts(g_port, &original)) return Status::Win32Error("GetCommTimeouts", GetLastError());
        COMMTIMEOUTS current = original;
        current.WriteTotalTimeoutMultiplier = 0;
        current.WriteTotalTimeoutConstant = ResolveTimeout(timeoutMs, g_writeTimeoutMs);
        if (!SetCommTimeouts(g_port, &current)) return Status::Win32Error("SetCommTimeouts", GetLastError());
        DWORD total = 0;
        Status result = Status::Ok();
        while (total < count)
        {
            DWORD written = 0;
            if (!WriteFile(g_port, data + total, count - total, &written, NULL))
            {
                result = Status::Win32Error("WriteFile", GetLastError()); break;
            }
            total += written;
            if (written == 0) break;
        }
        SetCommTimeouts(g_port, &original);
        *bytesWritten = total;
        return result;
    }

    Status Flush(long mask)
    {
        ScopedLock lock;
        if (g_port == INVALID_HANDLE_VALUE) return Status::Validation(ErrorNotOpen, "Serial port is not open");
        if (mask < 1 || mask > 3) return Status::Validation(ErrorInvalidParameter, "FlushMask must be 1, 2, or 3");
        DWORD flags = 0;
        if (mask & 1) flags |= PURGE_RXABORT | PURGE_RXCLEAR;
        if (mask & 2) flags |= PURGE_TXABORT | PURGE_TXCLEAR;
        return PurgeComm(g_port, flags) ? Status::Ok() : Status::Win32Error("PurgeComm", GetLastError());
    }

    Status SetControlLines(long dtr, long rts, long breakState)
    {
        ScopedLock lock;
        if (g_port == INVALID_HANDLE_VALUE) return Status::Validation(ErrorNotOpen, "Serial port is not open");
        if ((dtr < -1 || dtr > 1) || (rts < -1 || rts > 1) || (breakState < -1 || breakState > 1))
            return Status::Validation(ErrorInvalidParameter, "DTR, RTS, and Break must be -1, 0, or 1");
        if (rts != -1 && g_flowControl == 2) return Status::Validation(ErrorHandshakeConflict, "RTS is controlled by RTS/CTS flow control");
        if (dtr != -1 && g_flowControl == 3) return Status::Validation(ErrorHandshakeConflict, "DTR is controlled by DTR/DSR flow control");
        if (dtr != -1 && !EscapeCommFunction(g_port, dtr ? SETDTR : CLRDTR)) return Status::Win32Error("EscapeCommFunction(DTR)", GetLastError());
        if (rts != -1 && !EscapeCommFunction(g_port, rts ? SETRTS : CLRRTS)) return Status::Win32Error("EscapeCommFunction(RTS)", GetLastError());
        if (breakState != -1 && !EscapeCommFunction(g_port, breakState ? SETBREAK : CLRBREAK)) return Status::Win32Error("EscapeCommFunction(BREAK)", GetLastError());
        return Status::Ok();
    }

    Status GetLineStatus(long* cts, long* dsr, long* dcd, long* ring)
    {
        if (!cts || !dsr || !dcd || !ring) return Status::Validation(ErrorInvalidParameter, "Line status outputs are required");
        ScopedLock lock;
        *cts = *dsr = *dcd = *ring = 0;
        if (g_port == INVALID_HANDLE_VALUE) return Status::Validation(ErrorNotOpen, "Serial port is not open");
        DWORD status = 0;
        if (!GetCommModemStatus(g_port, &status)) return Status::Win32Error("GetCommModemStatus", GetLastError());
        *cts = (status & MS_CTS_ON) ? 1 : 0; *dsr = (status & MS_DSR_ON) ? 1 : 0;
        *dcd = (status & MS_RLSD_ON) ? 1 : 0; *ring = (status & MS_RING_ON) ? 1 : 0;
        return Status::Ok();
    }

    Status DecodeEscapes(const char* text, std::vector<unsigned char>* bytes)
    {
        if (!bytes) return Status::Validation(ErrorInvalidParameter, "Escape output is required");
        bytes->clear();
        if (!text) return Status::Ok();
        for (size_t i = 0; text[i]; ++i)
        {
            unsigned char value = static_cast<unsigned char>(text[i]);
            if (value != '\\') { bytes->push_back(value); continue; }
            const char next = text[++i];
            if (!next) return Status::Validation(ErrorInvalidParameter, "Trailing backslash in escaped text");
            if (next == 'r') bytes->push_back('\r');
            else if (next == 'n') bytes->push_back('\n');
            else if (next == 't') bytes->push_back('\t');
            else if (next == '\\') bytes->push_back('\\');
            else if (next == 'x')
            {
                const char a = text[++i], b = text[++i];
                if (!a || !b || !std::isxdigit(static_cast<unsigned char>(a)) || !std::isxdigit(static_cast<unsigned char>(b)))
                    return Status::Validation(ErrorInvalidParameter, "\\x escape must contain exactly two hexadecimal digits");
                const char hex[3] = { a, b, 0 };
                bytes->push_back(static_cast<unsigned char>(std::strtoul(hex, NULL, 16)));
            }
            else return Status::Validation(ErrorInvalidParameter, "Unsupported escape sequence");
        }
        return Status::Ok();
    }
}
