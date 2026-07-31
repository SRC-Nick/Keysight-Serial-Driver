#include "SRCSerial_Internal.h"

#include <devguid.h>
#include <setupapi.h>

#include <cstring>

namespace srcserial
{
    namespace
    {
        const char MoxaPortsRoot[] =
            "SYSTEM\\CurrentControlSet\\Enum\\MXUPORT\\COM";
        SRWLOCK g_moxaLock = SRWLOCK_INIT;

        class ScopedMoxaLock
        {
        public:
            ScopedMoxaLock() { AcquireSRWLockExclusive(&g_moxaLock); }
            ~ScopedMoxaLock() { ReleaseSRWLockExclusive(&g_moxaLock); }
        private:
            ScopedMoxaLock(const ScopedMoxaLock&);
            ScopedMoxaLock& operator=(const ScopedMoxaLock&);
        };

        Status ReadStringValue(HKEY key, const char* name, std::string* value)
        {
            char buffer[2048] = { 0 };
            DWORD type = 0;
            DWORD size = sizeof(buffer);
            const LONG result = RegQueryValueExA(key, name, NULL, &type,
                reinterpret_cast<LPBYTE>(buffer), &size);
            if (result != ERROR_SUCCESS)
                return Status::Win32Error(name, static_cast<DWORD>(result));
            if (type != REG_SZ && type != REG_EXPAND_SZ)
                return Status::Validation(ErrorMoxaRegistry,
                    "Moxa registry string has an unexpected type");
            buffer[sizeof(buffer) - 1] = 0;
            *value = buffer;
            return Status::Ok();
        }

        Status ReadDwordValue(HKEY key, const char* name, DWORD* value)
        {
            DWORD type = 0;
            DWORD size = sizeof(*value);
            const LONG result = RegQueryValueExA(key, name, NULL, &type,
                reinterpret_cast<LPBYTE>(value), &size);
            if (result != ERROR_SUCCESS)
                return Status::Win32Error(name, static_cast<DWORD>(result));
            if (type != REG_DWORD || size != sizeof(*value))
                return Status::Validation(ErrorMoxaRegistry,
                    "Moxa registry mode has an unexpected type");
            return Status::Ok();
        }

        std::string ReadDriverVersion(const std::string& instanceName)
        {
            const std::string instancePath = std::string(MoxaPortsRoot) + "\\" +
                instanceName;
            HKEY instanceKey = NULL;
            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, instancePath.c_str(), 0,
                KEY_QUERY_VALUE | KEY_WOW64_64KEY, &instanceKey) != ERROR_SUCCESS)
                return "";
            std::string driverPath;
            Status status = ReadStringValue(instanceKey, "Driver", &driverPath);
            RegCloseKey(instanceKey);
            if (!status.success || driverPath.empty()) return "";

            const std::string classPath =
                "SYSTEM\\CurrentControlSet\\Control\\Class\\" + driverPath;
            HKEY classKey = NULL;
            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, classPath.c_str(), 0,
                KEY_QUERY_VALUE | KEY_WOW64_64KEY, &classKey) != ERROR_SUCCESS)
                return "";
            std::string version;
            status = ReadStringValue(classKey, "DriverVersion", &version);
            RegCloseKey(classKey);
            return status.success ? version : "";
        }

        Status FindMoxaPort(const char* port, MoxaPortMode* mode,
            std::string* instanceName)
        {
            Status normalization;
            const std::string normalized = NormalizePortName(port, &normalization);
            if (!normalization.success) return normalization;
            const std::string canonical = normalized.substr(4);

            HKEY root = NULL;
            LONG result = RegOpenKeyExA(HKEY_LOCAL_MACHINE, MoxaPortsRoot, 0,
                KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE | KEY_WOW64_64KEY, &root);
            if (result == ERROR_FILE_NOT_FOUND)
                return Status::Validation(ErrorMoxaPortNotFound,
                    "Moxa MXUPORT registry root was not found");
            if (result != ERROR_SUCCESS)
                return Status::Win32Error("RegOpenKeyEx(MXUPORT)",
                    static_cast<DWORD>(result));

            Status status = Status::Validation(ErrorMoxaPortNotFound,
                "No Moxa UPort instance matches the requested COM port");
            for (DWORD index = 0;; ++index)
            {
                char childName[512] = { 0 };
                DWORD childLength = sizeof(childName);
                result = RegEnumKeyExA(root, index, childName, &childLength,
                    NULL, NULL, NULL, NULL);
                if (result == ERROR_NO_MORE_ITEMS) break;
                if (result != ERROR_SUCCESS)
                {
                    status = Status::Win32Error("RegEnumKeyEx(MXUPORT)",
                        static_cast<DWORD>(result));
                    break;
                }

                const std::string parametersPath = std::string(childName) +
                    "\\Device Parameters";
                HKEY parameters = NULL;
                result = RegOpenKeyExA(root, parametersPath.c_str(), 0,
                    KEY_QUERY_VALUE | KEY_WOW64_64KEY, &parameters);
                if (result != ERROR_SUCCESS) continue;
                std::string portName;
                Status portStatus = ReadStringValue(parameters, "PortName", &portName);
                if (portStatus.success && _stricmp(portName.c_str(), canonical.c_str()) == 0)
                {
                    mode->found = true;
                    *instanceName = childName;
                    mode->instanceId = std::string("MXUPORT\\COM\\") + childName;
                    mode->driverVersion = ReadDriverVersion(childName);
                    DWORD interfaceMode = 0;
                    DWORD txMode = 0;
                    status = ReadDwordValue(parameters, "SerInterface", &interfaceMode);
                    if (status.success)
                        status = ReadDwordValue(parameters, "TxMode", &txMode);
                    if (status.success && interfaceMode > 3)
                        status = Status::Validation(ErrorMoxaRegistry,
                            "Moxa SerInterface is outside the observed 0..3 range");
                    if (status.success)
                    {
                        mode->previousInterfaceMode = static_cast<long>(interfaceMode);
                        mode->interfaceMode = static_cast<long>(interfaceMode);
                        mode->txMode = static_cast<long>(txMode);
                    }
                    RegCloseKey(parameters);
                    break;
                }
                RegCloseKey(parameters);
            }
            RegCloseKey(root);
            return status;
        }

        Status RestartMoxaPort(const std::string& instanceId,
            bool* restartRequired)
        {
            *restartRequired = true;
            HDEVINFO devices = SetupDiGetClassDevsA(&GUID_DEVCLASS_PORTS,
                NULL, NULL, DIGCF_PRESENT);
            if (devices == INVALID_HANDLE_VALUE)
                return Status::Win32Error("SetupDiGetClassDevs",
                    GetLastError());

            Status status = Status::Win32Error("SetupDiEnumDeviceInfo",
                ERROR_NOT_FOUND);
            for (DWORD index = 0;; ++index)
            {
                SP_DEVINFO_DATA device = { 0 };
                device.cbSize = sizeof(device);
                if (!SetupDiEnumDeviceInfo(devices, index, &device))
                {
                    const DWORD error = GetLastError();
                    if (error != ERROR_NO_MORE_ITEMS)
                        status = Status::Win32Error("SetupDiEnumDeviceInfo", error);
                    break;
                }
                char currentId[2048] = { 0 };
                if (!SetupDiGetDeviceInstanceIdA(devices, &device, currentId,
                    sizeof(currentId), NULL))
                    continue;
                if (_stricmp(currentId, instanceId.c_str()) != 0) continue;

                SP_PROPCHANGE_PARAMS change = { 0 };
                change.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
                change.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
                change.StateChange = DICS_PROPCHANGE;
                change.Scope = DICS_FLAG_GLOBAL;
                change.HwProfile = 0;
                if (!SetupDiSetClassInstallParamsA(devices, &device,
                    &change.ClassInstallHeader, sizeof(change)))
                {
                    status = Status::Win32Error("SetupDiSetClassInstallParams",
                        GetLastError());
                    break;
                }
                if (!SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, devices, &device))
                {
                    status = Status::Win32Error("SetupDiCallClassInstaller",
                        GetLastError());
                    break;
                }
                SP_DEVINSTALL_PARAMS_A install = { 0 };
                install.cbSize = sizeof(install);
                if (!SetupDiGetDeviceInstallParamsA(devices, &device, &install))
                {
                    status = Status::Win32Error("SetupDiGetDeviceInstallParams",
                        GetLastError());
                    break;
                }
                *restartRequired = (install.Flags & (DI_NEEDRESTART | DI_NEEDREBOOT)) != 0;
                status = Status::Ok();
                break;
            }
            SetupDiDestroyDeviceInfoList(devices);
            return status;
        }
    }

    MoxaPortMode::MoxaPortMode()
        : found(false), previousInterfaceMode(-1), interfaceMode(-1), txMode(-1), registryUpdated(false),
          restartAttempted(false), restartSucceeded(false), restartRequired(false)
    {
    }

    Status GetMoxaPortMode(const char* port, MoxaPortMode* mode)
    {
        if (!mode)
            return Status::Validation(ErrorInvalidParameter,
                "Moxa mode output is required");
        *mode = MoxaPortMode();
        ScopedMoxaLock lock;
        std::string instanceName;
        return FindMoxaPort(port, mode, &instanceName);
    }

    Status SetMoxaPortMode(const char* port, long interfaceMode,
        const char* expectedDriverVersion, bool allowUnverifiedDriver,
        bool restartDevice, MoxaPortMode* mode)
    {
        if (!mode)
            return Status::Validation(ErrorInvalidParameter,
                "Moxa mode output is required");
        if (interfaceMode < 0 || interfaceMode > 3)
            return Status::Validation(ErrorInvalidParameter,
                "InterfaceMode must be 0 through 3");

        *mode = MoxaPortMode();
        ScopedMoxaLock lock;
        std::string instanceName;
        Status status = FindMoxaPort(port, mode, &instanceName);
        if (!status.success) return status;

        const std::string expected = expectedDriverVersion ?
            expectedDriverVersion : "";
        if (!allowUnverifiedDriver &&
            (expected.empty() || _stricmp(expected.c_str(),
                mode->driverVersion.c_str()) != 0))
            return Status::Validation(ErrorMoxaDriverMismatch,
                "Moxa driver version does not match ExpectedDriverVersion");

        bool serialOpen = false;
        std::string openPort;
        status = IsOpen(&serialOpen, &openPort);
        if (!status.success) return status;
        Status normalization;
        const std::string normalized = NormalizePortName(port, &normalization);
        if (!normalization.success) return normalization;
        if (serialOpen && _stricmp(openPort.c_str(), normalized.substr(4).c_str()) == 0)
            return Status::Validation(ErrorPortInUse,
                "Close the serial session before changing its Moxa interface mode");

        const long previousMode = mode->interfaceMode;
        if (previousMode != interfaceMode)
        {
            const std::string parametersPath = std::string(MoxaPortsRoot) + "\\" +
                instanceName + "\\Device Parameters";
            HKEY parameters = NULL;
            LONG result = RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                parametersPath.c_str(), 0,
                KEY_QUERY_VALUE | KEY_SET_VALUE | KEY_WOW64_64KEY, &parameters);
            if (result != ERROR_SUCCESS)
                return Status::Win32Error("RegOpenKeyEx(Moxa write)",
                    static_cast<DWORD>(result));
            DWORD currentValue = 0;
            status = ReadDwordValue(parameters, "SerInterface", &currentValue);
            if (status.success)
            {
                const DWORD newValue = static_cast<DWORD>(interfaceMode);
                result = RegSetValueExA(parameters, "SerInterface", 0,
                    REG_DWORD, reinterpret_cast<const BYTE*>(&newValue),
                    sizeof(newValue));
                if (result != ERROR_SUCCESS)
                    status = Status::Win32Error("RegSetValueEx(SerInterface)",
                        static_cast<DWORD>(result));
                else
                {
                    mode->registryUpdated = true;
                    mode->restartRequired = true;
                }
            }
            if (status.success)
            {
                DWORD verified = 0;
                status = ReadDwordValue(parameters, "SerInterface", &verified);
                if (status.success)
                {
                    mode->interfaceMode = static_cast<long>(verified);
                    if (verified != static_cast<DWORD>(interfaceMode))
                        status = Status::Validation(ErrorMoxaRegistry,
                            "Moxa SerInterface verification failed");
                }
            }
            RegCloseKey(parameters);
            if (!status.success) return status;
        }

        if (restartDevice)
        {
            mode->restartAttempted = true;
            bool stillRequired = true;
            status = RestartMoxaPort(mode->instanceId, &stillRequired);
            mode->restartSucceeded = status.success;
            mode->restartRequired = stillRequired;
            if (!status.success) return status;
        }
        return Status::Ok();
    }
}
