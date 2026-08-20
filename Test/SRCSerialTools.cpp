#include <windows.h>

#include "uta.h"
#include "utaapi.h"
#include "utacore.h"
#include "../src/SRCSerial_Internal.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    enum ParmKind { Int32Kind, StringKind, Int32ArrayKind };

    struct ParmSpec
    {
        const char* name;
        ParmKind kind;
        long intValue;
        const char* stringValue;
        int arraySize;
        bool output;
    };

    ParmSpec Int32(const char* name, long value, bool output = false)
    { ParmSpec p = { name, Int32Kind, value, "", 0, output }; return p; }
    ParmSpec String(const char* name, const char* value, bool output = false)
    { ParmSpec p = { name, StringKind, 0, value, 0, output }; return p; }
    ParmSpec Int32Array(const char* name, int size, bool output = false)
    { ParmSpec p = { name, Int32ArrayKind, 0, "", size, output }; return p; }

    void AddParm(HUTAPBDEF definition, const ParmSpec& spec)
    {
        const char* type = spec.kind == Int32Kind ? "CUtaInt32" :
            (spec.kind == StringKind ? "CUtaString" : "CUtaInt32Array");
        HUTAPARM parm = UtaParmCreate(spec.name, type);
        if (spec.kind == Int32Kind) UtaParmSetData(parm, reinterpret_cast<HUTADATA>(UtaInt32Create(spec.intValue)));
        else if (spec.kind == StringKind) UtaParmSetData(parm, reinterpret_cast<HUTADATA>(UtaStringCreate(spec.stringValue)));
        else UtaParmSetData(parm, reinterpret_cast<HUTADATA>(UtaI32ArrCreate(0, static_cast<UTAINT16>(spec.arraySize - 1))));
        if (spec.output) UtaParmSetOutput(parm);
        UtaPbDefAddParm(definition, parm);
    }

    void AddCommon(std::vector<ParmSpec>* parms)
    {
        parms->push_back(Int32("Success", 0, true));
        parms->push_back(Int32("ErrorCode", 0, true));
        parms->push_back(String("ErrorMessage", "", true));
    }

    bool SaveAction(const char* actionName, const std::vector<ParmSpec>& parms)
    {
        HUTAMDEF action = UtaMDefStdCCreate(actionName);
        HUTAPBDEF definition = UtaPbDefCreate();
        if (!action || !definition) return false;
        for (size_t i = 0; i < parms.size(); ++i) AddParm(definition, parms[i]);
        UtaMDefSetAuthor(action, "SRC Electrical");
        UtaMDefSetLibraryName(action, "SRCSerial.dll");
        UtaMDefSetSourceName(action, "src\\SRCSerial_Actions.cpp");
        UtaMDefSetActionEntryName(action, UTA_ACT_INITIATE_ID, actionName);
        UtaMDefSetParmBlockDef(action, definition);
        const std::string fileName = std::string("actions\\") + actionName + ".umd";
        UtaMDefSave(action, fileName.c_str());
        UtaMDefRelease(action);
        std::printf("generated %s\n", fileName.c_str());
        return true;
    }

    bool GenerateActions()
    {
        CreateDirectoryA("actions", NULL);
        std::vector<ParmSpec> p;
        p.push_back(String("Port", "COM1")); p.push_back(Int32("BaudRate", 9600));
        p.push_back(Int32("DataBits", 8)); p.push_back(Int32("StopBits", 1));
        p.push_back(Int32("Parity", 0)); p.push_back(Int32("FlowControl", 0));
        p.push_back(Int32("DTRMode", -1)); p.push_back(Int32("RTSMode", -1));
        p.push_back(Int32("ReadTimeoutMs", 1000)); p.push_back(Int32("WriteTimeoutMs", 1000));
        p.push_back(Int32("FlushOnOpen", 1)); p.push_back(Int32("Logging", 0)); AddCommon(&p);
        if (!SaveAction("SRCSerial_start", p)) return false;

        p.clear(); AddCommon(&p); if (!SaveAction("SRCSerial_stop", p)) return false;
        p.clear(); p.push_back(Int32("BytesAvailable", 0, true)); AddCommon(&p);
        if (!SaveAction("SRCSerial_getBufferLength", p)) return false;

        p.clear(); p.push_back(Int32("RequestedCount", 0)); p.push_back(Int32("TimeoutMs", -1));
        p.push_back(Int32Array("Data", 4096, true)); p.push_back(Int32("BytesRead", 0, true));
        p.push_back(Int32("TimedOut", 0, true)); AddCommon(&p);
        if (!SaveAction("SRCSerial_readBytes", p)) return false;

        p.clear(); p.push_back(Int32("MaxChars", 1024)); p.push_back(String("Terminator", ""));
        p.push_back(Int32("TimeoutMs", -1)); p.push_back(Int32("IncludeTerminator", 0));
        p.push_back(String("Text", "", true)); p.push_back(Int32("BytesRead", 0, true));
        p.push_back(Int32("TimedOut", 0, true)); AddCommon(&p);
        if (!SaveAction("SRCSerial_readString", p)) return false;

        p.clear(); p.push_back(Int32Array("Data", 4096)); p.push_back(Int32("Count", 0));
        p.push_back(Int32("TimeoutMs", -1)); p.push_back(Int32("BytesWritten", 0, true)); AddCommon(&p);
        if (!SaveAction("SRCSerial_writeBytes", p)) return false;

        p.clear(); p.push_back(String("Text", "")); p.push_back(String("Suffix", ""));
        p.push_back(Int32("TimeoutMs", -1)); p.push_back(Int32("BytesWritten", 0, true)); AddCommon(&p);
        if (!SaveAction("SRCSerial_writeString", p)) return false;

        p.clear(); p.push_back(Int32("FlushMask", 3)); AddCommon(&p);
        if (!SaveAction("SRCSerial_flush", p)) return false;

        p.clear(); p.push_back(Int32("DTR", -1)); p.push_back(Int32("RTS", -1));
        p.push_back(Int32("Break", -1)); AddCommon(&p);
        if (!SaveAction("SRCSerial_setControlLines", p)) return false;

        p.clear(); p.push_back(Int32("CTS", 0, true)); p.push_back(Int32("DSR", 0, true));
        p.push_back(Int32("DCD", 0, true)); p.push_back(Int32("Ring", 0, true)); AddCommon(&p);
        if (!SaveAction("SRCSerial_getLineStatus", p)) return false;

        p.clear(); p.push_back(Int32("Open", 0, true)); p.push_back(String("Port", "", true));
        AddCommon(&p); if (!SaveAction("SRCSerial_isOpen", p)) return false;

        p.clear(); p.push_back(Int32("Open", 0, true)); p.push_back(String("Port", "", true));
        p.push_back(Int32("BaudRate", 0, true)); p.push_back(Int32("DataBits", 0, true));
        p.push_back(Int32("StopBits", 0, true)); p.push_back(Int32("Parity", 0, true));
        p.push_back(Int32("FlowControl", 0, true)); p.push_back(Int32("DTRMode", 0, true));
        p.push_back(Int32("RTSMode", 0, true)); p.push_back(Int32("ReadTimeoutMs", 0, true));
        p.push_back(Int32("WriteTimeoutMs", 0, true)); p.push_back(Int32("Logging", 0, true));
        AddCommon(&p); if (!SaveAction("SRCSerial_getConfiguration", p)) return false;

        p.clear(); p.push_back(Int32("ResetAfterRead", 0));
        p.push_back(Int32("FrameErrors", 0, true)); p.push_back(Int32("ParityErrors", 0, true));
        p.push_back(Int32("OverrunErrors", 0, true)); p.push_back(Int32("BufferOverrunErrors", 0, true));
        p.push_back(Int32("BreakCount", 0, true)); p.push_back(Int32("RxBytesQueued", 0, true));
        p.push_back(Int32("TxBytesQueued", 0, true)); p.push_back(Int32("TotalRxBytes", 0, true));
        p.push_back(Int32("TotalTxBytes", 0, true)); p.push_back(Int32("LastWin32Error", 0, true));
        AddCommon(&p); if (!SaveAction("SRCSerial_getDiagnostics", p)) return false;

        p.clear(); p.push_back(Int32("MaxBytes", 4096)); p.push_back(Int32("TimeoutMs", -1));
        p.push_back(Int32("InterByteTimeoutMs", 20)); p.push_back(Int32Array("Data", 4096, true));
        p.push_back(String("Hex", "", true)); p.push_back(Int32("BytesRead", 0, true));
        p.push_back(Int32("TimedOut", 0, true)); AddCommon(&p);
        if (!SaveAction("SRCSerial_readUntilIdle", p)) return false;

        p.clear(); p.push_back(Int32("RequestFormat", 0)); p.push_back(String("RequestText", ""));
        p.push_back(String("RequestSuffix", "")); p.push_back(String("RequestHex", ""));
        p.push_back(Int32Array("RequestData", 4096)); p.push_back(Int32("RequestCount", 0));
        p.push_back(Int32("FlushBeforeWrite", 1)); p.push_back(Int32("ResponseMode", 0));
        p.push_back(Int32("ResponseCount", 0)); p.push_back(String("Terminator", ""));
        p.push_back(Int32("TimeoutMs", -1)); p.push_back(Int32("InterByteTimeoutMs", 20));
        p.push_back(Int32("PreTransmitDelayMs", 0)); p.push_back(Int32("PostTransmitDelayMs", 0));
        p.push_back(Int32("Retries", 0)); p.push_back(Int32Array("ResponseData", 4096, true));
        p.push_back(String("ResponseHex", "", true)); p.push_back(Int32("BytesWritten", 0, true));
        p.push_back(Int32("BytesRead", 0, true)); p.push_back(Int32("TimedOut", 0, true));
        p.push_back(Int32("Attempts", 0, true)); AddCommon(&p);
        if (!SaveAction("SRCSerial_transact", p)) return false;

        p.clear(); p.push_back(String("Hex", "")); p.push_back(Int32("TimeoutMs", -1));
        p.push_back(Int32("BytesWritten", 0, true)); AddCommon(&p);
        if (!SaveAction("SRCSerial_writeHex", p)) return false;

        p.clear(); p.push_back(Int32("RequestedCount", 0)); p.push_back(Int32("MaxBytes", 1024));
        p.push_back(Int32("TimeoutMs", -1)); p.push_back(String("Hex", "", true));
        p.push_back(Int32("BytesRead", 0, true)); p.push_back(Int32("TimedOut", 0, true));
        AddCommon(&p); if (!SaveAction("SRCSerial_readHex", p)) return false;

        p.clear(); p.push_back(Int32("Line", 0)); p.push_back(Int32("State", 0));
        p.push_back(Int32("DurationMs", 100)); p.push_back(Int32("RestoreState", -1));
        AddCommon(&p); if (!SaveAction("SRCSerial_pulseControlLine", p)) return false;

        p.clear(); p.push_back(Int32("TimeoutMs", -1)); p.push_back(Int32("TimedOut", 0, true));
        AddCommon(&p); if (!SaveAction("SRCSerial_drainTransmit", p)) return false;

        p.clear(); p.push_back(String("Ports", "", true)); p.push_back(Int32("Count", 0, true));
        AddCommon(&p); if (!SaveAction("SRCSerial_enumeratePorts", p)) return false;

        p.clear(); AddCommon(&p); if (!SaveAction("SRCSerial_cancel", p)) return false;

        p.clear(); p.push_back(String("Port", "COM1"));
        p.push_back(Int32("Found", 0, true)); p.push_back(Int32("InterfaceMode", -1, true));
        p.push_back(Int32("TxMode", -1, true)); p.push_back(String("InstanceId", "", true));
        p.push_back(String("DriverVersion", "", true)); AddCommon(&p);
        if (!SaveAction("SRCSerial_getMoxaPortMode", p)) return false;

        p.clear(); p.push_back(String("Port", "COM1")); p.push_back(Int32("InterfaceMode", 0));
        p.push_back(String("ExpectedDriverVersion", "4.3.0.0"));
        p.push_back(Int32("AllowUnverifiedDriver", 0)); p.push_back(Int32("RestartDevice", 0));
        p.push_back(Int32("Found", 0, true)); p.push_back(Int32("PreviousMode", -1, true));
        p.push_back(Int32("CurrentMode", -1, true)); p.push_back(Int32("TxMode", -1, true));
        p.push_back(String("InstanceId", "", true)); p.push_back(String("DriverVersion", "", true));
        p.push_back(Int32("RegistryUpdated", 0, true)); p.push_back(Int32("RestartAttempted", 0, true));
        p.push_back(Int32("RestartSucceeded", 0, true)); p.push_back(Int32("RestartRequired", 0, true));
        AddCommon(&p); if (!SaveAction("SRCSerial_setMoxaPortMode", p)) return false;

        p.clear(); p.push_back(Int32("RxFrameLength", 1));
        p.push_back(Int32("RxIdOffset", 0)); p.push_back(Int32("RxIdValue", 0));
        p.push_back(Int32("RxIdMask", 0)); p.push_back(Int32("RxChecksumMode", 0));
        p.push_back(Int32("RxChecksumStart", 0)); p.push_back(Int32("RxChecksumLength", 0));
        p.push_back(Int32("RxChecksumOffset", -1)); p.push_back(Int32("RxQueueCapacity", 256));
        p.push_back(Int32("EventQueueCapacity", 512)); p.push_back(Int32("SilenceTimeoutMs", 1000));
        p.push_back(Int32("PollIntervalMs", 1)); p.push_back(Int32("MinimumInterTxMs", 0));
        p.push_back(Int32("WorkerPriority", 1)); p.push_back(Int32("WorkerRunning", 0, true));
        AddCommon(&p); if (!SaveAction("SRCSerial_workerStart", p)) return false;

        p.clear(); p.push_back(Int32("ClearState", 0)); p.push_back(Int32("WorkerRunning", 0, true));
        AddCommon(&p); if (!SaveAction("SRCSerial_workerStop", p)) return false;

        p.clear(); p.push_back(Int32("ResetCounters", 0));
        p.push_back(Int32("WorkerRunning", 0, true)); p.push_back(Int32("RxFrameCount", 0, true));
        p.push_back(Int32("ValidRxFrameCount", 0, true)); p.push_back(Int32("InvalidRxFrameCount", 0, true));
        p.push_back(Int32("ChecksumErrorCount", 0, true)); p.push_back(Int32("BadIdCount", 0, true));
        p.push_back(Int32("DroppedByteCount", 0, true)); p.push_back(Int32("TxFrameCount", 0, true));
        p.push_back(Int32("ResponseTxCount", 0, true)); p.push_back(Int32("CyclicTxCount", 0, true));
        p.push_back(Int32("ManualTxCount", 0, true)); p.push_back(Int32("RxSilenceTimeoutCount", 0, true));
        p.push_back(Int32("RxFramesQueued", 0, true)); p.push_back(Int32("EventsQueued", 0, true));
        p.push_back(Int32("PendingTxCount", 0, true)); p.push_back(Int32("LastRxAgeMs", -1, true));
        p.push_back(Int32("LastResponseLatencyUs", -1, true)); p.push_back(Int32("MaxResponseLatencyUs", 0, true));
        p.push_back(Int32("LastValidRxAgeMs", -1, true)); p.push_back(Int32("LastTxAgeMs", -1, true));
        p.push_back(Int32("WorkerLastErrorCode", 0, true)); p.push_back(String("WorkerLastErrorMessage", "", true));
        AddCommon(&p); if (!SaveAction("SRCSerial_workerGetStatus", p)) return false;

        p.clear(); p.push_back(Int32("MaxEvents", 20)); p.push_back(Int32("ClearAfterRead", 1));
        p.push_back(String("EventsText", "", true)); p.push_back(Int32("EventsReturned", 0, true));
        p.push_back(Int32("EventsRemaining", 0, true)); AddCommon(&p);
        if (!SaveAction("SRCSerial_workerReadEvents", p)) return false;

        p.clear(); p.push_back(String("Hex", "")); p.push_back(Int32("Mode", 0));
        p.push_back(Int32("QuietGapMs", 1)); p.push_back(Int32("Queued", 0, true));
        p.push_back(Int32("QueueDepth", 0, true)); AddCommon(&p);
        if (!SaveAction("SRCSerial_workerQueueTx", p)) return false;

        p.clear(); p.push_back(Int32("JobId", 1)); p.push_back(String("FrameHex", ""));
        p.push_back(Int32("PeriodMs", 1000)); p.push_back(Int32("InitialDelayMs", 0));
        p.push_back(Int32("Enabled", 1)); p.push_back(Int32("ChecksumMode", 0));
        p.push_back(Int32("ChecksumStart", 0)); p.push_back(Int32("ChecksumLength", 0));
        p.push_back(Int32("ChecksumOffset", -1)); p.push_back(String("AppliedHex", "", true));
        p.push_back(Int32("ActiveCycles", 0, true)); AddCommon(&p);
        if (!SaveAction("SRCSerial_cycleCreate", p)) return false;

        p.clear(); p.push_back(Int32("JobId", 1)); p.push_back(String("FrameHex", ""));
        p.push_back(Int32("ByteOffset", -1)); p.push_back(Int32("ByteValue", 0));
        p.push_back(Int32("PeriodMs", -1)); p.push_back(Int32("Enabled", -1));
        p.push_back(Int32("RecalculateChecksum", 1)); p.push_back(String("AppliedHex", "", true));
        p.push_back(Int32("ActiveCycles", 0, true)); AddCommon(&p);
        if (!SaveAction("SRCSerial_cycleUpdate", p)) return false;

        p.clear(); p.push_back(Int32("JobId", 1)); p.push_back(Int32("Found", 0, true));
        p.push_back(Int32("ActiveCycles", 0, true)); AddCommon(&p);
        if (!SaveAction("SRCSerial_cycleDestroy", p)) return false;

        p.clear(); p.push_back(Int32("JobId", 1)); p.push_back(String("FrameHex", ""));
        p.push_back(Int32("TriggerOffset", -1)); p.push_back(Int32("TriggerValue", 0));
        p.push_back(Int32("TriggerMask", 255)); p.push_back(Int32("ResponseMode", 1));
        p.push_back(Int32("ResponseDelayMs", 0)); p.push_back(Int32("QuietGapMs", 1));
        p.push_back(Int32("ReplacePending", 1)); p.push_back(Int32("Enabled", 1));
        p.push_back(Int32("TriggerSkipCount", 0)); p.push_back(Int32("SendCountLimit", 0));
        p.push_back(Int32("ChecksumMode", 0)); p.push_back(Int32("ChecksumStart", 0));
        p.push_back(Int32("ChecksumLength", 0)); p.push_back(Int32("ChecksumOffset", -1));
        p.push_back(String("AppliedHex", "", true)); p.push_back(Int32("ActiveResponses", 0, true));
        AddCommon(&p); if (!SaveAction("SRCSerial_responseCreate", p)) return false;

        p.clear(); p.push_back(Int32("JobId", 1)); p.push_back(String("FrameHex", ""));
        p.push_back(Int32("ByteOffset", -1)); p.push_back(Int32("ByteValue", 0));
        p.push_back(Int32("ResponseMode", -1)); p.push_back(Int32("ResponseDelayMs", -1));
        p.push_back(Int32("QuietGapMs", -1)); p.push_back(Int32("ReplacePending", -1));
        p.push_back(Int32("Enabled", -1)); p.push_back(Int32("ResetTriggerCounter", 0));
        p.push_back(Int32("RecalculateChecksum", 1));
        p.push_back(String("AppliedHex", "", true)); p.push_back(Int32("ActiveResponses", 0, true));
        AddCommon(&p); if (!SaveAction("SRCSerial_responseUpdate", p)) return false;

        p.clear(); p.push_back(Int32("JobId", 1)); p.push_back(Int32("Found", 0, true));
        p.push_back(Int32("ActiveResponses", 0, true)); AddCommon(&p);
        if (!SaveAction("SRCSerial_responseDestroy", p)) return false;

        p.clear(); p.push_back(Int32("FramesAvailable", 0, true));
        p.push_back(Int32("EventsAvailable", 0, true)); p.push_back(Int32("StreamBytes", 0, true));
        AddCommon(&p); if (!SaveAction("SRCSerial_rxGetCount", p)) return false;

        p.clear(); p.push_back(Int32("Remove", 1)); p.push_back(Int32Array("Data", 4096, true));
        p.push_back(Int32("Found", 0, true)); p.push_back(Int32("BytesRead", 0, true));
        p.push_back(String("Hex", "", true)); p.push_back(Int32("Sequence", 0, true));
        p.push_back(String("TimestampUtc", "", true)); p.push_back(Int32("AgeMs", -1, true));
        p.push_back(Int32("ChecksumValid", 0, true)); p.push_back(Int32("FramesRemaining", 0, true));
        AddCommon(&p); if (!SaveAction("SRCSerial_rxReadFrame", p)) return false;

        p.clear(); p.push_back(Int32("ClearFrames", 1)); p.push_back(Int32("ClearEvents", 1));
        p.push_back(Int32("ClearCounters", 0)); p.push_back(Int32("Cleared", 0, true));
        AddCommon(&p); return SaveAction("SRCSerial_rxClear", p);
    }

    bool Inspect(const char* fileName)
    {
        HUTAMDEF action = UtaMDefRestore(fileName);
        if (!action) return false;
        std::printf("name=%s class=%s library=%s\n", UtaMDefGetName(action),
            UtaMDefGetClassName(action), UtaMDefGetLibraryName(action));
        for (int id = UTA_ACT_NONE_ID; id <= UTA_MAX_ACT_ID; ++id)
        {
            const char* entry = UtaMDefGetActionEntryName(action, static_cast<IDUTAACTENT>(id));
            if (entry && entry[0]) std::printf("entry[%d]=%s\n", id, entry);
        }
        HUTAPBDEF pb = UtaMDefGetParmBlockDef(action);
        std::printf("parameters=%d\n", pb ? UtaPbDefGetSize(pb) : 0);
        UtaMDefRelease(action);
        return true;
    }

    bool Check(bool condition, const char* name)
    {
        std::printf("%s %s\n", condition ? "PASS" : "FAIL", name);
        return condition;
    }

    bool SelfTest()
    {
        bool ok = true;
        srcserial::Status status;
        ok &= Check(srcserial::NormalizePortName("COM1", &status) == "\\\\.\\COM1" && status.success, "normalize COM1");
        ok &= Check(srcserial::NormalizePortName("  com12 ", &status) == "\\\\.\\COM12" && status.success, "normalize COM12");
        srcserial::NormalizePortName("COM0", &status);
        ok &= Check(!status.success && status.code == srcserial::ErrorInvalidParameter, "reject COM0");
        std::vector<unsigned char> bytes;
        status = srcserial::DecodeEscapes("A\\r\\n\\x00\\\\", &bytes);
        const unsigned char expected[] = { 'A', '\r', '\n', 0, '\\' };
        ok &= Check(status.success && bytes.size() == sizeof(expected) && std::memcmp(&bytes[0], expected, sizeof(expected)) == 0, "decode escapes");
        status = srcserial::DecodeEscapes("\\q", &bytes);
        ok &= Check(!status.success, "reject bad escape");
        status = srcserial::DecodeHex("02 31:ff,0x7A", &bytes);
        const unsigned char expectedHex[] = { 0x02, 0x31, 0xff, 0x7a };
        ok &= Check(status.success && bytes.size() == sizeof(expectedHex) &&
            std::memcmp(&bytes[0], expectedHex, sizeof(expectedHex)) == 0, "decode hex");
        ok &= Check(srcserial::FormatHex(expectedHex, sizeof(expectedHex)) ==
            "02 31 FF 7A", "format hex");
        status = srcserial::DecodeHex("0", &bytes);
        ok &= Check(!status.success && status.code == srcserial::ErrorInvalidHex,
            "reject incomplete hex");
        const unsigned char jlgBytes[] = { 0x74, 0x0A, 0x00, 0x00 };
        bytes.assign(jlgBytes, jlgBytes + sizeof(jlgBytes));
        status = srcserial::ApplyFrameChecksum(&bytes, 1, 0, 3, 3);
        ok &= Check(status.success && bytes[3] == 0x81,
            "calculate JLG one's-complement checksum");
        status = srcserial::ApplyFrameChecksum(&bytes, 1, 0, 4, 3);
        ok &= Check(!status.success && status.code == srcserial::ErrorWorkerChecksum,
            "reject checksum byte inside payload range");
        bool open = true;
        std::string port;
        status = srcserial::IsOpen(&open, &port);
        ok &= Check(status.success && !open && port.empty(), "closed session query");
        srcserial::Diagnostics diagnostics;
        status = srcserial::GetDiagnostics(&diagnostics, false);
        ok &= Check(status.success && diagnostics.totalRxBytes == 0,
            "closed diagnostics query");
        srcserial::PortConfig invalidConfig;
        invalidConfig.dataBits = 8;
        invalidConfig.stopBits = 15;
        status = srcserial::Start(invalidConfig);
        ok &= Check(!status.success && status.code == srcserial::ErrorInvalidParameter,
            "reject invalid framing combination");
        std::vector<unsigned char> idleData;
        bool timedOut = false;
        status = srcserial::ReadUntilIdle(16, 10, 2, &idleData, &timedOut);
        ok &= Check(!status.success && status.code == srcserial::ErrorNotOpen,
            "idle read requires open session");
        status = srcserial::DrainTransmit(10, &timedOut);
        ok &= Check(!status.success && status.code == srcserial::ErrorNotOpen,
            "drain requires open session");
        ok &= Check(srcserial::CancelPending().success, "cancel is safe while closed");
        srcserial::WorkerConfig workerConfig;
        workerConfig.rxFrameLength = 6;
        workerConfig.rxIdValue = 0x6A;
        workerConfig.rxIdMask = 0xFF;
        workerConfig.rxChecksumMode = 1;
        workerConfig.rxChecksumStart = 0;
        workerConfig.rxChecksumLength = 5;
        workerConfig.rxChecksumOffset = 5;
        status = srcserial::WorkerStart(workerConfig);
        ok &= Check(!status.success && status.code == srcserial::ErrorNotOpen,
            "worker requires open serial session");
        DWORD queueDepth = 0;
        status = srcserial::WorkerQueueTx(bytes, 0, 1, &queueDepth);
        ok &= Check(!status.success && status.code == srcserial::ErrorWorkerNotRunning,
            "manual worker TX requires running worker");
        std::string ports;
        DWORD portCount = 0;
        status = srcserial::EnumeratePorts(&ports, &portCount);
        ok &= Check(status.success, "enumerate present COM ports");
        srcserial::MoxaPortMode moxaMode;
        status = srcserial::GetMoxaPortMode("COM0", &moxaMode);
        ok &= Check(!status.success && status.code == srcserial::ErrorInvalidParameter,
            "reject invalid Moxa COM name");
        status = srcserial::SetMoxaPortMode("COM1", 4, "4.3.0.0", false,
            false, &moxaMode);
        ok &= Check(!status.success && status.code == srcserial::ErrorInvalidParameter,
            "reject invalid Moxa interface mode");
        DWORD available = 123;
        status = srcserial::GetBufferLength(&available);
        ok &= Check(!status.success && status.code == srcserial::ErrorNotOpen && available == 0, "closed port status");
        ok &= Check(srcserial::Stop().success, "idempotent stop");
        return ok;
    }

    bool ActionSmoke(const char* dllPath)
    {
        typedef void (WINAPI *ActionRoutine)(HUTAPB);
        HMODULE dll = LoadLibraryA(dllPath);
        if (!Check(dll != NULL, "load SRCSerial.dll")) return false;
        ActionRoutine stop = reinterpret_cast<ActionRoutine>(GetProcAddress(dll, "SRCSerial_stop"));
        ActionRoutine getLength = reinterpret_cast<ActionRoutine>(GetProcAddress(dll, "SRCSerial_getBufferLength"));
        const char* exports[] = {
            "SRCSerial_start", "SRCSerial_stop", "SRCSerial_getBufferLength",
            "SRCSerial_readBytes", "SRCSerial_readString", "SRCSerial_writeBytes",
            "SRCSerial_writeString", "SRCSerial_flush", "SRCSerial_setControlLines",
            "SRCSerial_getLineStatus", "SRCSerial_isOpen", "SRCSerial_getConfiguration",
            "SRCSerial_getDiagnostics", "SRCSerial_readUntilIdle", "SRCSerial_transact",
            "SRCSerial_writeHex", "SRCSerial_readHex", "SRCSerial_pulseControlLine",
            "SRCSerial_drainTransmit", "SRCSerial_enumeratePorts", "SRCSerial_cancel",
            "SRCSerial_getMoxaPortMode", "SRCSerial_setMoxaPortMode",
            "SRCSerial_workerStart", "SRCSerial_workerStop",
            "SRCSerial_workerGetStatus", "SRCSerial_workerReadEvents",
            "SRCSerial_workerQueueTx", "SRCSerial_cycleCreate",
            "SRCSerial_cycleUpdate", "SRCSerial_cycleDestroy",
            "SRCSerial_responseCreate", "SRCSerial_responseUpdate",
            "SRCSerial_responseDestroy", "SRCSerial_rxGetCount",
            "SRCSerial_rxReadFrame", "SRCSerial_rxClear"
        };
        bool allExports = true;
        for (size_t i = 0; i < sizeof(exports) / sizeof(exports[0]); ++i)
            allExports &= GetProcAddress(dll, exports[i]) != NULL;
        bool ok = Check(stop != NULL && getLength != NULL && allExports,
            "resolve all action exports");
        HUTAPBDEF definition = UtaPbDefCreate();
        std::vector<ParmSpec> parms;
        parms.push_back(Int32("BytesAvailable", 99, true)); AddCommon(&parms);
        for (size_t i = 0; i < parms.size(); ++i) AddParm(definition, parms[i]);
        HUTAPB block = UtaPbDefCreateParameterBlock(definition, definition);
        UtaPbBind(block);
        if (ok)
        {
            getLength(block);
            const long success = UtaInt32GetValue(reinterpret_cast<HUTAINT32>(UtaPbFindData(block, "Success")));
            const long code = UtaInt32GetValue(reinterpret_cast<HUTAINT32>(UtaPbFindData(block, "ErrorCode")));
            ok &= Check(success == 0 && code == srcserial::ErrorNotOpen, "closed action reports structured error");
            stop(block);
            const long stopSuccess = UtaInt32GetValue(reinterpret_cast<HUTAINT32>(UtaPbFindData(block, "Success")));
            ok &= Check(stopSuccess == 1, "stop action is idempotent");
        }
        UtaPbUnBind(block);
        UtaPbRelease(block);
        UtaPbDefRelease(definition);

        ActionRoutine getMoxa = reinterpret_cast<ActionRoutine>(
            GetProcAddress(dll, "SRCSerial_getMoxaPortMode"));
        ActionRoutine setMoxa = reinterpret_cast<ActionRoutine>(
            GetProcAddress(dll, "SRCSerial_setMoxaPortMode"));
        HUTAPBDEF moxaDefinition = UtaPbDefCreate();
        std::vector<ParmSpec> moxaParms;
        moxaParms.push_back(String("Port", "COM1"));
        moxaParms.push_back(Int32("Found", 0, true));
        moxaParms.push_back(Int32("InterfaceMode", -1, true));
        moxaParms.push_back(Int32("TxMode", -1, true));
        moxaParms.push_back(String("InstanceId", "", true));
        moxaParms.push_back(String("DriverVersion", "", true));
        AddCommon(&moxaParms);
        for (size_t i = 0; i < moxaParms.size(); ++i)
            AddParm(moxaDefinition, moxaParms[i]);
        HUTAPB moxaBlock = UtaPbDefCreateParameterBlock(moxaDefinition,
            moxaDefinition);
        UtaPbBind(moxaBlock);
        getMoxa(moxaBlock);
        const long moxaSuccess = UtaInt32GetValue(reinterpret_cast<HUTAINT32>(
            UtaPbFindData(moxaBlock, "Success")));
        const long moxaCode = UtaInt32GetValue(reinterpret_cast<HUTAINT32>(
            UtaPbFindData(moxaBlock, "ErrorCode")));
        const long moxaFound = UtaInt32GetValue(reinterpret_cast<HUTAINT32>(
            UtaPbFindData(moxaBlock, "Found")));
        ok &= Check((moxaSuccess == 1 && moxaFound == 1) ||
            (moxaSuccess == 0 && moxaCode == srcserial::ErrorMoxaPortNotFound),
            "Moxa query action reports matched or absent adapter");
        UtaPbUnBind(moxaBlock);
        UtaPbRelease(moxaBlock);
        UtaPbDefRelease(moxaDefinition);

        HUTAPBDEF setMoxaDefinition = UtaPbDefCreate();
        std::vector<ParmSpec> setMoxaParms;
        setMoxaParms.push_back(String("Port", "COM999"));
        setMoxaParms.push_back(Int32("InterfaceMode", 2));
        setMoxaParms.push_back(String("ExpectedDriverVersion",
            "intentionally-wrong-version"));
        setMoxaParms.push_back(Int32("AllowUnverifiedDriver", 0));
        setMoxaParms.push_back(Int32("RestartDevice", 0));
        setMoxaParms.push_back(Int32("Found", 0, true));
        setMoxaParms.push_back(Int32("PreviousMode", -1, true));
        setMoxaParms.push_back(Int32("CurrentMode", -1, true));
        setMoxaParms.push_back(Int32("TxMode", -1, true));
        setMoxaParms.push_back(String("InstanceId", "", true));
        setMoxaParms.push_back(String("DriverVersion", "", true));
        setMoxaParms.push_back(Int32("RegistryUpdated", 0, true));
        setMoxaParms.push_back(Int32("RestartAttempted", 0, true));
        setMoxaParms.push_back(Int32("RestartSucceeded", 0, true));
        setMoxaParms.push_back(Int32("RestartRequired", 0, true));
        AddCommon(&setMoxaParms);
        for (size_t i = 0; i < setMoxaParms.size(); ++i)
            AddParm(setMoxaDefinition, setMoxaParms[i]);
        HUTAPB setMoxaBlock = UtaPbDefCreateParameterBlock(setMoxaDefinition,
            setMoxaDefinition);
        UtaPbBind(setMoxaBlock);
        setMoxa(setMoxaBlock);
        const long retainedInterfaceMode = UtaInt32GetValue(
            reinterpret_cast<HUTAINT32>(UtaPbFindData(setMoxaBlock,
                "InterfaceMode")));
        const long setMoxaCode = UtaInt32GetValue(reinterpret_cast<HUTAINT32>(
            UtaPbFindData(setMoxaBlock, "ErrorCode")));
        ok &= Check(retainedInterfaceMode == 2,
            "Moxa set action preserves InterfaceMode input");
        ok &= Check(setMoxaCode != srcserial::ErrorInvalidParameter,
            "Moxa set action accepts InterfaceMode 2");
        UtaPbUnBind(setMoxaBlock);
        UtaPbRelease(setMoxaBlock);
        UtaPbDefRelease(setMoxaDefinition);
        FreeLibrary(dll);
        return ok;
    }

    bool MoxaProbe(const char* port)
    {
        srcserial::MoxaPortMode mode;
        const srcserial::Status status = srcserial::GetMoxaPortMode(port, &mode);
        if (!status.success)
        {
            std::fprintf(stderr, "Moxa probe failed: %ld %s\n", status.code,
                status.message.c_str());
            return false;
        }
        std::printf("port=%s found=%d interfaceMode=%ld txMode=%ld instance=%s driver=%s\n",
            port, mode.found ? 1 : 0, mode.interfaceMode, mode.txMode,
            mode.instanceId.c_str(), mode.driverVersion.c_str());
        srcserial::MoxaPortMode guarded;
        srcserial::Status guardedStatus = srcserial::SetMoxaPortMode(port,
            mode.interfaceMode, "intentionally-wrong-version", false, false,
            &guarded);
        if (guardedStatus.success ||
            guardedStatus.code != srcserial::ErrorMoxaDriverMismatch)
        {
            std::fprintf(stderr, "Moxa driver-version guard did not reject mismatch\n");
            return false;
        }
        guardedStatus = srcserial::SetMoxaPortMode(port, mode.interfaceMode,
            mode.driverVersion.c_str(), false, false, &guarded);
        if (!guardedStatus.success || guarded.registryUpdated)
        {
            std::fprintf(stderr, "Moxa no-change guarded set failed: %ld %s\n",
                guardedStatus.code, guardedStatus.message.c_str());
            return false;
        }
        std::printf("driverVersionGuard=pass noChangeSet=pass\n");
        return true;
    }
}

int main(int argc, char** argv)
{
    UtaCoreWakeUp("SRCSerialTools", NULL, NULL);
    srcserial::Initialize(GetModuleHandleA(NULL));
    srcserial::InitializeWorker();
    int result = 1;
    if (argc >= 2 && std::strcmp(argv[1], "generate") == 0) result = GenerateActions() ? 0 : 1;
    else if (argc >= 3 && std::strcmp(argv[1], "inspect") == 0) result = Inspect(argv[2]) ? 0 : 1;
    else if (argc >= 2 && std::strcmp(argv[1], "self-test") == 0) result = SelfTest() ? 0 : 1;
    else if (argc >= 3 && std::strcmp(argv[1], "action-smoke") == 0) result = ActionSmoke(argv[2]) ? 0 : 1;
    else if (argc >= 3 && std::strcmp(argv[1], "moxa-probe") == 0) result = MoxaProbe(argv[2]) ? 0 : 1;
    else std::fprintf(stderr, "usage: SRCSerialTools generate | inspect <umd> | self-test | action-smoke <dll> | moxa-probe <COMn>\n");
    srcserial::ShutdownWorker(false);
    srcserial::Shutdown();
    UtaCoreShutDown();
    return result;
}
