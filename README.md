# SRCSerial

`SRCSerial.dll` is a 32-bit Keysight/Agilent TestExec SL action library for
serial devices exposed as Windows COM ports. It supports RS-232, RS-422, and
RS-485 USB adapters without linking to a vendor SDK.

The driver targets TestExec SL 7.1 and Windows 7 SP1. The recommended adapter
is the optically isolated Moxa UPort 1150I. Its Windows driver selects the
electrical interface and performs automatic RS-485 direction control;
`SRCSerial.dll` configures and uses the resulting COM port.

## Contents

- `SRCSerial.sln` / `SRCSerial.vcxproj`: Win32 DLL project.
- `src`: Win32 transport, TestExec parameter helpers, and action exports.
- `actions`: generated TestExec `.umd` definitions, one per export.
- `Test/SRCSerialTools.cpp`: metadata generator, unit tests, UMD inspector, and
  DLL/UTA smoke test.
- `Docs/ACTIONS.md`: complete TestExec parameter contract.
- `Docs/HARDWARE_SETUP.md`: Moxa, wiring, and deployment procedure.
- `Docs/TESTING.md`: local, loopback, and Windows 7 acceptance tests.

## Build

Install the Visual Studio 2019 `v142` x86/x64 C++ build tools in the Visual
Studio Installer. The project deliberately uses `v142`, Win32, the static C++
runtime, and `_WIN32_WINNT=0x0601` for the Windows 7 target.

From a Developer PowerShell prompt:

```powershell
msbuild SRCSerial.sln /m /p:Configuration=Release /p:Platform=Win32
```

On a development machine that only has v143, `Test/build_v143.cmd` provides a
local compatibility build. That build is not the Windows 7 release artifact.

## TestExec installation

Copy `Release\SRCSerial.dll` and every file under `actions` into directories
listed in TestExec's DLL and action-definition search paths. The Action Wizard
documentation requires both the DLL and UMD definitions. Restart TestExec after
changing its search paths.

Typical text flow:

1. `SRCSerial_start(Port="COM3", BaudRate=9600, ...)`
2. `SRCSerial_writeString(Text="*IDN?", Suffix="\r\n")`
3. `SRCSerial_readString(MaxChars=1024, Terminator="\r\n", TimeoutMs=1000)`
4. `SRCSerial_stop()`

For binary protocols, use `SRCSerial_writeBytes` and `SRCSerial_readBytes`.

## Error model

Every action writes `Success`, `ErrorCode`, and `ErrorMessage`. Windows errors
use `GetLastError()` values. Driver validation errors occupy `-1001` through
`-1099`. Timeouts are successful calls with `TimedOut=1` and may return partial
data. The DLL never displays modal error dialogs.

