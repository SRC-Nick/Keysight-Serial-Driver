# SRCSerial

`SRCSerial.dll` is a 32-bit Keysight/Agilent TestExec SL action library for
industrial serial devices exposed as Windows COM ports. It uses only the Win32
COM-port and device-enumeration APIs, so it works with built-in UARTs, USB
adapters, PCI/PCIe cards, and virtual COM ports whose drivers support Windows 7.
No Moxa or other vendor SDK is linked into the DLL.

The driver targets TestExec SL 7.1 and Windows 7 SP1. The recommended physical
adapter is the isolated Moxa UPort 1150I because it provides standards-compliant
RS-232/422/485 transceivers, automatic RS-485 direction control, and a supported
Windows 7 driver. Electrical mode, termination, and bias are configured outside
this DLL.

## Capabilities

- One synchronized COM session per TestExec process.
- COM1 through COM999, arbitrary supported baud rates, 5–8 data bits, all Win32
  parity modes, valid stop-bit combinations, and no/software/hardware flow
  control.
- Explicit startup DTR and RTS behavior, manual line control, timed pulses,
  modem-input status, transmit draining, and queue purging.
- Narrow-string, raw-byte, and hexadecimal reads and writes.
- Fixed-length, terminated, and inter-byte-idle receive framing.
- Atomic request/response transactions with delays, retries, three request
  formats, three response-completion modes, and optional stale-input clearing.
- Overlapped Win32 I/O with bounded deadlines and a cancellation action.
- Accumulated framing, parity, overrun, break, byte, queue, and Win32-error
  diagnostics.
- Effective-configuration queries and present-port discovery including friendly
  name, hardware ID, device-instance ID, and Windows location.
- Four logging levels with timestamped hex/ASCII traffic and a 5 MiB limit.
- Structured noninteractive errors; the DLL never displays a message box.

## Actions

| Action | Purpose |
|---|---|
| `SRCSerial_start` | Open and deterministically configure a COM port. |
| `SRCSerial_stop` | Cancel, purge, and close the current session; idempotent. |
| `SRCSerial_cancel` | Request cancellation of a concurrently executing serial action. |
| `SRCSerial_isOpen` | Report session state and open COM name. |
| `SRCSerial_getConfiguration` | Return the effective active configuration. |
| `SRCSerial_enumeratePorts` | List present Windows Ports-class COM devices. |
| `SRCSerial_getBufferLength` | Inspect queued receive bytes without consuming them. |
| `SRCSerial_readBytes` | Read an exact count or a nonblocking queue snapshot. |
| `SRCSerial_readString` | Read narrow text through an escaped terminator. |
| `SRCSerial_readUntilIdle` | Read a frame completed by an inter-byte quiet period. |
| `SRCSerial_readHex` | Read bytes into a printable hexadecimal string. |
| `SRCSerial_writeBytes` | Send unsigned byte values from an Int32 array. |
| `SRCSerial_writeString` | Send a narrow string and escaped suffix. |
| `SRCSerial_writeHex` | Parse and send hexadecimal text. |
| `SRCSerial_transact` | Perform an atomic write/read transaction with optional retries. |
| `SRCSerial_flush` | Discard receive, transmit, or both queues. |
| `SRCSerial_drainTransmit` | Wait for the transmit queue to empty without discarding it. |
| `SRCSerial_setControlLines` | Set DTR, RTS, and break. |
| `SRCSerial_pulseControlLine` | Atomically pulse DTR, RTS, or break and restore it. |
| `SRCSerial_getLineStatus` | Read CTS, DSR, DCD, and ring inputs. |
| `SRCSerial_getDiagnostics` | Read/reset accumulated transport diagnostics. |

The authoritative parameter definitions, enum values, parameter order, array
bounds, Action Wizard procedure, and examples are in
[`actions/README.md`](actions/README.md). Generated `.umd` files for every
action are checked into `actions`.

## Typical usage

Text command terminated by CR/LF:

```text
SRCSerial_start(Port="COM3", BaudRate=9600, DataBits=8, StopBits=1,
                Parity=0, FlowControl=0, DTRMode=-1, RTSMode=-1)
SRCSerial_writeString(Text="*IDN?", Suffix="\r\n", TimeoutMs=1000)
SRCSerial_readString(MaxChars=1024, Terminator="\r\n",
                     TimeoutMs=1000, IncludeTerminator=0)
SRCSerial_stop()
```

Binary command entered as hex, with a response completed after 20 ms of silence:

```text
SRCSerial_writeHex(Hex="02 31 30 03", TimeoutMs=1000)
SRCSerial_readUntilIdle(MaxBytes=4096, TimeoutMs=2000,
                        InterByteTimeoutMs=20)
```

Atomic text request/response:

```text
SRCSerial_transact(
    RequestFormat=0, RequestText="READ", RequestSuffix="\r",
    FlushBeforeWrite=1,
    ResponseMode=2, Terminator="\r",
    TimeoutMs=1000, InterByteTimeoutMs=20,
    PreTransmitDelayMs=0, PostTransmitDelayMs=0, Retries=1)
```

`ResponseData`, `ResponseHex`, `BytesRead`, `TimedOut`, and `Attempts` receive
the result. A timeout is a completed action with `TimedOut=1`; inspect
`Success` separately for transport or validation failure.

## Session recovery

Normal test-plan teardown should always call `SRCSerial_stop`. If a testplan
aborts while TestExec remains alive, the session remains open, but the next
`SRCSerial_start` closes and replaces it. If TestExec terminates, Windows
reclaims the COM handle even if DLL detach code cannot run. A hung or debugger-
paused TestExec process retains the port until it resumes or terminates.

`SRCSerial_cancel` is intended for a parallel abort/cleanup path. It sets a
thread-safe cancellation request and calls `CancelIoEx`. The active action
returns error 995 (`ERROR_OPERATION_ABORTED`) at its next cancellation point.
It cannot help if TestExec never schedules the cancellation action.

## Logging

The `Logging` value supplied to `SRCSerial_start` is:

- `0`: off.
- `1`: configuration, errors, and transaction timeouts.
- `2`: level 1 plus timestamped TX/RX hex and printable ASCII.
- `3`: level 2 plus queue-depth polling details.

Logs are written under `logs` beside the loaded DLL. Each process log stops
growing at 5 MiB.

## Errors and diagnostics

Every action ends with these output parameters:

- `Success: Int32`: 1 on completion, 0 on failure.
- `ErrorCode: Int32`: zero, a Win32 error, or a negative library error.
- `ErrorMessage: String`: empty on success.

Timeout-capable read, drain, and transaction actions normally report
`Success=1, TimedOut=1`, retaining partial data. Write timeout is an error
because the complete request was not transmitted.

Validation errors are:

| Code | Meaning |
|---:|---|
| -1001 | Invalid or missing parameter. |
| -1002 | No COM port is open. |
| -1003 | Requested data exceeds the action array. |
| -1004 | NUL encountered by a string action; use bytes or hex. |
| -1005 | Manual line control conflicts with handshake ownership. |
| -1006 | Invalid hexadecimal input. |
| -1007 | A generated text output is too large. |
| -1099 | An exception was contained at the exported action boundary. |

`SRCSerial_getDiagnostics` exposes `CE_FRAME`, `CE_RXPARITY`, `CE_OVERRUN`,
`CE_RXOVER`, and `CE_BREAK` observations. These are especially useful for
distinguishing application-protocol failures from baud, framing, signal-level,
polarity, noise, or adapter problems. COM APIs cannot directly determine
whether a connector carries true bipolar RS-232 or TTL UART levels; verify that
electrically with appropriate test equipment.

## Repository contents

- `SRCSerial.sln` / `SRCSerial.vcxproj`: Win32 DLL project.
- `src`: Win32 transport, SetupAPI enumeration, UTA helpers, and exports.
- `actions`: generated UMDs plus the complete recreation reference.
- `Test/SRCSerialTools.cpp`: UMD generator/inspector, unit tests, and DLL smoke
  test.
- `Test/README.md`: standalone console-test commands, prerequisites, expected
  output, scope, and limitations.
- `Scripts/Build-Win32Package.ps1`: reproducible Win32 build, validation,
  packaging, metadata, ZIP, and SHA-256 generation.
- `Docs/FULL_BUILD_BRANCH.md`: layout and deployment instructions for the
  compiled-artifact branch.
- `Docs/HARDWARE_SETUP.md`: adapter, wiring, and deployment procedure.
- `Docs/TESTING.md`: local, loopback, RS-485, and Windows 7 acceptance tests.

## Build and validate

Install the Visual Studio 2019 `v142` x86/x64 C++ build tools. The checked-in
projects use Win32, the static C/C++ runtime, `_WIN32_WINNT=0x0601`, and
Windows 7-era APIs.

```powershell
msbuild SRCSerial.sln /m /p:Configuration=Release /p:Platform=Win32
```

On the current development computer, which only has v143 installed:

```powershell
Test\build_v143.cmd
```

That is a compatibility build, not the Windows 7 release artifact. Hardware
acceptance and the final release must use the pinned v142 toolset.

Create a complete transferable package with v142:

```powershell
powershell -ExecutionPolicy Bypass -File Scripts\Build-Win32Package.ps1 -Toolset v142
```

Until v142 is installed on this development PC, a clearly marked v143
compatibility candidate can be created with:

```powershell
powershell -ExecutionPolicy Bypass -File Scripts\Build-Win32Package.ps1 -Toolset v143
```

The package records its source commit, compiler toolset, MSBuild version,
platform, runtime linkage, and qualification state in `BUILD-METADATA.txt`.

## Git branches and downloading

The repository uses two long-lived branches:

- `main`: source code, projects, tests, UMD definitions, scripts, and
  documentation. Generated compiler/linker artifacts are ignored.
- `windows7-full-build`: everything in `main` plus a build snapshot under
  `artifacts/windows7-full-build`, a transferable ZIP, symbols, import
  libraries, build metadata, and SHA-256 checksums.

Clone only the source branch:

```powershell
git clone --branch main --single-branch https://github.com/SRC-Nick/Keysight-Serial-Driver.git
```

Clone the complete build branch:

```powershell
git clone --branch windows7-full-build --single-branch https://github.com/SRC-Nick/Keysight-Serial-Driver.git
```

If the repository is already cloned:

```powershell
git fetch origin windows7-full-build
git checkout windows7-full-build
git pull --ff-only origin windows7-full-build
```

Return to the source branch with:

```powershell
git checkout main
git pull --ff-only origin main
```

This is a private repository. For an HTTPS clone, enter the GitHub username when
prompted and use a personal access token with read access to this repository in
place of a password. Do not put the token directly in a command, script, or
remote URL. GitHub documents this process under
[managing personal access tokens](https://docs.github.com/en/authentication/keeping-your-account-and-data-secure/managing-your-personal-access-tokens).

Current Git for Windows releases no longer support Windows 7. The official
[Git for Windows requirements](https://gitforwindows.org/requirements.html)
identify v2.46.2 as the last Windows 7-compatible release. Because that version
no longer receives normal platform support, the preferred station workflow is:

1. Clone or download `windows7-full-build` on a maintained development PC.
2. Verify the ZIP SHA-256.
3. Copy the package to the Windows 7 station using approved removable media or
   the station's controlled file-transfer process.
4. Deploy only the contents under `deploy`; no Git or compiler is required on
   the station.

If Git must be installed directly on the Windows 7 machine, use v2.46.2 from
the official Git for Windows release archive and authenticate the private clone
with a narrowly scoped read-only token. Treat both the obsolete operating
system and its final supported Git client as additional security exposure.

When signed into GitHub in a browser, the full branch can also be downloaded
without Git from:

```text
https://github.com/SRC-Nick/Keysight-Serial-Driver/archive/refs/heads/windows7-full-build.zip
```

## TestExec installation

Copy `Release\SRCSerial.dll` and every `actions\SRCSerial_*.umd` file into the
configured TestExec DLL and action-definition search paths. Restart TestExec
after changing a search path or replacing action definitions. See
[`Docs/HARDWARE_SETUP.md`](Docs/HARDWARE_SETUP.md) for deployment and rollback.
