#include "SRCSerial_Internal.h"

#include <algorithm>
#include <cstdio>
#include <deque>
#include <sstream>

namespace srcserial
{
    namespace
    {
        const size_t MaxWorkerFrameBytes = 4096;
        const size_t MaxWorkerJobs = 32;
        const size_t MaxManualQueue = 32;

        struct TimedFrame
        {
            bool checksumValid;
            DWORD sequence;
            SYSTEMTIME timestampUtc;
            LONGLONG timestampQpc;
            std::vector<unsigned char> data;
        };

        struct WorkerEvent
        {
            SYSTEMTIME timestampUtc;
            std::string type;
            long jobId;
            std::vector<unsigned char> data;
            std::string message;
        };

        struct ChecksumDefinition
        {
            long mode;
            long start;
            long length;
            long offset;
        };

        struct CycleJob
        {
            long id;
            std::vector<unsigned char> data;
            DWORD periodMs;
            bool enabled;
            ChecksumDefinition checksum;
            LONGLONG dueQpc;
        };

        struct ResponseJob
        {
            long id;
            std::vector<unsigned char> data;
            long triggerOffset;
            unsigned char triggerValue;
            unsigned char triggerMask;
            long responseMode;
            DWORD responseDelayMs;
            DWORD quietGapMs;
            bool replacePending;
            bool enabled;
            bool pending;
            DWORD triggerSkipCount;
            DWORD sendCountLimit;
            DWORD triggerCount;
            DWORD sentCount;
            ChecksumDefinition checksum;
            LONGLONG dueQpc;
            LONGLONG triggerQpc;
        };

        struct ManualJob
        {
            std::vector<unsigned char> data;
            long mode;
            DWORD quietGapMs;
            bool ready;
        };

        struct PendingSend
        {
            bool found;
            long kind;
            long jobId;
            LONGLONG triggerQpc;
            std::vector<unsigned char> data;

            PendingSend() : found(false), kind(0), jobId(0), triggerQpc(0) {}
        };

        CRITICAL_SECTION g_workerLock;
        bool g_workerLockInitialized = false;
        HANDLE g_workerThread = NULL;
        HANDLE g_workerWake = NULL;
        DWORD g_workerThreadId = 0;
        volatile LONG g_workerStopRequested = 0;
        bool g_workerRunning = false;
        WorkerConfig g_workerConfig;
        WorkerStatus g_workerStatus;
        std::vector<unsigned char> g_rxStream;
        std::deque<TimedFrame> g_rxFrames;
        std::deque<WorkerEvent> g_events;
        std::vector<CycleJob> g_cycles;
        std::vector<ResponseJob> g_responses;
        std::deque<ManualJob> g_manual;
        LARGE_INTEGER g_qpcFrequency = { 0 };
        LONGLONG g_lastRxQpc = 0;
        LONGLONG g_lastValidRxQpc = 0;
        LONGLONG g_lastTxQpc = 0;
        bool g_silenceReported = false;
        DWORD g_sequence = 0;

        class WorkerLock
        {
        public:
            WorkerLock() { if (g_workerLockInitialized) EnterCriticalSection(&g_workerLock); }
            ~WorkerLock() { if (g_workerLockInitialized) LeaveCriticalSection(&g_workerLock); }
        private:
            WorkerLock(const WorkerLock&);
            WorkerLock& operator=(const WorkerLock&);
        };

        DWORD AddWorkerCounter(DWORD current, DWORD value)
        {
            const DWORD maximum = 0x7fffffffUL;
            return current >= maximum - (std::min)(value, maximum) ? maximum : current + value;
        }

        LONGLONG QpcNow()
        {
            LARGE_INTEGER value;
            QueryPerformanceCounter(&value);
            return value.QuadPart;
        }

        LONGLONG QpcAfterMs(LONGLONG now, DWORD milliseconds)
        {
            return now + (g_qpcFrequency.QuadPart * static_cast<LONGLONG>(milliseconds)) / 1000;
        }

        long QpcAgeMs(LONGLONG timestamp, LONGLONG now)
        {
            if (!timestamp || !g_qpcFrequency.QuadPart) return -1;
            LONGLONG value = ((now - timestamp) * 1000) / g_qpcFrequency.QuadPart;
            if (value < 0) value = 0;
            if (value > 0x7fffffffLL) value = 0x7fffffffLL;
            return static_cast<long>(value);
        }

        void WakeWorker()
        {
            if (g_workerWake) SetEvent(g_workerWake);
        }

        void PushEventNoLock(const char* type, long jobId,
            const std::vector<unsigned char>* data, const char* message)
        {
            if (g_workerConfig.eventQueueCapacity <= 0) return;
            WorkerEvent eventValue;
            GetSystemTime(&eventValue.timestampUtc);
            eventValue.type = type ? type : "INFO";
            eventValue.jobId = jobId;
            if (data) eventValue.data = *data;
            if (message) eventValue.message = message;
            while (g_events.size() >= static_cast<size_t>(g_workerConfig.eventQueueCapacity))
                g_events.pop_front();
            g_events.push_back(eventValue);
        }

        Status ValidateChecksumDefinition(size_t dataSize, long mode,
            long start, long length, long offset)
        {
            if (mode < 0 || mode > 3)
                return Status::Validation(ErrorWorkerChecksum,
                    "ChecksumMode must be 0 none, 1 one's-complement sum, 2 sum, or 3 XOR");
            if (mode == 0) return Status::Ok();
            if (start < 0 || length <= 0 || offset < 0 ||
                static_cast<size_t>(start) >= dataSize ||
                static_cast<size_t>(offset) >= dataSize ||
                static_cast<size_t>(start + length) > dataSize)
                return Status::Validation(ErrorWorkerChecksum,
                    "Checksum start, length, and offset must fit the frame");
            if (offset >= start && offset < start + length)
                return Status::Validation(ErrorWorkerChecksum,
                    "ChecksumOffset cannot be inside the checksum payload range");
            return Status::Ok();
        }

        unsigned char CalculateChecksum(const std::vector<unsigned char>& data,
            long mode, long start, long length)
        {
            unsigned long accumulator = 0;
            for (long i = 0; i < length; ++i)
            {
                const unsigned char value = data[static_cast<size_t>(start + i)];
                accumulator = mode == 3 ? (accumulator ^ value) : (accumulator + value);
            }
            if (mode == 1) return static_cast<unsigned char>(0xFFU - (accumulator & 0xFFU));
            return static_cast<unsigned char>(accumulator & 0xFFU);
        }

        bool ValidateFrameChecksum(const std::vector<unsigned char>& data)
        {
            if (g_workerConfig.rxChecksumMode == 0) return true;
            const unsigned char expected = CalculateChecksum(data,
                g_workerConfig.rxChecksumMode, g_workerConfig.rxChecksumStart,
                g_workerConfig.rxChecksumLength);
            return data[static_cast<size_t>(g_workerConfig.rxChecksumOffset)] == expected;
        }

        bool Matches(const ResponseJob& job, const std::vector<unsigned char>& frame)
        {
            if (job.triggerOffset < 0) return true;
            if (static_cast<size_t>(job.triggerOffset) >= frame.size()) return false;
            return (frame[static_cast<size_t>(job.triggerOffset)] & job.triggerMask) ==
                (job.triggerValue & job.triggerMask);
        }

        DWORD CountPendingNoLock()
        {
            DWORD count = static_cast<DWORD>(g_manual.size());
            for (size_t i = 0; i < g_responses.size(); ++i)
                if (g_responses[i].pending) ++count;
            return count;
        }

        void ResetCountersNoLock()
        {
            g_workerStatus.rxFrameCount = 0;
            g_workerStatus.validRxFrameCount = 0;
            g_workerStatus.invalidRxFrameCount = 0;
            g_workerStatus.checksumErrorCount = 0;
            g_workerStatus.badIdCount = 0;
            g_workerStatus.droppedByteCount = 0;
            g_workerStatus.txFrameCount = 0;
            g_workerStatus.responseTxCount = 0;
            g_workerStatus.cyclicTxCount = 0;
            g_workerStatus.manualTxCount = 0;
            g_workerStatus.rxSilenceTimeoutCount = 0;
            g_workerStatus.lastResponseLatencyUs = -1;
            g_workerStatus.maxResponseLatencyUs = 0;
        }

        void ResetStateNoLock(bool clearQueues)
        {
            ResetCountersNoLock();
            g_workerStatus.lastErrorCode = 0;
            g_workerStatus.lastErrorMessage.clear();
            g_rxStream.clear();
            g_cycles.clear();
            g_responses.clear();
            g_manual.clear();
            if (clearQueues)
            {
                g_rxFrames.clear();
                g_events.clear();
            }
            g_lastRxQpc = 0;
            g_lastValidRxQpc = 0;
            g_lastTxQpc = 0;
            g_silenceReported = false;
            g_sequence = 0;
        }

        void StoreFrameNoLock(const std::vector<unsigned char>& data,
            bool checksumValid, LONGLONG timestampQpc)
        {
            TimedFrame frame;
            frame.checksumValid = checksumValid;
            frame.sequence = ++g_sequence;
            GetSystemTime(&frame.timestampUtc);
            frame.timestampQpc = timestampQpc;
            frame.data = data;
            while (g_rxFrames.size() >= static_cast<size_t>(g_workerConfig.rxQueueCapacity))
                g_rxFrames.pop_front();
            g_rxFrames.push_back(frame);
        }

        void ScheduleResponsesNoLock(const std::vector<unsigned char>& frame,
            LONGLONG now)
        {
            for (size_t i = 0; i < g_manual.size(); ++i)
                if (g_manual[i].mode == 1) g_manual[i].ready = true;
            for (size_t i = 0; i < g_responses.size(); ++i)
            {
                ResponseJob& job = g_responses[i];
                if (!job.enabled || !Matches(job, frame)) continue;
                job.triggerCount = AddWorkerCounter(job.triggerCount, 1);
                if (job.triggerCount <= job.triggerSkipCount) continue;
                if (job.sendCountLimit && job.sentCount >= job.sendCountLimit) continue;
                if (job.pending && !job.replacePending) continue;
                job.pending = true;
                job.dueQpc = QpcAfterMs(now, job.responseDelayMs);
                job.triggerQpc = now;
            }
        }

        void ParseFramesNoLock(LONGLONG now)
        {
            const size_t frameLength = static_cast<size_t>(g_workerConfig.rxFrameLength);
            const size_t idOffset = static_cast<size_t>(g_workerConfig.rxIdOffset);
            while (g_rxStream.size() >= frameLength)
            {
                if ((g_rxStream[idOffset] & static_cast<unsigned char>(g_workerConfig.rxIdMask)) !=
                    (static_cast<unsigned char>(g_workerConfig.rxIdValue) &
                     static_cast<unsigned char>(g_workerConfig.rxIdMask)))
                {
                    g_rxStream.erase(g_rxStream.begin());
                    g_workerStatus.badIdCount = AddWorkerCounter(g_workerStatus.badIdCount, 1);
                    g_workerStatus.droppedByteCount = AddWorkerCounter(g_workerStatus.droppedByteCount, 1);
                    continue;
                }

                std::vector<unsigned char> frame(g_rxStream.begin(),
                    g_rxStream.begin() + frameLength);
                const bool valid = ValidateFrameChecksum(frame);
                g_workerStatus.rxFrameCount = AddWorkerCounter(g_workerStatus.rxFrameCount, 1);
                StoreFrameNoLock(frame, valid, now);
                if (valid)
                {
                    g_workerStatus.validRxFrameCount = AddWorkerCounter(
                        g_workerStatus.validRxFrameCount, 1);
                    g_lastValidRxQpc = now;
                    PushEventNoLock("RX_VALID", 0, &frame, NULL);
                    ScheduleResponsesNoLock(frame, now);
                    g_rxStream.erase(g_rxStream.begin(), g_rxStream.begin() + frameLength);
                }
                else
                {
                    g_workerStatus.invalidRxFrameCount = AddWorkerCounter(
                        g_workerStatus.invalidRxFrameCount, 1);
                    g_workerStatus.checksumErrorCount = AddWorkerCounter(
                        g_workerStatus.checksumErrorCount, 1);
                    PushEventNoLock("RX_CHECKSUM", 0, &frame, "checksum validation failed");
                    g_rxStream.erase(g_rxStream.begin());
                    g_workerStatus.droppedByteCount = AddWorkerCounter(
                        g_workerStatus.droppedByteCount, 1);
                }
            }
            const size_t maximumStream = frameLength * 4;
            while (g_rxStream.size() > maximumStream)
            {
                g_rxStream.erase(g_rxStream.begin());
                g_workerStatus.droppedByteCount = AddWorkerCounter(
                    g_workerStatus.droppedByteCount, 1);
            }
        }

        bool MinimumTxGapSatisfiedNoLock(LONGLONG now)
        {
            return !g_lastTxQpc || QpcAgeMs(g_lastTxQpc, now) >= g_workerConfig.minimumInterTxMs;
        }

        PendingSend SelectSendNoLock(LONGLONG now)
        {
            PendingSend send;
            if (!MinimumTxGapSatisfiedNoLock(now)) return send;

            for (size_t i = 0; i < g_responses.size(); ++i)
            {
                ResponseJob& job = g_responses[i];
                if (!job.enabled || !job.pending || now < job.dueQpc) continue;
                if (job.responseMode == 1 &&
                    QpcAgeMs(g_lastRxQpc, now) < static_cast<long>(job.quietGapMs))
                    continue;
                send.found = true;
                send.kind = 1;
                send.jobId = job.id;
                send.triggerQpc = job.triggerQpc;
                send.data = job.data;
                job.pending = false;
                return send;
            }

            for (std::deque<ManualJob>::iterator it = g_manual.begin();
                it != g_manual.end(); ++it)
            {
                if (!it->ready) continue;
                if (it->mode == 2 &&
                    QpcAgeMs(g_lastRxQpc, now) < static_cast<long>(it->quietGapMs))
                    continue;
                send.found = true;
                send.kind = 2;
                send.data = it->data;
                g_manual.erase(it);
                return send;
            }

            for (size_t i = 0; i < g_cycles.size(); ++i)
            {
                CycleJob& job = g_cycles[i];
                if (!job.enabled || now < job.dueQpc) continue;
                send.found = true;
                send.kind = 3;
                send.jobId = job.id;
                send.data = job.data;
                job.dueQpc = QpcAfterMs(now, job.periodMs);
                return send;
            }
            return send;
        }

        void RecordSendResult(const PendingSend& send, const Status& result,
            DWORD bytesWritten, LONGLONG now)
        {
            WorkerLock lock;
            if (result.success && bytesWritten == send.data.size())
            {
                g_lastTxQpc = now;
                g_workerStatus.txFrameCount = AddWorkerCounter(g_workerStatus.txFrameCount, 1);
                if (send.kind == 1)
                {
                    g_workerStatus.responseTxCount = AddWorkerCounter(g_workerStatus.responseTxCount, 1);
                    for (size_t i = 0; i < g_responses.size(); ++i)
                        if (g_responses[i].id == send.jobId)
                        {
                            g_responses[i].sentCount = AddWorkerCounter(
                                g_responses[i].sentCount, 1);
                            break;
                        }
                    if (send.triggerQpc && g_qpcFrequency.QuadPart)
                    {
                        LONGLONG latency = ((now - send.triggerQpc) * 1000000LL) /
                            g_qpcFrequency.QuadPart;
                        if (latency < 0) latency = 0;
                        if (latency > 0x7fffffffLL) latency = 0x7fffffffLL;
                        g_workerStatus.lastResponseLatencyUs = static_cast<long>(latency);
                        if (g_workerStatus.lastResponseLatencyUs > g_workerStatus.maxResponseLatencyUs)
                            g_workerStatus.maxResponseLatencyUs = g_workerStatus.lastResponseLatencyUs;
                    }
                }
                else if (send.kind == 2)
                    g_workerStatus.manualTxCount = AddWorkerCounter(g_workerStatus.manualTxCount, 1);
                else if (send.kind == 3)
                    g_workerStatus.cyclicTxCount = AddWorkerCounter(g_workerStatus.cyclicTxCount, 1);
                PushEventNoLock(send.kind == 1 ? "TX_RESPONSE" :
                    (send.kind == 2 ? "TX_MANUAL" : "TX_CYCLE"),
                    send.jobId, &send.data, NULL);
            }
            else
            {
                if (InterlockedCompareExchange(&g_workerStopRequested, 0, 0) != 0)
                    return;
                g_workerStatus.lastErrorCode = result.code;
                g_workerStatus.lastErrorMessage = result.message;
                PushEventNoLock("TRANSPORT_ERROR", send.jobId, &send.data,
                    result.message.c_str());
                InterlockedExchange(&g_workerStopRequested, 1);
            }
        }

        DWORD WINAPI WorkerThreadProc(LPVOID)
        {
            const int priorities[] = { THREAD_PRIORITY_NORMAL,
                THREAD_PRIORITY_ABOVE_NORMAL, THREAD_PRIORITY_HIGHEST };
            SetThreadPriority(GetCurrentThread(), priorities[g_workerConfig.workerPriority]);

            while (InterlockedCompareExchange(&g_workerStopRequested, 0, 0) == 0)
            {
                std::vector<unsigned char> received;
                bool timedOut = false;
                const Status readStatus = ReadBytes(0, 0, 4096, &received, &timedOut);
                const LONGLONG now = QpcNow();
                if (!readStatus.success)
                {
                    if (InterlockedCompareExchange(&g_workerStopRequested, 0, 0) != 0)
                        break;
                    WorkerLock lock;
                    g_workerStatus.lastErrorCode = readStatus.code;
                    g_workerStatus.lastErrorMessage = readStatus.message;
                    PushEventNoLock("TRANSPORT_ERROR", 0, NULL,
                        readStatus.message.c_str());
                    InterlockedExchange(&g_workerStopRequested, 1);
                    break;
                }

                PendingSend send;
                {
                    WorkerLock lock;
                    if (!received.empty())
                    {
                        g_lastRxQpc = now;
                        g_silenceReported = false;
                        g_rxStream.insert(g_rxStream.end(), received.begin(), received.end());
                        ParseFramesNoLock(now);
                    }
                    if (g_workerConfig.silenceTimeoutMs > 0 && g_lastRxQpc &&
                        !g_silenceReported &&
                        QpcAgeMs(g_lastRxQpc, now) >= g_workerConfig.silenceTimeoutMs)
                    {
                        g_silenceReported = true;
                        g_workerStatus.rxSilenceTimeoutCount = AddWorkerCounter(
                            g_workerStatus.rxSilenceTimeoutCount, 1);
                        PushEventNoLock("RX_SILENCE", 0, NULL,
                            "receive silence threshold exceeded");
                    }
                    send = SelectSendNoLock(now);
                }

                if (send.found)
                {
                    DWORD written = 0;
                    const Status writeStatus = WriteBytes(&send.data[0],
                        static_cast<DWORD>(send.data.size()), MAXDWORD, &written);
                    RecordSendResult(send, writeStatus, written, QpcNow());
                }
                else
                    WaitForSingleObject(g_workerWake,
                        static_cast<DWORD>(g_workerConfig.pollIntervalMs));
            }

            {
                WorkerLock lock;
                g_workerRunning = false;
                g_workerStatus.running = false;
                PushEventNoLock("WORKER_STOP", 0, NULL,
                    g_workerStatus.lastErrorMessage.empty() ? NULL :
                    g_workerStatus.lastErrorMessage.c_str());
            }
            return 0;
        }

        template <typename T>
        typename std::vector<T>::iterator FindJob(std::vector<T>& jobs, long id)
        {
            for (typename std::vector<T>::iterator it = jobs.begin(); it != jobs.end(); ++it)
                if (it->id == id) return it;
            return jobs.end();
        }

        Status RequireRunningNoLock()
        {
            return g_workerRunning ? Status::Ok() :
                Status::Validation(ErrorWorkerNotRunning, "Protocol worker is not running");
        }

        Status ValidateJobData(const std::vector<unsigned char>& data,
            long checksumMode, long checksumStart, long checksumLength,
            long checksumOffset)
        {
            if (data.empty() || data.size() > MaxWorkerFrameBytes)
                return Status::Validation(ErrorInvalidParameter,
                    "Worker TX frame must contain 1 through 4096 bytes");
            return ValidateChecksumDefinition(data.size(), checksumMode,
                checksumStart, checksumLength, checksumOffset);
        }
    }

    WorkerConfig::WorkerConfig()
        : rxFrameLength(1), rxIdOffset(0), rxIdValue(0), rxIdMask(0),
          rxChecksumMode(0), rxChecksumStart(0), rxChecksumLength(0),
          rxChecksumOffset(-1), rxQueueCapacity(256), eventQueueCapacity(512),
          silenceTimeoutMs(1000), pollIntervalMs(1), minimumInterTxMs(0),
          workerPriority(1)
    {
    }

    WorkerStatus::WorkerStatus()
        : running(false), rxFrameCount(0), validRxFrameCount(0),
          invalidRxFrameCount(0), checksumErrorCount(0), badIdCount(0),
          droppedByteCount(0), txFrameCount(0), responseTxCount(0),
          cyclicTxCount(0), manualTxCount(0), rxSilenceTimeoutCount(0),
          rxFramesQueued(0), eventsQueued(0), pendingTxCount(0),
          lastResponseLatencyUs(-1), maxResponseLatencyUs(0),
          lastRxAgeMs(-1), lastValidRxAgeMs(-1), lastTxAgeMs(-1),
          lastErrorCode(0)
    {
    }

    WorkerFrame::WorkerFrame()
        : found(false), checksumValid(false), sequence(0), ageMs(-1)
    {
        ZeroMemory(&timestampUtc, sizeof(timestampUtc));
    }

    void InitializeWorker()
    {
        InitializeCriticalSection(&g_workerLock);
        g_workerLockInitialized = true;
        QueryPerformanceFrequency(&g_qpcFrequency);
    }

    void ShutdownWorker(bool processTerminating)
    {
        if (!g_workerLockInitialized) return;
        InterlockedExchange(&g_workerStopRequested, 1);
        WakeWorker();
        CancelPending();
        if (!processTerminating && g_workerThread &&
            GetCurrentThreadId() != g_workerThreadId)
            WaitForSingleObject(g_workerThread, INFINITE);
        if (g_workerThread) CloseHandle(g_workerThread);
        if (g_workerWake) CloseHandle(g_workerWake);
        g_workerThread = NULL;
        g_workerWake = NULL;
        g_workerThreadId = 0;
        g_workerRunning = false;
        DeleteCriticalSection(&g_workerLock);
        g_workerLockInitialized = false;
    }

    bool WorkerBlocksCurrentThread()
    {
        if (!g_workerLockInitialized) return false;
        WorkerLock lock;
        return g_workerRunning && GetCurrentThreadId() != g_workerThreadId;
    }

    bool WorkerIsRunning()
    {
        if (!g_workerLockInitialized) return false;
        WorkerLock lock;
        return g_workerRunning;
    }

    Status ApplyFrameChecksum(std::vector<unsigned char>* data, long mode,
        long start, long length, long offset)
    {
        if (!data) return Status::Validation(ErrorInvalidParameter, "Frame data is required");
        const Status validation = ValidateChecksumDefinition(data->size(), mode,
            start, length, offset);
        if (!validation.success || mode == 0) return validation;
        (*data)[static_cast<size_t>(offset)] = CalculateChecksum(*data, mode,
            start, length);
        return Status::Ok();
    }

    Status WorkerStart(const WorkerConfig& config)
    {
        if (config.rxFrameLength < 1 || config.rxFrameLength > 4096 ||
            config.rxIdOffset < 0 || config.rxIdOffset >= config.rxFrameLength ||
            config.rxIdValue < 0 || config.rxIdValue > 255 ||
            config.rxIdMask < 0 || config.rxIdMask > 255)
            return Status::Validation(ErrorInvalidParameter,
                "RX length, ID offset, ID value, and ID mask are invalid");
        const Status checksumValidation = ValidateChecksumDefinition(
            static_cast<size_t>(config.rxFrameLength), config.rxChecksumMode,
            config.rxChecksumStart, config.rxChecksumLength,
            config.rxChecksumOffset);
        if (!checksumValidation.success) return checksumValidation;
        if (config.rxQueueCapacity < 1 || config.rxQueueCapacity > 4096 ||
            config.eventQueueCapacity < 1 || config.eventQueueCapacity > 8192 ||
            config.silenceTimeoutMs < 0 || config.pollIntervalMs < 1 ||
            config.pollIntervalMs > 50 || config.minimumInterTxMs < 0 ||
            config.minimumInterTxMs > 60000 || config.workerPriority < 0 ||
            config.workerPriority > 2)
            return Status::Validation(ErrorInvalidParameter,
                "Worker queue, timing, or priority setting is outside its supported range");

        bool open = false;
        std::string port;
        Status status = IsOpen(&open, &port);
        if (!status.success) return status;
        if (!open) return Status::Validation(ErrorNotOpen, "Serial port is not open");

        WorkerStop(true);
        WorkerLock lock;
        g_workerConfig = config;
        g_workerStatus = WorkerStatus();
        ResetStateNoLock(true);
        InterlockedExchange(&g_workerStopRequested, 0);
        g_workerWake = CreateEventA(NULL, FALSE, FALSE, NULL);
        if (!g_workerWake)
            return Status::Win32Error("CreateEvent(worker)", GetLastError());
        g_workerRunning = true;
        g_workerStatus.running = true;
        PushEventNoLock("WORKER_START", 0, NULL, port.c_str());
        g_workerThread = CreateThread(NULL, 0, WorkerThreadProc, NULL, 0,
            &g_workerThreadId);
        if (!g_workerThread)
        {
            const DWORD error = GetLastError();
            g_workerRunning = false;
            g_workerStatus.running = false;
            CloseHandle(g_workerWake);
            g_workerWake = NULL;
            g_workerThreadId = 0;
            return Status::Win32Error("CreateThread(worker)", error);
        }
        return Status::Ok();
    }

    Status WorkerStop(bool clearState)
    {
        if (!g_workerLockInitialized) return Status::Ok();
        HANDLE thread = NULL;
        {
            WorkerLock lock;
            InterlockedExchange(&g_workerStopRequested, 1);
            WakeWorker();
            thread = g_workerThread;
        }
        CancelPending();
        if (thread && GetCurrentThreadId() != g_workerThreadId)
        {
            const DWORD wait = WaitForSingleObject(thread, 5000);
            if (wait == WAIT_TIMEOUT)
                return Status::Validation(ErrorInternal,
                    "Protocol worker did not stop within 5000 ms");
        }
        WorkerLock lock;
        if (g_workerThread)
        {
            CloseHandle(g_workerThread);
            g_workerThread = NULL;
        }
        if (g_workerWake)
        {
            CloseHandle(g_workerWake);
            g_workerWake = NULL;
        }
        g_workerThreadId = 0;
        g_workerRunning = false;
        g_workerStatus.running = false;
        g_cycles.clear();
        g_responses.clear();
        g_manual.clear();
        g_rxStream.clear();
        if (clearState)
        {
            g_rxFrames.clear();
            g_events.clear();
            ResetCountersNoLock();
            g_workerStatus.lastErrorCode = 0;
            g_workerStatus.lastErrorMessage.clear();
        }
        return Status::Ok();
    }

    Status WorkerGetStatus(WorkerStatus* status, bool resetCounters)
    {
        if (!status) return Status::Validation(ErrorInvalidParameter,
            "Worker status output is required");
        WorkerLock lock;
        *status = g_workerStatus;
        status->running = g_workerRunning;
        status->rxFramesQueued = static_cast<DWORD>(g_rxFrames.size());
        status->eventsQueued = static_cast<DWORD>(g_events.size());
        status->pendingTxCount = CountPendingNoLock();
        const LONGLONG now = QpcNow();
        status->lastRxAgeMs = QpcAgeMs(g_lastRxQpc, now);
        status->lastValidRxAgeMs = QpcAgeMs(g_lastValidRxQpc, now);
        status->lastTxAgeMs = QpcAgeMs(g_lastTxQpc, now);
        if (resetCounters) ResetCountersNoLock();
        return Status::Ok();
    }

    Status WorkerReadEvents(DWORD maximum, bool clearAfterRead,
        std::string* text, DWORD* returned, DWORD* remaining)
    {
        if (!text || !returned || !remaining || maximum == 0 || maximum > 1000)
            return Status::Validation(ErrorInvalidParameter,
                "Event outputs and MaxEvents 1 through 1000 are required");
        WorkerLock lock;
        text->clear();
        *returned = (std::min)(maximum, static_cast<DWORD>(g_events.size()));
        std::ostringstream stream;
        for (DWORD i = 0; i < *returned; ++i)
        {
            const WorkerEvent& eventValue = g_events[static_cast<size_t>(i)];
            char timestamp[40] = { 0 };
            ::sprintf_s(timestamp, "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
                eventValue.timestampUtc.wYear, eventValue.timestampUtc.wMonth,
                eventValue.timestampUtc.wDay, eventValue.timestampUtc.wHour,
                eventValue.timestampUtc.wMinute, eventValue.timestampUtc.wSecond,
                eventValue.timestampUtc.wMilliseconds);
            stream << timestamp << " " << eventValue.type;
            if (eventValue.jobId) stream << " job=" << eventValue.jobId;
            if (!eventValue.data.empty()) stream << " " << FormatHex(
                &eventValue.data[0], eventValue.data.size());
            if (!eventValue.message.empty()) stream << " " << eventValue.message;
            stream << "\r\n";
        }
        *text = stream.str();
        if (clearAfterRead)
            for (DWORD i = 0; i < *returned; ++i) g_events.pop_front();
        *remaining = static_cast<DWORD>(g_events.size()) -
            (clearAfterRead ? 0 : *returned);
        return Status::Ok();
    }

    Status WorkerQueueTx(const std::vector<unsigned char>& data, long mode,
        DWORD quietGapMs, DWORD* queueDepth)
    {
        if (!queueDepth || data.empty() || data.size() > MaxWorkerFrameBytes ||
            mode < 0 || mode > 2 || quietGapMs > 60000)
            return Status::Validation(ErrorInvalidParameter,
                "Manual frame, Mode 0..2, QuietGapMs, and QueueDepth are invalid");
        WorkerLock lock;
        Status status = RequireRunningNoLock();
        if (!status.success) return status;
        if (g_manual.size() >= MaxManualQueue)
            return Status::Validation(ErrorWorkerQueueFull,
                "Manual TX queue is full");
        ManualJob job;
        job.data = data;
        job.mode = mode;
        job.quietGapMs = quietGapMs;
        job.ready = mode != 1;
        g_manual.push_back(job);
        *queueDepth = static_cast<DWORD>(g_manual.size());
        WakeWorker();
        return Status::Ok();
    }

    Status CycleCreate(long jobId, const std::vector<unsigned char>& data,
        DWORD periodMs, DWORD initialDelayMs, bool enabled,
        long checksumMode, long checksumStart, long checksumLength,
        long checksumOffset, std::string* appliedHex, DWORD* activeCount)
    {
        if (!appliedHex || !activeCount || jobId <= 0 || periodMs == 0 ||
            periodMs > 86400000UL || initialDelayMs > 86400000UL)
            return Status::Validation(ErrorInvalidParameter,
                "Cycle ID, timing, and outputs are invalid");
        Status status = ValidateJobData(data, checksumMode, checksumStart,
            checksumLength, checksumOffset);
        if (!status.success) return status;
        WorkerLock lock;
        status = RequireRunningNoLock();
        if (!status.success) return status;
        if (g_cycles.size() >= MaxWorkerJobs)
            return Status::Validation(ErrorWorkerQueueFull, "Cyclic job table is full");
        if (FindJob(g_cycles, jobId) != g_cycles.end())
            return Status::Validation(ErrorWorkerDuplicateJob, "Cyclic JobId already exists");
        CycleJob job;
        job.id = jobId;
        job.data = data;
        job.periodMs = periodMs;
        job.enabled = enabled;
        job.checksum.mode = checksumMode;
        job.checksum.start = checksumStart;
        job.checksum.length = checksumLength;
        job.checksum.offset = checksumOffset;
        status = ApplyFrameChecksum(&job.data, checksumMode, checksumStart,
            checksumLength, checksumOffset);
        if (!status.success) return status;
        job.dueQpc = QpcAfterMs(QpcNow(), initialDelayMs);
        g_cycles.push_back(job);
        *appliedHex = FormatHex(&job.data[0], job.data.size());
        *activeCount = static_cast<DWORD>(g_cycles.size());
        WakeWorker();
        return Status::Ok();
    }

    Status CycleUpdate(long jobId, const std::vector<unsigned char>* replacement,
        long byteOffset, long byteValue, long periodMs, long enabled,
        bool recalculateChecksum, std::string* appliedHex, DWORD* activeCount)
    {
        if (!appliedHex || !activeCount || jobId <= 0 || byteOffset < -1 ||
            byteValue < 0 || byteValue > 255 || periodMs < -1 || periodMs == 0 ||
            enabled < -1 || enabled > 1)
            return Status::Validation(ErrorInvalidParameter, "Cyclic update parameters are invalid");
        WorkerLock lock;
        Status status = RequireRunningNoLock();
        if (!status.success) return status;
        std::vector<CycleJob>::iterator it = FindJob(g_cycles, jobId);
        if (it == g_cycles.end())
            return Status::Validation(ErrorWorkerJobNotFound, "Cyclic JobId was not found");
        std::vector<unsigned char> updated = replacement ? *replacement : it->data;
        status = ValidateJobData(updated, it->checksum.mode, it->checksum.start,
            it->checksum.length, it->checksum.offset);
        if (!status.success) return status;
        if (byteOffset >= 0)
        {
            if (static_cast<size_t>(byteOffset) >= updated.size())
                return Status::Validation(ErrorInvalidParameter, "ByteOffset is outside the cyclic frame");
            updated[static_cast<size_t>(byteOffset)] = static_cast<unsigned char>(byteValue);
        }
        if (recalculateChecksum)
        {
            status = ApplyFrameChecksum(&updated, it->checksum.mode,
                it->checksum.start, it->checksum.length, it->checksum.offset);
            if (!status.success) return status;
        }
        it->data.swap(updated);
        if (periodMs > 0)
        {
            it->periodMs = static_cast<DWORD>(periodMs);
            it->dueQpc = QpcAfterMs(QpcNow(), it->periodMs);
        }
        if (enabled >= 0)
        {
            const bool wasEnabled = it->enabled;
            it->enabled = enabled != 0;
            if (!wasEnabled && it->enabled) it->dueQpc = QpcNow();
        }
        *appliedHex = FormatHex(&it->data[0], it->data.size());
        *activeCount = static_cast<DWORD>(g_cycles.size());
        WakeWorker();
        return Status::Ok();
    }

    Status CycleDestroy(long jobId, bool* found, DWORD* activeCount)
    {
        if (!found || !activeCount || jobId <= 0)
            return Status::Validation(ErrorInvalidParameter, "Cycle outputs and positive JobId are required");
        WorkerLock lock;
        Status status = RequireRunningNoLock();
        if (!status.success) return status;
        std::vector<CycleJob>::iterator it = FindJob(g_cycles, jobId);
        *found = it != g_cycles.end();
        if (*found) g_cycles.erase(it);
        *activeCount = static_cast<DWORD>(g_cycles.size());
        return Status::Ok();
    }

    Status ResponseCreate(long jobId, const std::vector<unsigned char>& data,
        long triggerOffset, long triggerValue, long triggerMask,
        long responseMode, DWORD responseDelayMs, DWORD quietGapMs,
        bool replacePending, bool enabled, DWORD triggerSkipCount,
        DWORD sendCountLimit, long checksumMode,
        long checksumStart, long checksumLength, long checksumOffset,
        std::string* appliedHex, DWORD* activeCount)
    {
        if (!appliedHex || !activeCount || jobId <= 0 || triggerOffset < -1 ||
            triggerValue < 0 ||
            triggerValue > 255 || triggerMask < 0 || triggerMask > 255 ||
            responseMode < 0 || responseMode > 1 || responseDelayMs > 60000 ||
            quietGapMs > 60000)
            return Status::Validation(ErrorInvalidParameter, "Response job parameters are invalid");
        Status status = ValidateJobData(data, checksumMode, checksumStart,
            checksumLength, checksumOffset);
        if (!status.success) return status;
        WorkerLock lock;
        status = RequireRunningNoLock();
        if (!status.success) return status;
        if (triggerOffset >= g_workerConfig.rxFrameLength)
            return Status::Validation(ErrorInvalidParameter,
                "TriggerOffset must fit the configured RX frame");
        if (g_responses.size() >= MaxWorkerJobs)
            return Status::Validation(ErrorWorkerQueueFull, "Response job table is full");
        if (FindJob(g_responses, jobId) != g_responses.end())
            return Status::Validation(ErrorWorkerDuplicateJob, "Response JobId already exists");
        ResponseJob job;
        job.id = jobId;
        job.data = data;
        job.triggerOffset = triggerOffset;
        job.triggerValue = static_cast<unsigned char>(triggerValue);
        job.triggerMask = static_cast<unsigned char>(triggerMask);
        job.responseMode = responseMode;
        job.responseDelayMs = responseDelayMs;
        job.quietGapMs = quietGapMs;
        job.replacePending = replacePending;
        job.enabled = enabled;
        job.pending = false;
        job.triggerSkipCount = triggerSkipCount;
        job.sendCountLimit = sendCountLimit;
        job.triggerCount = 0;
        job.sentCount = 0;
        job.checksum.mode = checksumMode;
        job.checksum.start = checksumStart;
        job.checksum.length = checksumLength;
        job.checksum.offset = checksumOffset;
        status = ApplyFrameChecksum(&job.data, checksumMode, checksumStart,
            checksumLength, checksumOffset);
        if (!status.success) return status;
        job.dueQpc = 0;
        job.triggerQpc = 0;
        g_responses.push_back(job);
        *appliedHex = FormatHex(&job.data[0], job.data.size());
        *activeCount = static_cast<DWORD>(g_responses.size());
        WakeWorker();
        return Status::Ok();
    }

    Status ResponseUpdate(long jobId, const std::vector<unsigned char>* replacement,
        long byteOffset, long byteValue, long responseMode,
        long responseDelayMs, long quietGapMs, long replacePending,
        long enabled, bool resetTriggerCounter, bool recalculateChecksum,
        std::string* appliedHex, DWORD* activeCount)
    {
        if (!appliedHex || !activeCount || jobId <= 0 || byteOffset < -1 ||
            byteValue < 0 || byteValue > 255 || responseMode < -1 ||
            responseMode > 1 || responseDelayMs < -1 || quietGapMs < -1 ||
            replacePending < -1 || replacePending > 1 || enabled < -1 || enabled > 1)
            return Status::Validation(ErrorInvalidParameter, "Response update parameters are invalid");
        WorkerLock lock;
        Status status = RequireRunningNoLock();
        if (!status.success) return status;
        std::vector<ResponseJob>::iterator it = FindJob(g_responses, jobId);
        if (it == g_responses.end())
            return Status::Validation(ErrorWorkerJobNotFound, "Response JobId was not found");
        std::vector<unsigned char> updated = replacement ? *replacement : it->data;
        status = ValidateJobData(updated, it->checksum.mode, it->checksum.start,
            it->checksum.length, it->checksum.offset);
        if (!status.success) return status;
        if (byteOffset >= 0)
        {
            if (static_cast<size_t>(byteOffset) >= updated.size())
                return Status::Validation(ErrorInvalidParameter, "ByteOffset is outside the response frame");
            updated[static_cast<size_t>(byteOffset)] = static_cast<unsigned char>(byteValue);
        }
        if (recalculateChecksum)
        {
            status = ApplyFrameChecksum(&updated, it->checksum.mode,
                it->checksum.start, it->checksum.length, it->checksum.offset);
            if (!status.success) return status;
        }
        it->data.swap(updated);
        if (responseMode >= 0) it->responseMode = responseMode;
        if (responseDelayMs >= 0) it->responseDelayMs = static_cast<DWORD>(responseDelayMs);
        if (quietGapMs >= 0) it->quietGapMs = static_cast<DWORD>(quietGapMs);
        if (replacePending >= 0) it->replacePending = replacePending != 0;
        if (enabled >= 0)
        {
            it->enabled = enabled != 0;
            if (!it->enabled) it->pending = false;
        }
        if (resetTriggerCounter)
        {
            it->triggerCount = 0;
            it->sentCount = 0;
            it->pending = false;
        }
        *appliedHex = FormatHex(&it->data[0], it->data.size());
        *activeCount = static_cast<DWORD>(g_responses.size());
        WakeWorker();
        return Status::Ok();
    }

    Status ResponseDestroy(long jobId, bool* found, DWORD* activeCount)
    {
        if (!found || !activeCount || jobId <= 0)
            return Status::Validation(ErrorInvalidParameter, "Response outputs and positive JobId are required");
        WorkerLock lock;
        Status status = RequireRunningNoLock();
        if (!status.success) return status;
        std::vector<ResponseJob>::iterator it = FindJob(g_responses, jobId);
        *found = it != g_responses.end();
        if (*found) g_responses.erase(it);
        *activeCount = static_cast<DWORD>(g_responses.size());
        return Status::Ok();
    }

    Status RxGetCount(DWORD* framesAvailable, DWORD* eventsAvailable,
        DWORD* streamBytes)
    {
        if (!framesAvailable || !eventsAvailable || !streamBytes)
            return Status::Validation(ErrorInvalidParameter, "RX queue outputs are required");
        WorkerLock lock;
        *framesAvailable = static_cast<DWORD>(g_rxFrames.size());
        *eventsAvailable = static_cast<DWORD>(g_events.size());
        *streamBytes = static_cast<DWORD>(g_rxStream.size());
        return Status::Ok();
    }

    Status RxReadFrame(bool remove, WorkerFrame* frame, DWORD* remaining)
    {
        if (!frame || !remaining)
            return Status::Validation(ErrorInvalidParameter, "RX frame outputs are required");
        WorkerLock lock;
        *frame = WorkerFrame();
        if (g_rxFrames.empty())
        {
            *remaining = 0;
            return Status::Ok();
        }
        const TimedFrame& stored = g_rxFrames.front();
        frame->found = true;
        frame->checksumValid = stored.checksumValid;
        frame->sequence = stored.sequence;
        frame->timestampUtc = stored.timestampUtc;
        frame->ageMs = QpcAgeMs(stored.timestampQpc, QpcNow());
        frame->data = stored.data;
        if (remove) g_rxFrames.pop_front();
        *remaining = static_cast<DWORD>(g_rxFrames.size()) - (remove ? 0 : 1);
        return Status::Ok();
    }

    Status RxClear(bool clearFrames, bool clearEvents, bool clearCounters,
        bool* cleared)
    {
        if (!cleared) return Status::Validation(ErrorInvalidParameter, "Cleared output is required");
        WorkerLock lock;
        if (clearFrames) { g_rxFrames.clear(); g_rxStream.clear(); }
        if (clearEvents) g_events.clear();
        if (clearCounters) ResetCountersNoLock();
        *cleared = clearFrames || clearEvents || clearCounters;
        return Status::Ok();
    }
}
