#include "SRCSerial_Internal.h"

#include <devguid.h>
#include <setupapi.h>

#include <algorithm>
#include <sstream>

namespace srcserial
{
    namespace
    {
        std::string DeviceProperty(HDEVINFO devices, SP_DEVINFO_DATA* device, DWORD property)
        {
            char value[2048] = { 0 };
            DWORD type = 0;
            if (!SetupDiGetDeviceRegistryPropertyA(devices, device, property,
                &type, reinterpret_cast<PBYTE>(value), sizeof(value), NULL))
                return "";
            return value;
        }

        std::string DeviceInstanceId(HDEVINFO devices, SP_DEVINFO_DATA* device)
        {
            char value[2048] = { 0 };
            if (!SetupDiGetDeviceInstanceIdA(devices, device, value, sizeof(value), NULL))
                return "";
            return value;
        }

        std::string PortName(HDEVINFO devices, SP_DEVINFO_DATA* device)
        {
            HKEY key = SetupDiOpenDevRegKey(devices, device, DICS_FLAG_GLOBAL,
                0, DIREG_DEV, KEY_QUERY_VALUE);
            if (key == INVALID_HANDLE_VALUE) return "";
            char value[256] = { 0 };
            DWORD type = 0;
            DWORD size = sizeof(value);
            const LONG result = RegQueryValueExA(key, "PortName", NULL, &type,
                reinterpret_cast<LPBYTE>(value), &size);
            RegCloseKey(key);
            return result == ERROR_SUCCESS && type == REG_SZ ? value : "";
        }

        std::string CleanField(const std::string& value)
        {
            std::string result = value;
            for (size_t i = 0; i < result.size(); ++i)
            {
                if (result[i] == '|' || result[i] == '\r' || result[i] == '\n')
                    result[i] = ' ';
            }
            return result;
        }
    }

    Status EnumeratePorts(std::string* result, DWORD* count)
    {
        if (!result || !count)
            return Status::Validation(ErrorInvalidParameter, "Port list outputs are required");
        result->clear();
        *count = 0;
        HDEVINFO devices = SetupDiGetClassDevsA(&GUID_DEVCLASS_PORTS, NULL, NULL,
            DIGCF_PRESENT);
        if (devices == INVALID_HANDLE_VALUE)
            return Status::Win32Error("SetupDiGetClassDevs", GetLastError());

        std::vector<std::string> rows;
        for (DWORD index = 0;; ++index)
        {
            SP_DEVINFO_DATA device = { 0 };
            device.cbSize = sizeof(device);
            if (!SetupDiEnumDeviceInfo(devices, index, &device))
            {
                const DWORD error = GetLastError();
                if (error == ERROR_NO_MORE_ITEMS) break;
                SetupDiDestroyDeviceInfoList(devices);
                return Status::Win32Error("SetupDiEnumDeviceInfo", error);
            }
            const std::string port = PortName(devices, &device);
            if (port.size() < 4 || _strnicmp(port.c_str(), "COM", 3) != 0) continue;
            std::ostringstream row;
            row << CleanField(port) << '|'
                << CleanField(DeviceProperty(devices, &device, SPDRP_FRIENDLYNAME)) << '|'
                << CleanField(DeviceProperty(devices, &device, SPDRP_HARDWAREID)) << '|'
                << CleanField(DeviceInstanceId(devices, &device)) << '|'
                << CleanField(DeviceProperty(devices, &device, SPDRP_LOCATION_INFORMATION));
            rows.push_back(row.str());
        }
        SetupDiDestroyDeviceInfoList(devices);
        std::sort(rows.begin(), rows.end());
        std::ostringstream output;
        for (size_t i = 0; i < rows.size(); ++i)
        {
            if (i) output << "\r\n";
            output << rows[i];
        }
        *result = output.str();
        *count = static_cast<DWORD>(rows.size());
        if (result->size() > 30000)
        {
            result->clear();
            *count = 0;
            return Status::Validation(ErrorOutputTooLarge, "Enumerated port list exceeds 30000 characters");
        }
        return Status::Ok();
    }
}
