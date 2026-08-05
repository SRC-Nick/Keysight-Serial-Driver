# Windows 7 full-build package

This package is generated from the `windows7-full-build` branch. That branch
contains the complete source tree plus compiled Win32 artifacts beneath
`artifacts/windows7-full-build`.

## Deployment contents

- `deploy\SRCSerial.dll`: 32-bit statically linked TestExec action DLL.
- `deploy\actions`: all 37 TestExec UMD definitions and their reference.
- `deploy\Examples`: product-specific worker examples, including the JLG
  joystick TestExec pseudocode and validation plan.
- `deploy\README.md`, `HARDWARE_SETUP.md`, and `TESTING.md`: operating,
  deployment, and acceptance documentation.
- `tools\SRCSerialTools.exe`: console UTA test/inspection utility.
- `development`: import library and linker export file.
- `symbols`: DLL and test-tool PDB files for debugging.
- `BUILD-METADATA.txt`: exact source commit, toolset, platform, and qualification
  state.
- `PE-REPORT.txt`: recorded machine type, exports, and imported DLL/API names.
- `SHA256SUMS.txt`: checksums for every unpacked package file.

The adjacent `.zip` file is the transferable copy of this directory, and its
`.sha256` file verifies the archive itself.

## Install on the TestExec station

The Windows 7 machine does not need Visual Studio, MSBuild, a compiler, or the
source tree. Copy these items from `deploy`:

1. `SRCSerial.dll` into a TestExec DLL search-path directory.
2. All `actions\SRCSerial_*.umd` files into a TestExec action-definition
   search-path directory.
3. Keep the documentation and checksum file with the station configuration
   record.
4. Restart TestExec and perform the acceptance procedure in `TESTING.md`.

`SRCSerialTools.exe` requires the installed Keysight UTA runtime. It is useful
for checks outside the TestExec UI but is not required during ordinary testplan
execution.

## Qualification warning

Read `BUILD-METADATA.txt` before deployment. `HardwareQualified=No` or
`TestExecWindows7Qualified=No` means the package is a candidate, not a released
station baseline. Do not tag or publish it as a release until it passes the
actual Windows 7/TestExec and physical RS-232/RS-485 acceptance procedure.

The first package may be built with v143 because v142 is not installed on the
development PC. The project remains pinned to v142, and a v142 package should
replace the candidate when that toolset becomes available.

## Rebuild on the development PC

Preferred Windows 7 candidate using the pinned toolset:

```powershell
powershell -ExecutionPolicy Bypass -File Scripts\Build-Win32Package.ps1 -Toolset v142
```

Local v143 compatibility package:

```powershell
powershell -ExecutionPolicy Bypass -File Scripts\Build-Win32Package.ps1 -Toolset v143
```

Run the script from a clean `main` checkout. After verification, update the
artifact branch from `main`, run the packaging script there, force-add the
ignored `artifacts` directory, commit, and push.
