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
        ErrorInvalidHex = -1006,
        ErrorOutputTooLarge = -1007,
        ErrorMoxaPortNotFound = -1008,
        ErrorMoxaDriverMismatch = -1009,
        ErrorPortInUse = -1010,
        ErrorMoxaRegistry = -1011,
        ErrorWorkerActive = -1012,
        ErrorWorkerNotRunning = -1013,
        ErrorWorkerJobNotFound = -1014,
        ErrorWorkerDuplicateJob = -1015,
        ErrorWorkerQueueFull = -1016,
        ErrorWorkerChecksum = -1017,
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
        long dtrMode;
        long rtsMode;
        long readTimeoutMs;
        long writeTimeoutMs;
        bool flushOnOpen;
        long logging;

        PortConfig();
    };

    struct Diagnostics
    {
        DWORD frameErrors;
        DWORD parityErrors;
        DWORD overrunErrors;
        DWORD bufferOverrunErrors;
        DWORD breakCount;
        DWORD rxBytesQueued;
        DWORD txBytesQueued;
        DWORD totalRxBytes;
        DWORD totalTxBytes;
        DWORD lastWin32Error;

        Diagnostics();
    };

    struct TransactionConfig
    {
        bool flushBeforeWrite;
        long responseMode;
        DWORD responseCount;
        std::vector<unsigned char> terminator;
        DWORD overallTimeoutMs;
        DWORD interByteTimeoutMs;
        DWORD preTransmitDelayMs;
        DWORD postTransmitDelayMs;
        long retries;

        TransactionConfig();
    };

    struct MoxaPortMode
    {
        bool found;
        long previousInterfaceMode;
        long interfaceMode;
        long txMode;
        std::string instanceId;
        std::string driverVersion;
        bool registryUpdated;
        bool restartAttempted;
        bool restartSucceeded;
        bool restartRequired;

        MoxaPortMode();
    };

    struct WorkerConfig
    {
        long rxFrameLength;
        long rxIdOffset;
        long rxIdValue;
        long rxIdMask;
        long rxChecksumMode;
        long rxChecksumStart;
        long rxChecksumLength;
        long rxChecksumOffset;
        long rxQueueCapacity;
        long eventQueueCapacity;
        long silenceTimeoutMs;
        long pollIntervalMs;
        long minimumInterTxMs;
        long workerPriority;

        WorkerConfig();
    };

    struct WorkerStatus
    {
        bool running;
        DWORD rxFrameCount;
        DWORD validRxFrameCount;
        DWORD invalidRxFrameCount;
        DWORD checksumErrorCount;
        DWORD badIdCount;
        DWORD droppedByteCount;
        DWORD txFrameCount;
        DWORD responseTxCount;
        DWORD cyclicTxCount;
        DWORD manualTxCount;
        DWORD rxSilenceTimeoutCount;
        DWORD rxFramesQueued;
        DWORD eventsQueued;
        DWORD pendingTxCount;
        long lastResponseLatencyUs;
        long maxResponseLatencyUs;
        long lastRxAgeMs;
        long lastValidRxAgeMs;
        long lastTxAgeMs;
        long lastErrorCode;
        std::string lastErrorMessage;

        WorkerStatus();
    };

    struct WorkerFrame
    {
        bool found;
        bool checksumValid;
        DWORD sequence;
        SYSTEMTIME timestampUtc;
        long ageMs;
        std::vector<unsigned char> data;

        WorkerFrame();
    };

    void Initialize(HMODULE module);
    void Shutdown();
    void InitializeWorker();
    void ShutdownWorker(bool processTerminating);
    Status Start(const PortConfig& config);
    Status Stop();
    Status CancelPending();
    Status IsOpen(bool* open, std::string* port);
    Status GetConfiguration(PortConfig* config, bool* open);
    Status GetDiagnostics(Diagnostics* diagnostics, bool resetAfterRead);
    Status GetBufferLength(DWORD* bytesAvailable);
    Status ReadBytes(DWORD requestedCount, DWORD timeoutMs, DWORD capacity, std::vector<unsigned char>* data, bool* timedOut);
    Status ReadString(DWORD maxChars, const std::vector<unsigned char>& terminator, DWORD timeoutMs, bool includeTerminator, std::string* text, DWORD* bytesRead, bool* timedOut);
    Status ReadUntilIdle(DWORD maximum, DWORD overallTimeoutMs, DWORD interByteTimeoutMs, std::vector<unsigned char>* data, bool* timedOut);
    Status WriteBytes(const unsigned char* data, DWORD count, DWORD timeoutMs, DWORD* bytesWritten);
    Status Transaction(const unsigned char* request, DWORD requestCount, DWORD responseCapacity,
        const TransactionConfig& config, std::vector<unsigned char>* response,
        DWORD* bytesWritten, bool* timedOut, long* attempts);
    Status Flush(long mask);
    Status DrainTransmit(DWORD timeoutMs, bool* timedOut);
    Status SetControlLines(long dtr, long rts, long breakState);
    Status PulseControlLine(long line, long state, DWORD durationMs, long restoreState);
    Status GetLineStatus(long* cts, long* dsr, long* dcd, long* ring);
    Status DecodeEscapes(const char* text, std::vector<unsigned char>* bytes);
    Status DecodeHex(const char* text, std::vector<unsigned char>* bytes);
    std::string FormatHex(const unsigned char* data, size_t count);
    Status EnumeratePorts(std::string* result, DWORD* count);
    Status GetMoxaPortMode(const char* port, MoxaPortMode* mode);
    Status SetMoxaPortMode(const char* port, long interfaceMode,
        const char* expectedDriverVersion, bool allowUnverifiedDriver,
        bool restartDevice, MoxaPortMode* mode);
    std::string NormalizePortName(const char* port, Status* status);

    bool WorkerBlocksCurrentThread();
    bool WorkerIsRunning();
    Status WorkerStart(const WorkerConfig& config);
    Status WorkerStop(bool clearState);
    Status WorkerGetStatus(WorkerStatus* status, bool resetCounters);
    Status WorkerReadEvents(DWORD maximum, bool clearAfterRead,
        std::string* text, DWORD* returned, DWORD* remaining);
    Status WorkerQueueTx(const std::vector<unsigned char>& data, long mode,
        DWORD quietGapMs, DWORD* queueDepth);
    Status CycleCreate(long jobId, const std::vector<unsigned char>& data,
        DWORD periodMs, DWORD initialDelayMs, bool enabled,
        long checksumMode, long checksumStart, long checksumLength,
        long checksumOffset, std::string* appliedHex, DWORD* activeCount);
    Status CycleUpdate(long jobId, const std::vector<unsigned char>* replacement,
        long byteOffset, long byteValue, long periodMs, long enabled,
        bool recalculateChecksum, std::string* appliedHex, DWORD* activeCount);
    Status CycleDestroy(long jobId, bool* found, DWORD* activeCount);
    Status ResponseCreate(long jobId, const std::vector<unsigned char>& data,
        long triggerOffset, long triggerValue, long triggerMask,
        long responseMode, DWORD responseDelayMs, DWORD quietGapMs,
        bool replacePending, bool enabled, DWORD triggerSkipCount,
        DWORD sendCountLimit, long checksumMode,
        long checksumStart, long checksumLength, long checksumOffset,
        std::string* appliedHex, DWORD* activeCount);
    Status ResponseUpdate(long jobId, const std::vector<unsigned char>* replacement,
        long byteOffset, long byteValue, long responseMode,
        long responseDelayMs, long quietGapMs, long replacePending,
        long enabled, bool resetTriggerCounter, bool recalculateChecksum,
        std::string* appliedHex, DWORD* activeCount);
    Status ResponseDestroy(long jobId, bool* found, DWORD* activeCount);
    Status RxGetCount(DWORD* framesAvailable, DWORD* eventsAvailable,
        DWORD* streamBytes);
    Status RxReadFrame(bool remove, WorkerFrame* frame, DWORD* remaining);
    Status RxClear(bool clearFrames, bool clearEvents, bool clearCounters,
        bool* cleared);
    Status ApplyFrameChecksum(std::vector<unsigned char>* data, long mode,
        long start, long length, long offset);

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
