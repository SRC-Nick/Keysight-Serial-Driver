# SRCSerial test tools

`SRCSerialTools.exe` validates much of `SRCSerial.dll` without launching the
Keysight TestExec application. It is a normal Win32 console program that:

- Unit-tests transport-independent parsing and validation.
- Loads the compiled DLL with `LoadLibrary`.
- Resolves every exported action with `GetProcAddress`.
- Creates real Keysight UTA parameter blocks.
- Calls selected DLL actions using the same parameter-block interface used by
  TestExec.
- Generates and restores the binary TestExec `.umd` action definitions.

This is useful for development and regression testing outside the TestExec UI.
It is not completely independent of Keysight software: the tool links to
`utacore.dll` and requires the TestExec/TS-5000 UTA headers, libraries, and
runtime installed on the computer.

## Files

- `SRCSerialTools.cpp`: console test, UMD generator, and UMD inspector.
- `SRCSerialTools.vcxproj`: Win32 release console project.
- `build_v143.cmd`: local compatibility build and complete automated check.
- `Release\SRCSerialTools.exe`: generated executable; ignored by Git.

The test project also compiles the transport and port-enumeration sources
directly so pure functions and closed-session behavior can be tested without
going through the DLL.

## Prerequisites

- Keysight/Agilent TestExec SL 7.1 or TS-5000 System Software with:
  - UTA headers.
  - `utacore.lib`.
  - `utacore.dll` and its runtime dependencies.
- Visual Studio C++ Win32 build tools.
- The repository must retain the installed Keysight include/library paths used
  by the project files.

The production Windows 7 build uses MSVC v142. `build_v143.cmd` deliberately
overrides the toolset to v143 for development computers that do not have v142.
A v143 result is not the final Windows 7 release artifact.

## Build and run the complete local check

From the repository root:

```powershell
Test\build_v143.cmd
```

The script:

1. Builds `SRCSerial.dll` and `SRCSerialTools.exe` as Win32 release binaries.
2. Runs the pure and closed-session self-tests.
3. Loads the actual DLL and performs the UTA action smoke test.
4. Restores and inspects all 21 checked-in UMD files.
5. Fails if the UMD count is not exactly 21.
6. Uses `dumpbin`, when available, to display the DLL machine type, exports,
   and imports.

A successful run includes messages similar to:

```text
PASS decode escapes
PASS decode hex
PASS reject invalid framing combination
PASS enumerate present COM ports
PASS load SRCSerial.dll
PASS resolve all action exports
PASS closed action reports structured error
```

It should also report:

```text
14C machine (x86)
21 number of functions
21 number of names
```

## Individual commands

Run these from the repository root after building the solution.

### Self-tests

```powershell
Test\Release\SRCSerialTools.exe self-test
```

Currently covers:

- COM-name normalization, including COM numbers above 9.
- Invalid COM-name rejection.
- Escape decoding.
- Hexadecimal decoding and formatting.
- Invalid and incomplete hex rejection.
- Session-state and diagnostics queries while closed.
- Invalid data-bit/stop-bit combinations.
- Closed-port read, drain, and buffer-query errors.
- Cancellation while closed.
- Present COM-port enumeration.
- Idempotent stop.

This command does not open a COM port or transmit data.

### DLL/UTA smoke test

```powershell
Test\Release\SRCSerialTools.exe action-smoke Release\SRCSerial.dll
```

This command:

- Loads the actual DLL.
- Verifies all 21 exports.
- Creates and binds a real UTA parameter block.
- Calls `SRCSerial_getBufferLength` while closed.
- Verifies the structured `Success=0, ErrorCode=-1002` response.
- Calls `SRCSerial_stop` and verifies that it succeeds while already stopped.

This proves that the DLL can be invoked through UTA without running TestExec,
but it is not a physical serial loopback test.

### Generate UMD files

```powershell
Test\Release\SRCSerialTools.exe generate
```

This writes all action definitions to `actions\SRCSerial_*.umd`. Use it only
when the public action parameter contract changes. The UTA serializer embeds
changing metadata, so unnecessary regeneration creates binary Git differences.

The definitions and their exact parameter order, type, direction, defaults,
array bounds, and Action Wizard settings are documented in
[`../actions/README.md`](../actions/README.md).

### Inspect a UMD

```powershell
Test\Release\SRCSerialTools.exe inspect actions\SRCSerial_transact.umd
```

The inspector restores the binary definition and reports its:

- Measurement name.
- UTA definition class.
- DLL library name.
- Configured action entry and entry ID.
- Parameter count.

Example:

```text
name=SRCSerial_transact class=CUtaStdCMeasDef library=SRCSerial.dll
entry[8]=SRCSerial_transact
parameters=24
```

## Testing without the TestExec application

The existing console tool exercises the DLL/UTA boundary but intentionally does
not provide an interactive terminal or arbitrary hardware-command interface.
Physical communication is tested with the loopback and two-endpoint procedures
in [`../Docs/TESTING.md`](../Docs/TESTING.md).

There are three relevant levels of independence:

| Test | TestExec UI required | Keysight UTA runtime required | Hardware required |
|---|---:|---:|---:|
| `self-test` | No | Yes | No |
| `action-smoke` | No | Yes | No |
| UMD generate/inspect | No | Yes | No |
| Final TestExec action acceptance | Yes | Yes | Yes |

A completely Keysight-independent harness could call the internal C++
transport functions directly or use a separate plain-C test DLL interface, but
that would not validate the actual `HUTAPB` action boundary. The current tool
intentionally uses UTA so its smoke test remains representative of TestExec.
It does not currently provide a command that opens a selected COM port and
performs the physical acceptance sequence from the console.

## Hardware acceptance

Automated desktop checks do not replace physical validation. Before release:

1. Run an RS-232 Tx/Rx loopback.
2. Test text, hex, raw binary, fixed-count, terminated, and idle-framed reads.
3. Exercise DTR/RTS/CTS and diagnostic error reporting.
4. Validate cancellation, disconnect/reconnect, and repeated start/stop.
5. Use two endpoints for RS-485 2-wire request/response traffic.
6. Run all actions inside the actual Windows 7 TestExec station.

The complete procedure and acceptance criteria are in
[`../Docs/TESTING.md`](../Docs/TESTING.md).

## Troubleshooting

- If `SRCSerialTools.exe` cannot start because `utacore.dll` is missing, run it
  on a computer with the Keysight runtime installed or correct the runtime DLL
  search path.
- If the build cannot find `uta.h` or `utacore.lib`, verify the Keysight include
  and library paths in the project files.
- If MSBuild reports MSB8020 for v142, install the Visual Studio 2019 C++ build
  tools or use `build_v143.cmd` only for a local compatibility check.
- If `action-smoke` cannot load `SRCSerial.dll`, build the Win32 Release target
  and confirm its dependent DLLs are available to the process.
- If an expected UMD is missing, rebuild `SRCSerialTools.exe`, run `generate`,
  and rerun `build_v143.cmd`.
