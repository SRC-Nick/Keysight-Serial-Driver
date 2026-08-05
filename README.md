# SRCSerial

`SRCSerial.dll` is a 32-bit Keysight/Agilent TestExec SL action library for
industrial serial devices exposed as Windows COM ports. It uses only the Win32
COM-port and device-enumeration APIs, so it works with built-in UARTs, USB
adapters, PCI/PCIe cards, and virtual COM ports whose drivers support Windows 7.
No Moxa or other vendor SDK is linked into the DLL. Two optional experimental
actions can inspect and change the registry convention observed in Moxa driver
4.3.0.0; all normal serial actions remain vendor-neutral.

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
- Guarded Moxa UPort electrical-mode inspection and configuration for the
  locally verified 4.3.0.0 driver registry layout.
- A persistent fixed-frame protocol worker for sub-TestExec timing, with
  high-resolution response latency measurements and exclusive COM ownership.
- Generic RX-triggered response jobs with immediate or resettable quiet-gap
  scheduling, checksum generation, first/steady response sequencing, and
  atomic byte/frame updates.
- Generic create/update/destroy cyclic TX jobs, queued manual TX, bounded raw
  RX-frame and event rings, silence detection, counters, and timestamps.
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
| `SRCSerial_getMoxaPortMode` | Inspect a Moxa port's registry mode and driver identity. |
| `SRCSerial_setMoxaPortMode` | Experimentally set Moxa RS-232/422/485 mode and optionally refresh the device. |
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
| `SRCSerial_workerStart` | Start the fixed-frame background receive/scheduler worker. |
| `SRCSerial_workerStop` | Stop worker traffic while optionally preserving captured evidence. |
| `SRCSerial_workerGetStatus` | Read worker counters, queue depths, errors, ages, and response latency. |
| `SRCSerial_workerReadEvents` | Read bounded timestamped RX/TX/error events. |
| `SRCSerial_workerQueueTx` | Queue an immediate, next-valid-RX, or quiet-gap manual frame. |
| `SRCSerial_cycleCreate` | Create a periodic TX job with optional generated checksum. |
| `SRCSerial_cycleUpdate` | Atomically update a cyclic frame, period, or enabled state. |
| `SRCSerial_cycleDestroy` | Destroy a cyclic TX job. |
| `SRCSerial_responseCreate` | Create a valid-RX-triggered response job. |
| `SRCSerial_responseUpdate` | Atomically update response bytes, timing, sequence, or enabled state. |
| `SRCSerial_responseDestroy` | Destroy an RX-triggered response job. |
| `SRCSerial_rxGetCount` | Inspect worker frame/event queue depths. |
| `SRCSerial_rxReadFrame` | Peek or consume the oldest timestamped raw frame. |
| `SRCSerial_rxClear` | Clear worker frames, events, and/or counters. |

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

## Background protocol worker

Use the worker when the product requires a response cadence that TestExec steps
cannot meet reliably. The normal pattern is:

```text
SRCSerial_start(...)
SRCSerial_workerStart(RxFrameLength=6, RxIdOffset=0, RxIdValue=0x6A,
                      RxIdMask=0xFF, RxChecksumMode=1,
                      RxChecksumStart=0, RxChecksumLength=5,
                      RxChecksumOffset=5, PollIntervalMs=1)
SRCSerial_responseCreate(JobId=1, FrameHex="74 0A 00 00",
                         ResponseMode=1, QuietGapMs=1,
                         ChecksumMode=1, ChecksumStart=0,
                         ChecksumLength=3, ChecksumOffset=3)
// Power and exercise the UUT; poll status/read buffered frames at normal step speed.
SRCSerial_workerStop(ClearState=1)
SRCSerial_stop()
```

`ResponseMode=0` sends after a valid frame and the configured response delay.
Mode 1 also waits until receive traffic has been quiet for `QuietGapMs`; a new
valid frame replaces and reschedules a pending response when
`ReplacePending=1`. `TriggerSkipCount` and `SendCountLimit` can express a
first-message/steady-message sequence using two generic response jobs.

While the worker runs, normal foreground reads, writes, transactions, queue
purges, and control-line changes return -1012. Use the worker RX/event/status
actions and worker-safe job updates instead. Configuration and transport
diagnostic queries remain available.

The complete JLG joystick example—including Keysight pseudocode, two possible
startup response sequences, frame decoding, negative tests, and acceptance
gates—is in
[`Examples/JLG_Joystick_TestExec_Test_Plan.md`](Examples/JLG_Joystick_TestExec_Test_Plan.md).

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
| -1008 | No Moxa UPort registry instance matches the COM port. |
| -1009 | Installed Moxa driver does not match the required version. |
| -1010 | The matching COM port is open in this DLL. |
| -1011 | Moxa registry values have an unexpected type or value. |
| -1012 | Background worker owns the COM port; use worker-safe actions. |
| -1013 | A worker job/action requires a running worker. |
| -1014 | Requested cyclic or response job ID was not found. |
| -1015 | Requested job ID already exists in its job table. |
| -1016 | A bounded worker job/manual queue is full. |
| -1017 | Checksum mode or checksum byte range is invalid. |
| -1099 | An exception was contained at the exported action boundary. |

`SRCSerial_getDiagnostics` exposes `CE_FRAME`, `CE_RXPARITY`, `CE_OVERRUN`,
`CE_RXOVER`, and `CE_BREAK` observations. These are especially useful for
distinguishing application-protocol failures from baud, framing, signal-level,
polarity, noise, or adapter problems. COM APIs cannot directly determine
whether a connector carries true bipolar RS-232 or TTL UART levels; verify that
electrically with appropriate test equipment.

## Experimental Moxa electrical-mode control

Windows exposes baud, framing, flow control, and line state through the normal
COM API, but it does not expose whether a multi-interface adapter is operating
as RS-232, RS-422, RS-485 2-wire, or RS-485 4-wire. Moxa does not provide a
public API used by this project.

On the locally installed UPort 1150 driver `4.3.0.0`, Device Manager stores the
selection under:

```text
HKLM\SYSTEM\CurrentControlSet\Enum\MXUPORT\COM\<instance>\Device Parameters
```

The observed `SerInterface` mapping is:

- `0`: RS-232.
- `1`: RS-422.
- `2`: RS-485 2-wire.
- `3`: RS-485 4-wire.

`SRCSerial_getMoxaPortMode` safely enumerates instances and matches `PortName`;
it does not hardcode a device-instance path. It also returns `TxMode`, the
instance ID, and driver version. `TxMode` is diagnostic only and is never
modified because its public meaning has not been established.

`SRCSerial_setMoxaPortMode` has several safeguards:

- The serial session must be closed.
- `ExpectedDriverVersion` defaults to `4.3.0.0`.
- A different or unknown driver is rejected unless `AllowUnverifiedDriver=1`.
- Only an existing `REG_DWORD SerInterface` value is changed and read back.
- `RestartDevice=0` is the default; the action reports `RestartRequired=1`.
- `RestartDevice=1` explicitly requests a SetupAPI `DIF_PROPERTYCHANGE` for the
  matched port and reports whether Windows still requests restart/reboot.
- No privilege elevation, process launch, prompt, or message box occurs.

Writing under `HKLM\...\Enum` and refreshing a device normally requires an
Administrator process. If TestExec is not elevated, the action returns the
Windows access-denied error and leaves the value unchanged. A successful
registry write does not prove that the running driver applied the change;
verify it after the SetupAPI refresh, Device Manager restart, or unplug/replug.

Example registry-only change to RS-485 2-wire:

```text
SRCSerial_stop()
SRCSerial_setMoxaPortMode(
    Port="COM1", InterfaceMode=2,
    ExpectedDriverVersion="4.3.0.0",
    AllowUnverifiedDriver=0, RestartDevice=0)
```

Preserve `PreviousMode` for rollback. Treat this feature as experimental and
station-qualified, not as a portable Moxa contract. The older Windows 7 driver
v3.2 must be observed and qualified separately; do not enable
`AllowUnverifiedDriver` merely to bypass that validation on a production
station.

## Repository contents

- `SRCSerial.sln` / `SRCSerial.vcxproj`: Win32 DLL project.
- `src`: Win32 transport, protocol worker, SetupAPI enumeration, UTA helpers,
  and exports.
- `actions`: generated UMDs plus the complete recreation reference.
- `Test/SRCSerialTools.cpp`: UMD generator/inspector, unit tests, and DLL smoke
  test.
- `Test/README.md`: standalone console-test commands, prerequisites, expected
  output, scope, and limitations.
- `Scripts/Build-Win32Package.ps1`: reproducible Win32 build, validation,
  packaging, metadata, ZIP, and SHA-256 generation.
- `Docs/FULL_BUILD_BRANCH.md`: layout and deployment instructions for the
  compiled-artifact branch.
- `Examples/JLG_Joystick_TestExec_Test_Plan.md`: product-specific TestExec
  worker example and validation plan derived from the JLG protocol findings.
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
