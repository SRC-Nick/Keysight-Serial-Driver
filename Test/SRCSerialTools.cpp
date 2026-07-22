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
        return SaveAction("SRCSerial_getLineStatus", p);
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
        bool ok = Check(stop != NULL && getLength != NULL, "resolve action exports");
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
        FreeLibrary(dll);
        return ok;
    }
}

int main(int argc, char** argv)
{
    UtaCoreWakeUp("SRCSerialTools", NULL, NULL);
    int result = 1;
    if (argc >= 2 && std::strcmp(argv[1], "generate") == 0) result = GenerateActions() ? 0 : 1;
    else if (argc >= 3 && std::strcmp(argv[1], "inspect") == 0) result = Inspect(argv[2]) ? 0 : 1;
    else if (argc >= 2 && std::strcmp(argv[1], "self-test") == 0) result = SelfTest() ? 0 : 1;
    else if (argc >= 3 && std::strcmp(argv[1], "action-smoke") == 0) result = ActionSmoke(argv[2]) ? 0 : 1;
    else std::fprintf(stderr, "usage: SRCSerialTools generate | inspect <umd> | self-test | action-smoke <dll>\n");
    UtaCoreShutDown();
    return result;
}
