#include "SRCSerial_Internal.h"

#include <cstring>

namespace
{
    srcserial::Status UnexpectedFailure()
    {
        return srcserial::Status::Validation(srcserial::ErrorInternal, "Unexpected internal exception");
    }

    DWORD ActionTimeout(HUTAPB block)
    {
        const long value = srcserial::GetInt32(block, "TimeoutMs", -1);
        return value < 0 ? MAXDWORD : static_cast<DWORD>(value);
    }

    srcserial::Status ReadByteArray(HUTAPB block, const char* arrayName,
        const char* countName, std::vector<unsigned char>* bytes)
    {
        HUTAI32ARR array = srcserial::GetInt32Array(block, arrayName);
        UTAINT32* buffer = array ? UtaI32ArrGetBuffer(array) : NULL;
        const DWORD capacity = srcserial::GetArrayCapacity(array);
        const long count = srcserial::GetInt32(block, countName, 0);
        if (!array || !buffer || count < 0 || static_cast<DWORD>(count) > capacity)
            return srcserial::Status::Validation(srcserial::ErrorInvalidParameter,
                "Byte array and count within its capacity are required");
        bytes->resize(static_cast<size_t>(count));
        for (long i = 0; i < count; ++i)
        {
            if (buffer[i] < 0 || buffer[i] > 255)
                return srcserial::Status::Validation(srcserial::ErrorInvalidParameter,
                    "Byte array values must be between 0 and 255");
            (*bytes)[static_cast<size_t>(i)] = static_cast<unsigned char>(buffer[i]);
        }
        return srcserial::Status::Ok();
    }

    srcserial::Status WriteByteArray(HUTAPB block, const char* name,
        const std::vector<unsigned char>& bytes)
    {
        HUTAI32ARR array = srcserial::GetInt32Array(block, name);
        UTAINT32* buffer = array ? UtaI32ArrGetBuffer(array) : NULL;
        const DWORD capacity = srcserial::GetArrayCapacity(array);
        if (!array || !buffer)
            return srcserial::Status::Validation(srcserial::ErrorInvalidParameter,
                "Output byte array is required");
        if (bytes.size() > capacity)
            return srcserial::Status::Validation(srcserial::ErrorBufferTooSmall,
                "Output byte array is too small");
        for (DWORD i = 0; i < capacity; ++i) buffer[i] = 0;
        for (size_t i = 0; i < bytes.size(); ++i)
            buffer[i] = static_cast<UTAINT32>(bytes[i]);
        return srcserial::Status::Ok();
    }

    void SetMoxaModeOutputs(HUTAPB block, const srcserial::MoxaPortMode& mode,
        bool includeChangeOutputs)
    {
        srcserial::SetInt32(block, "Found", mode.found ? 1 : 0);
        srcserial::SetInt32(block, "InterfaceMode", mode.interfaceMode);
        srcserial::SetInt32(block, "PreviousMode", mode.previousInterfaceMode);
        srcserial::SetInt32(block, "CurrentMode", mode.interfaceMode);
        srcserial::SetInt32(block, "TxMode", mode.txMode);
        srcserial::SetString(block, "InstanceId", mode.instanceId.c_str());
        srcserial::SetString(block, "DriverVersion", mode.driverVersion.c_str());
        if (includeChangeOutputs)
        {
            srcserial::SetInt32(block, "RegistryUpdated", mode.registryUpdated ? 1 : 0);
            srcserial::SetInt32(block, "RestartAttempted", mode.restartAttempted ? 1 : 0);
            srcserial::SetInt32(block, "RestartSucceeded", mode.restartSucceeded ? 1 : 0);
            srcserial::SetInt32(block, "RestartRequired", mode.restartRequired ? 1 : 0);
        }
    }

}

extern "C" void UTAAPI SRCSerial_start(HUTAPB block)
{
    srcserial::ClearStatus(block);
    try
    {
        srcserial::PortConfig config;
        config.port = srcserial::GetString(block, "Port", "COM1");
        config.baudRate = srcserial::GetInt32(block, "BaudRate", 9600);
        config.dataBits = srcserial::GetInt32(block, "DataBits", 8);
        config.stopBits = srcserial::GetInt32(block, "StopBits", 1);
        config.parity = srcserial::GetInt32(block, "Parity", 0);
        config.flowControl = srcserial::GetInt32(block, "FlowControl", 0);
        config.dtrMode = srcserial::GetInt32(block, "DTRMode", -1);
        config.rtsMode = srcserial::GetInt32(block, "RTSMode", -1);
        config.readTimeoutMs = srcserial::GetInt32(block, "ReadTimeoutMs", 1000);
        config.writeTimeoutMs = srcserial::GetInt32(block, "WriteTimeoutMs", 1000);
        config.flushOnOpen = srcserial::GetInt32(block, "FlushOnOpen", 1) != 0;
        config.logging = srcserial::GetInt32(block, "Logging", 0);
        srcserial::SetStatus(block, srcserial::Start(config));
    }
    catch (...) { srcserial::SetStatus(block, UnexpectedFailure()); }
}

extern "C" void UTAAPI SRCSerial_stop(HUTAPB block)
{
    srcserial::ClearStatus(block);
    try { srcserial::SetStatus(block, srcserial::Stop()); }
    catch (...) { srcserial::SetStatus(block, UnexpectedFailure()); }
}

extern "C" void UTAAPI SRCSerial_getBufferLength(HUTAPB block)
{
    srcserial::ClearStatus(block);
    srcserial::SetInt32(block, "BytesAvailable", 0);
    try
    {
        DWORD available = 0;
        const srcserial::Status status = srcserial::GetBufferLength(&available);
        srcserial::SetInt32(block, "BytesAvailable", static_cast<long>(available));
        srcserial::SetStatus(block, status);
    }
    catch (...) { srcserial::SetStatus(block, UnexpectedFailure()); }
}

extern "C" void UTAAPI SRCSerial_readBytes(HUTAPB block)
{
    srcserial::ClearStatus(block);
    srcserial::SetInt32(block, "BytesRead", 0);
    srcserial::SetInt32(block, "TimedOut", 0);
    try
    {
        HUTAI32ARR array = srcserial::GetInt32Array(block, "Data");
        const DWORD capacity = srcserial::GetArrayCapacity(array);
        UTAINT32* buffer = array ? UtaI32ArrGetBuffer(array) : NULL;
        if (!array || !buffer)
        {
            srcserial::SetStatus(block, srcserial::Status::Validation(srcserial::ErrorInvalidParameter, "Data Int32 array is required"));
            return;
        }
        for (DWORD i = 0; i < capacity; ++i) buffer[i] = 0;
        const long requestedValue = srcserial::GetInt32(block, "RequestedCount", 0);
        if (requestedValue < 0)
        {
            srcserial::SetStatus(block, srcserial::Status::Validation(srcserial::ErrorInvalidParameter, "RequestedCount cannot be negative"));
            return;
        }
        std::vector<unsigned char> data;
        bool timedOut = false;
        const srcserial::Status status = srcserial::ReadBytes(static_cast<DWORD>(requestedValue), ActionTimeout(block), capacity, &data, &timedOut);
        for (size_t i = 0; i < data.size(); ++i) buffer[i] = static_cast<UTAINT32>(data[i]);
        srcserial::SetInt32(block, "BytesRead", static_cast<long>(data.size()));
        srcserial::SetInt32(block, "TimedOut", timedOut ? 1 : 0);
        srcserial::SetStatus(block, status);
    }
    catch (...) { srcserial::SetStatus(block, UnexpectedFailure()); }
}

extern "C" void UTAAPI SRCSerial_readString(HUTAPB block)
{
    srcserial::ClearStatus(block);
    srcserial::SetString(block, "Text", "");
    srcserial::SetInt32(block, "BytesRead", 0);
    srcserial::SetInt32(block, "TimedOut", 0);
    try
    {
        const long maxValue = srcserial::GetInt32(block, "MaxChars", 1024);
        if (maxValue <= 0)
        {
            srcserial::SetStatus(block, srcserial::Status::Validation(srcserial::ErrorInvalidParameter, "MaxChars must be positive"));
            return;
        }
        const std::string terminatorText = srcserial::GetString(block, "Terminator", "");
        std::vector<unsigned char> terminator;
        srcserial::Status status = srcserial::DecodeEscapes(terminatorText.c_str(), &terminator);
        if (!status.success) { srcserial::SetStatus(block, status); return; }
        std::string text;
        DWORD bytesRead = 0;
        bool timedOut = false;
        status = srcserial::ReadString(static_cast<DWORD>(maxValue), terminator, ActionTimeout(block),
            srcserial::GetInt32(block, "IncludeTerminator", 0) != 0, &text, &bytesRead, &timedOut);
        if (status.success) srcserial::SetString(block, "Text", text.c_str());
        srcserial::SetInt32(block, "BytesRead", static_cast<long>(bytesRead));
        srcserial::SetInt32(block, "TimedOut", timedOut ? 1 : 0);
        srcserial::SetStatus(block, status);
    }
    catch (...) { srcserial::SetStatus(block, UnexpectedFailure()); }
}

extern "C" void UTAAPI SRCSerial_writeBytes(HUTAPB block)
{
    srcserial::ClearStatus(block);
    srcserial::SetInt32(block, "BytesWritten", 0);
    try
    {
        HUTAI32ARR array = srcserial::GetInt32Array(block, "Data");
        UTAINT32* buffer = array ? UtaI32ArrGetBuffer(array) : NULL;
        const DWORD capacity = srcserial::GetArrayCapacity(array);
        const long countValue = srcserial::GetInt32(block, "Count", 0);
        if (!array || !buffer || countValue < 0 || static_cast<DWORD>(countValue) > capacity)
        {
            srcserial::SetStatus(block, srcserial::Status::Validation(srcserial::ErrorInvalidParameter, "Data array and Count within its capacity are required"));
            return;
        }
        std::vector<unsigned char> data(static_cast<size_t>(countValue));
        for (long i = 0; i < countValue; ++i)
        {
            if (buffer[i] < 0 || buffer[i] > 255)
            {
                srcserial::SetStatus(block, srcserial::Status::Validation(srcserial::ErrorInvalidParameter, "Data values must be between 0 and 255"));
                return;
            }
            data[static_cast<size_t>(i)] = static_cast<unsigned char>(buffer[i]);
        }
        DWORD written = 0;
        const srcserial::Status status = srcserial::WriteBytes(data.empty() ? NULL : &data[0], static_cast<DWORD>(data.size()), ActionTimeout(block), &written);
        srcserial::SetInt32(block, "BytesWritten", static_cast<long>(written));
        srcserial::SetStatus(block, status);
    }
    catch (...) { srcserial::SetStatus(block, UnexpectedFailure()); }
}

extern "C" void UTAAPI SRCSerial_writeString(HUTAPB block)
{
    srcserial::ClearStatus(block);
    srcserial::SetInt32(block, "BytesWritten", 0);
    try
    {
        const std::string text = srcserial::GetString(block, "Text", "");
        const std::string suffixText = srcserial::GetString(block, "Suffix", "");
        std::vector<unsigned char> suffix;
        srcserial::Status status = srcserial::DecodeEscapes(suffixText.c_str(), &suffix);
        if (!status.success) { srcserial::SetStatus(block, status); return; }
        std::vector<unsigned char> data(text.begin(), text.end());
        data.insert(data.end(), suffix.begin(), suffix.end());
        DWORD written = 0;
        status = srcserial::WriteBytes(data.empty() ? NULL : &data[0], static_cast<DWORD>(data.size()), ActionTimeout(block), &written);
        srcserial::SetInt32(block, "BytesWritten", static_cast<long>(written));
        srcserial::SetStatus(block, status);
    }
    catch (...) { srcserial::SetStatus(block, UnexpectedFailure()); }
}

extern "C" void UTAAPI SRCSerial_flush(HUTAPB block)
{
    srcserial::ClearStatus(block);
    try { srcserial::SetStatus(block, srcserial::Flush(srcserial::GetInt32(block, "FlushMask", 3))); }
    catch (...) { srcserial::SetStatus(block, UnexpectedFailure()); }
}

extern "C" void UTAAPI SRCSerial_setControlLines(HUTAPB block)
{
    srcserial::ClearStatus(block);
    try
    {
        srcserial::SetStatus(block, srcserial::SetControlLines(
            srcserial::GetInt32(block, "DTR", -1), srcserial::GetInt32(block, "RTS", -1),
            srcserial::GetInt32(block, "Break", -1)));
    }
    catch (...) { srcserial::SetStatus(block, UnexpectedFailure()); }
}

extern "C" void UTAAPI SRCSerial_getLineStatus(HUTAPB block)
{
    srcserial::ClearStatus(block);
    srcserial::SetInt32(block, "CTS", 0); srcserial::SetInt32(block, "DSR", 0);
    srcserial::SetInt32(block, "DCD", 0); srcserial::SetInt32(block, "Ring", 0);
    try
    {
        long cts = 0, dsr = 0, dcd = 0, ring = 0;
        const srcserial::Status status = srcserial::GetLineStatus(&cts, &dsr, &dcd, &ring);
        srcserial::SetInt32(block, "CTS", cts); srcserial::SetInt32(block, "DSR", dsr);
        srcserial::SetInt32(block, "DCD", dcd); srcserial::SetInt32(block, "Ring", ring);
        srcserial::SetStatus(block, status);
    }
    catch (...) { srcserial::SetStatus(block, UnexpectedFailure()); }
}

extern "C" void UTAAPI SRCSerial_isOpen(HUTAPB block)
{
    srcserial::ClearStatus(block);
    srcserial::SetInt32(block, "Open", 0);
    srcserial::SetString(block, "Port", "");
    try
    {
        bool open = false;
        std::string port;
        const srcserial::Status status = srcserial::IsOpen(&open, &port);
        srcserial::SetInt32(block, "Open", open ? 1 : 0);
        srcserial::SetString(block, "Port", port.c_str());
        srcserial::SetStatus(block, status);
    }
    catch (...) { srcserial::SetStatus(block, UnexpectedFailure()); }
}

extern "C" void UTAAPI SRCSerial_getConfiguration(HUTAPB block)
{
    srcserial::ClearStatus(block);
    srcserial::SetInt32(block, "Open", 0);
    srcserial::SetString(block, "Port", "");
    try
    {
        srcserial::PortConfig config;
        bool open = false;
        const srcserial::Status status = srcserial::GetConfiguration(&config, &open);
        srcserial::SetInt32(block, "Open", open ? 1 : 0);
        srcserial::SetString(block, "Port", config.port.c_str());
        srcserial::SetInt32(block, "BaudRate", config.baudRate);
        srcserial::SetInt32(block, "DataBits", config.dataBits);
        srcserial::SetInt32(block, "StopBits", config.stopBits);
        srcserial::SetInt32(block, "Parity", config.parity);
        srcserial::SetInt32(block, "FlowControl", config.flowControl);
        srcserial::SetInt32(block, "DTRMode", config.dtrMode);
        srcserial::SetInt32(block, "RTSMode", config.rtsMode);
        srcserial::SetInt32(block, "ReadTimeoutMs", config.readTimeoutMs);
        srcserial::SetInt32(block, "WriteTimeoutMs", config.writeTimeoutMs);
        srcserial::SetInt32(block, "Logging", config.logging);
        srcserial::SetStatus(block, status);
    }
    catch (...) { srcserial::SetStatus(block, UnexpectedFailure()); }
}

extern "C" void UTAAPI SRCSerial_getDiagnostics(HUTAPB block)
{
    srcserial::ClearStatus(block);
    const char* outputs[] = { "FrameErrors", "ParityErrors", "OverrunErrors",
        "BufferOverrunErrors", "BreakCount", "RxBytesQueued", "TxBytesQueued",
        "TotalRxBytes", "TotalTxBytes", "LastWin32Error" };
    for (size_t i = 0; i < sizeof(outputs) / sizeof(outputs[0]); ++i)
        srcserial::SetInt32(block, outputs[i], 0);
    try
    {
        srcserial::Diagnostics value;
        const srcserial::Status status = srcserial::GetDiagnostics(&value,
            srcserial::GetInt32(block, "ResetAfterRead", 0) != 0);
        srcserial::SetInt32(block, "FrameErrors", static_cast<long>(value.frameErrors));
        srcserial::SetInt32(block, "ParityErrors", static_cast<long>(value.parityErrors));
        srcserial::SetInt32(block, "OverrunErrors", static_cast<long>(value.overrunErrors));
        srcserial::SetInt32(block, "BufferOverrunErrors", static_cast<long>(value.bufferOverrunErrors));
        srcserial::SetInt32(block, "BreakCount", static_cast<long>(value.breakCount));
        srcserial::SetInt32(block, "RxBytesQueued", static_cast<long>(value.rxBytesQueued));
        srcserial::SetInt32(block, "TxBytesQueued", static_cast<long>(value.txBytesQueued));
        srcserial::SetInt32(block, "TotalRxBytes", static_cast<long>(value.totalRxBytes));
        srcserial::SetInt32(block, "TotalTxBytes", static_cast<long>(value.totalTxBytes));
        srcserial::SetInt32(block, "LastWin32Error", static_cast<long>(value.lastWin32Error));
        srcserial::SetStatus(block, status);
    }
    catch (...) { srcserial::SetStatus(block, UnexpectedFailure()); }
}

extern "C" void UTAAPI SRCSerial_readUntilIdle(HUTAPB block)
{
    srcserial::ClearStatus(block);
    srcserial::SetInt32(block, "BytesRead", 0);
    srcserial::SetInt32(block, "TimedOut", 0);
    srcserial::SetString(block, "Hex", "");
    try
    {
        HUTAI32ARR array = srcserial::GetInt32Array(block, "Data");
        const DWORD capacity = srcserial::GetArrayCapacity(array);
        const long maximum = srcserial::GetInt32(block, "MaxBytes", static_cast<long>(capacity));
        const long idle = srcserial::GetInt32(block, "InterByteTimeoutMs", 20);
        if (maximum <= 0 || static_cast<DWORD>(maximum) > capacity || idle <= 0)
        {
            srcserial::SetStatus(block, srcserial::Status::Validation(
                srcserial::ErrorInvalidParameter, "MaxBytes must fit Data and InterByteTimeoutMs must be positive"));
            return;
        }
        std::vector<unsigned char> data;
        bool timedOut = false;
        srcserial::Status status = srcserial::ReadUntilIdle(
            static_cast<DWORD>(maximum), ActionTimeout(block), static_cast<DWORD>(idle),
            &data, &timedOut);
        if (status.success) status = WriteByteArray(block, "Data", data);
        srcserial::SetInt32(block, "BytesRead", static_cast<long>(data.size()));
        srcserial::SetInt32(block, "TimedOut", timedOut ? 1 : 0);
        srcserial::SetString(block, "Hex", srcserial::FormatHex(
            data.empty() ? NULL : &data[0], data.size()).c_str());
        srcserial::SetStatus(block, status);
    }
    catch (...) { srcserial::SetStatus(block, UnexpectedFailure()); }
}

extern "C" void UTAAPI SRCSerial_writeHex(HUTAPB block)
{
    srcserial::ClearStatus(block);
    srcserial::SetInt32(block, "BytesWritten", 0);
    try
    {
        const std::string text = srcserial::GetString(block, "Hex", "");
        std::vector<unsigned char> data;
        srcserial::Status status = srcserial::DecodeHex(text.c_str(), &data);
        DWORD written = 0;
        if (status.success)
            status = srcserial::WriteBytes(data.empty() ? NULL : &data[0],
                static_cast<DWORD>(data.size()), ActionTimeout(block), &written);
        srcserial::SetInt32(block, "BytesWritten", static_cast<long>(written));
        srcserial::SetStatus(block, status);
    }
    catch (...) { srcserial::SetStatus(block, UnexpectedFailure()); }
}

extern "C" void UTAAPI SRCSerial_readHex(HUTAPB block)
{
    srcserial::ClearStatus(block);
    srcserial::SetString(block, "Hex", "");
    srcserial::SetInt32(block, "BytesRead", 0);
    srcserial::SetInt32(block, "TimedOut", 0);
    try
    {
        const long requested = srcserial::GetInt32(block, "RequestedCount", 0);
        const long maximum = srcserial::GetInt32(block, "MaxBytes", 1024);
        if (requested < 0 || maximum <= 0 || requested > maximum)
        {
            srcserial::SetStatus(block, srcserial::Status::Validation(
                srcserial::ErrorInvalidParameter, "RequestedCount must be 0..MaxBytes and MaxBytes must be positive"));
            return;
        }
        std::vector<unsigned char> data;
        bool timedOut = false;
        const srcserial::Status status = srcserial::ReadBytes(
            static_cast<DWORD>(requested), ActionTimeout(block),
            static_cast<DWORD>(maximum), &data, &timedOut);
        const std::string hex = srcserial::FormatHex(
            data.empty() ? NULL : &data[0], data.size());
        srcserial::SetString(block, "Hex", hex.c_str());
        srcserial::SetInt32(block, "BytesRead", static_cast<long>(data.size()));
        srcserial::SetInt32(block, "TimedOut", timedOut ? 1 : 0);
        srcserial::SetStatus(block, status);
    }
    catch (...) { srcserial::SetStatus(block, UnexpectedFailure()); }
}

extern "C" void UTAAPI SRCSerial_transact(HUTAPB block)
{
    srcserial::ClearStatus(block);
    srcserial::SetInt32(block, "BytesWritten", 0);
    srcserial::SetInt32(block, "BytesRead", 0);
    srcserial::SetInt32(block, "TimedOut", 0);
    srcserial::SetInt32(block, "Attempts", 0);
    srcserial::SetString(block, "ResponseHex", "");
    try
    {
        std::vector<unsigned char> request;
        const long format = srcserial::GetInt32(block, "RequestFormat", 0);
        srcserial::Status status;
        if (format == 0)
        {
            const std::string text = srcserial::GetString(block, "RequestText", "");
            const std::string suffixText = srcserial::GetString(block, "RequestSuffix", "");
            std::vector<unsigned char> suffix;
            status = srcserial::DecodeEscapes(suffixText.c_str(), &suffix);
            request.assign(text.begin(), text.end());
            request.insert(request.end(), suffix.begin(), suffix.end());
        }
        else if (format == 1)
        {
            const std::string hex = srcserial::GetString(block, "RequestHex", "");
            status = srcserial::DecodeHex(hex.c_str(), &request);
        }
        else if (format == 2)
            status = ReadByteArray(block, "RequestData", "RequestCount", &request);
        else
            status = srcserial::Status::Validation(srcserial::ErrorInvalidParameter,
                "RequestFormat must be 0, 1, or 2");
        if (!status.success) { srcserial::SetStatus(block, status); return; }

        srcserial::TransactionConfig config;
        config.flushBeforeWrite = srcserial::GetInt32(block, "FlushBeforeWrite", 1) != 0;
        config.responseMode = srcserial::GetInt32(block, "ResponseMode", 0);
        const long responseCount = srcserial::GetInt32(block, "ResponseCount", 0);
        if (responseCount < 0)
        {
            srcserial::SetStatus(block, srcserial::Status::Validation(
                srcserial::ErrorInvalidParameter, "ResponseCount cannot be negative"));
            return;
        }
        config.responseCount = static_cast<DWORD>(responseCount);
        const std::string terminatorText = srcserial::GetString(block, "Terminator", "");
        status = srcserial::DecodeEscapes(terminatorText.c_str(), &config.terminator);
        if (!status.success) { srcserial::SetStatus(block, status); return; }
        config.overallTimeoutMs = ActionTimeout(block);
        const long idle = srcserial::GetInt32(block, "InterByteTimeoutMs", 20);
        const long pre = srcserial::GetInt32(block, "PreTransmitDelayMs", 0);
        const long post = srcserial::GetInt32(block, "PostTransmitDelayMs", 0);
        if (idle <= 0 || pre < 0 || post < 0)
        {
            srcserial::SetStatus(block, srcserial::Status::Validation(
                srcserial::ErrorInvalidParameter, "Transaction time values are invalid"));
            return;
        }
        config.interByteTimeoutMs = static_cast<DWORD>(idle);
        config.preTransmitDelayMs = static_cast<DWORD>(pre);
        config.postTransmitDelayMs = static_cast<DWORD>(post);
        config.retries = srcserial::GetInt32(block, "Retries", 0);

        HUTAI32ARR responseArray = srcserial::GetInt32Array(block, "ResponseData");
        const DWORD capacity = srcserial::GetArrayCapacity(responseArray);
        std::vector<unsigned char> response;
        DWORD written = 0;
        bool timedOut = false;
        long attempts = 0;
        status = srcserial::Transaction(request.empty() ? NULL : &request[0],
            static_cast<DWORD>(request.size()), capacity, config, &response,
            &written, &timedOut, &attempts);
        if (status.success) status = WriteByteArray(block, "ResponseData", response);
        srcserial::SetInt32(block, "BytesWritten", static_cast<long>(written));
        srcserial::SetInt32(block, "BytesRead", static_cast<long>(response.size()));
        srcserial::SetInt32(block, "TimedOut", timedOut ? 1 : 0);
        srcserial::SetInt32(block, "Attempts", attempts);
        srcserial::SetString(block, "ResponseHex", srcserial::FormatHex(
            response.empty() ? NULL : &response[0], response.size()).c_str());
        srcserial::SetStatus(block, status);
    }
    catch (...) { srcserial::SetStatus(block, UnexpectedFailure()); }
}

extern "C" void UTAAPI SRCSerial_pulseControlLine(HUTAPB block)
{
    srcserial::ClearStatus(block);
    try
    {
        const long duration = srcserial::GetInt32(block, "DurationMs", 100);
        if (duration < 0)
        {
            srcserial::SetStatus(block, srcserial::Status::Validation(
                srcserial::ErrorInvalidParameter, "DurationMs cannot be negative"));
            return;
        }
        srcserial::SetStatus(block, srcserial::PulseControlLine(
            srcserial::GetInt32(block, "Line", 0),
            srcserial::GetInt32(block, "State", 0),
            static_cast<DWORD>(duration),
            srcserial::GetInt32(block, "RestoreState", -1)));
    }
    catch (...) { srcserial::SetStatus(block, UnexpectedFailure()); }
}

extern "C" void UTAAPI SRCSerial_drainTransmit(HUTAPB block)
{
    srcserial::ClearStatus(block);
    srcserial::SetInt32(block, "TimedOut", 0);
    try
    {
        DWORD timeout = ActionTimeout(block);
        if (timeout == MAXDWORD)
        {
            srcserial::PortConfig config;
            bool open = false;
            srcserial::GetConfiguration(&config, &open);
            timeout = static_cast<DWORD>(config.writeTimeoutMs);
        }
        bool timedOut = false;
        const srcserial::Status status = srcserial::DrainTransmit(timeout, &timedOut);
        srcserial::SetInt32(block, "TimedOut", timedOut ? 1 : 0);
        srcserial::SetStatus(block, status);
    }
    catch (...) { srcserial::SetStatus(block, UnexpectedFailure()); }
}

extern "C" void UTAAPI SRCSerial_enumeratePorts(HUTAPB block)
{
    srcserial::ClearStatus(block);
    srcserial::SetString(block, "Ports", "");
    srcserial::SetInt32(block, "Count", 0);
    try
    {
        std::string ports;
        DWORD count = 0;
        const srcserial::Status status = srcserial::EnumeratePorts(&ports, &count);
        srcserial::SetString(block, "Ports", ports.c_str());
        srcserial::SetInt32(block, "Count", static_cast<long>(count));
        srcserial::SetStatus(block, status);
    }
    catch (...) { srcserial::SetStatus(block, UnexpectedFailure()); }
}

extern "C" void UTAAPI SRCSerial_cancel(HUTAPB block)
{
    srcserial::ClearStatus(block);
    try { srcserial::SetStatus(block, srcserial::CancelPending()); }
    catch (...) { srcserial::SetStatus(block, UnexpectedFailure()); }
}

extern "C" void UTAAPI SRCSerial_getMoxaPortMode(HUTAPB block)
{
    srcserial::ClearStatus(block);
    srcserial::MoxaPortMode mode;
    SetMoxaModeOutputs(block, mode, false);
    try
    {
        const std::string port = srcserial::GetString(block, "Port", "COM1");
        const srcserial::Status status = srcserial::GetMoxaPortMode(
            port.c_str(), &mode);
        SetMoxaModeOutputs(block, mode, false);
        srcserial::SetStatus(block, status);
    }
    catch (...) { srcserial::SetStatus(block, UnexpectedFailure()); }
}

extern "C" void UTAAPI SRCSerial_setMoxaPortMode(HUTAPB block)
{
    srcserial::ClearStatus(block);
    srcserial::MoxaPortMode mode;
    SetMoxaModeOutputs(block, mode, true);
    try
    {
        const std::string port = srcserial::GetString(block, "Port", "COM1");
        const std::string expected = srcserial::GetString(block,
            "ExpectedDriverVersion", "4.3.0.0");
        const srcserial::Status status = srcserial::SetMoxaPortMode(
            port.c_str(), srcserial::GetInt32(block, "InterfaceMode", 0),
            expected.c_str(),
            srcserial::GetInt32(block, "AllowUnverifiedDriver", 0) != 0,
            srcserial::GetInt32(block, "RestartDevice", 0) != 0,
            &mode);
        SetMoxaModeOutputs(block, mode, true);
        srcserial::SetStatus(block, status);
    }
    catch (...) { srcserial::SetStatus(block, UnexpectedFailure()); }
}
