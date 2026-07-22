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
        config.readTimeoutMs = srcserial::GetInt32(block, "ReadTimeoutMs", 1000);
        config.writeTimeoutMs = srcserial::GetInt32(block, "WriteTimeoutMs", 1000);
        config.flushOnOpen = srcserial::GetInt32(block, "FlushOnOpen", 1) != 0;
        config.logging = srcserial::GetInt32(block, "Logging", 0) != 0;
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
