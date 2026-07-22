#pragma once

#include <windows.h>
#include <stddef.h>

#include <string>
#include <vector>

#include "../SRCSerial.h"

namespace srcserial
{
    enum ErrorCode
    {
        ErrorInvalidParameter = -1001,
        ErrorNotOpen = -1002,
        ErrorBufferTooSmall = -1003,
        ErrorBinaryString = -1004,
        ErrorHandshakeConflict = -1005,
        ErrorInternal = -1099
    };

    struct Status
    {
        bool success;
        long code;
        std::string message;

        Status();
        static Status Ok();
        static Status Validation(long code, const char* message);
        static Status Win32Error(const char* operation, DWORD code);
    };

    struct PortConfig
    {
        std::string port;
        long baudRate;
        long dataBits;
        long stopBits;
        long parity;
        long flowControl;
        long readTimeoutMs;
        long writeTimeoutMs;
        bool flushOnOpen;
        bool logging;

        PortConfig();
    };

    void Initialize(HMODULE module);
    void Shutdown();
    Status Start(const PortConfig& config);
    Status Stop();
    Status GetBufferLength(DWORD* bytesAvailable);
    Status ReadBytes(DWORD requestedCount, DWORD timeoutMs, DWORD capacity, std::vector<unsigned char>* data, bool* timedOut);
    Status ReadString(DWORD maxChars, const std::vector<unsigned char>& terminator, DWORD timeoutMs, bool includeTerminator, std::string* text, DWORD* bytesRead, bool* timedOut);
    Status WriteBytes(const unsigned char* data, DWORD count, DWORD timeoutMs, DWORD* bytesWritten);
    Status Flush(long mask);
    Status SetControlLines(long dtr, long rts, long breakState);
    Status GetLineStatus(long* cts, long* dsr, long* dcd, long* ring);
    Status DecodeEscapes(const char* text, std::vector<unsigned char>* bytes);
    std::string NormalizePortName(const char* port, Status* status);

    bool HasParameter(HUTAPB block, const char* name);
    long GetInt32(HUTAPB block, const char* name, long defaultValue);
    std::string GetString(HUTAPB block, const char* name, const char* defaultValue);
    HUTAI32ARR GetInt32Array(HUTAPB block, const char* name);
    DWORD GetArrayCapacity(HUTAI32ARR array);
    void SetInt32(HUTAPB block, const char* name, long value);
    void SetString(HUTAPB block, const char* name, const char* value);
    void SetStatus(HUTAPB block, const Status& status);
    void ClearStatus(HUTAPB block);
}
