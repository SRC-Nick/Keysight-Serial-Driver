#include "SRCSerial_Internal.h"

namespace srcserial
{
    bool HasParameter(HUTAPB block, const char* name)
    {
        return block && name && UtaPbFindData(block, name) != NULL;
    }

    long GetInt32(HUTAPB block, const char* name, long defaultValue)
    {
        if (!HasParameter(block, name))
        {
            return defaultValue;
        }
        long value = defaultValue;
        UtaPbGetInt32(block, name, &value);
        return value;
    }

    std::string GetString(HUTAPB block, const char* name, const char* defaultValue)
    {
        if (!HasParameter(block, name))
        {
            return defaultValue ? defaultValue : "";
        }
        char value[4096] = { 0 };
        UtaPbGetString(block, name, value, static_cast<int>(sizeof(value)));
        return value;
    }

    HUTAI32ARR GetInt32Array(HUTAPB block, const char* name)
    {
        return reinterpret_cast<HUTAI32ARR>(block ? UtaPbFindData(block, name) : NULL);
    }

    DWORD GetArrayCapacity(HUTAI32ARR array)
    {
        return array ? static_cast<DWORD>(UtaArrayGetSize(reinterpret_cast<HUTADATA>(array))) : 0;
    }

    void SetInt32(HUTAPB block, const char* name, long value)
    {
        HUTADATA data = block ? UtaPbFindData(block, name) : NULL;
        if (data)
        {
            UtaInt32SetValue(reinterpret_cast<HUTAINT32>(data), static_cast<UTAINT32>(value));
        }
    }

    void SetString(HUTAPB block, const char* name, const char* value)
    {
        HUTADATA data = block ? UtaPbFindData(block, name) : NULL;
        if (data)
        {
            UtaStringSetValue(reinterpret_cast<HUTASTRING>(data), value ? value : "");
        }
    }

    void SetStatus(HUTAPB block, const Status& status)
    {
        SetInt32(block, "Success", status.success ? 1 : 0);
        SetInt32(block, "ErrorCode", status.code);
        SetString(block, "ErrorMessage", status.message.c_str());
    }

    void ClearStatus(HUTAPB block)
    {
        SetStatus(block, Status::Ok());
    }
}
