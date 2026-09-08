# TestExec action definitions

This directory contains one generated `.umd` file for each exported standard C
action in `SRCSerial.dll`. This document is the authoritative public contract
and contains everything needed to recreate the definitions with Keysight/
Agilent Action Wizard or the repository generator.

## Common definition settings

For every action:

- Definition type/class: **Standard C Measurement Definition**
  (`CUtaStdCMeasDef`).
- Action/measurement name: exactly the export name shown below.
- Library name: `SRCSerial.dll`.
- Source name: `src\SRCSerial_Actions.cpp` (informational).
- Action entry: **Initiate**, ID 8 (`UTA_ACT_INITIATE_ID`).
- Entry-point name: exactly the action/measurement name.
- Calling convention: UTA (`UTAAPI`); the implementation is exported with
  `extern "C"`.
- Parameter order: exactly the action-specific order shown below, followed by
  the three common outputs.

Append these parameters to every definition:

| Order | Name | UTA type | Direction | Default | Meaning |
|---:|---|---|---|---:|---|
| last-2 | `Success` | `CUtaInt32` | Output | 0 | 1 when the action completed successfully; otherwise 0. |
| last-1 | `ErrorCode` | `CUtaInt32` | Output | 0 | 0 on success, a negative project validation code, or a positive Win32 error. |
| last | `ErrorMessage` | `CUtaString` | Output | empty | Human-readable context; empty on success. |

All byte arrays are `CUtaInt32Array` with lower bound 0 and upper bound 4095
(4096 elements). Received and transmitted values are unsigned bytes represented
as Int32 values `0..255`.

## Recreation with Action Wizard

1. Open Action Wizard and create a new **Standard C Measurement Definition**.
2. Set the measurement name to the exact `SRCSerial_...` name.
3. Set the DLL/library to `SRCSerial.dll`.
4. Assign the same `SRCSerial_...` export to the **Initiate** action entry.
5. Add action-specific parameters in the exact order shown in this document.
6. Add `Success`, `ErrorCode`, and `ErrorMessage` last.
7. Mark every parameter labeled Output as output; leave all others as input.
8. For each byte array, select `CUtaInt32Array` and set bounds `0..4095`.
9. Save as `actions\<action-name>.umd`.
10. Inspect or exercise the definition in TestExec before deployment.

The automated equivalent is:

```powershell
Test\Release\SRCSerialTools.exe generate
Test\Release\SRCSerialTools.exe inspect actions\SRCSerial_start.umd
```

UMD serialization embeds changing metadata, so regeneration is intentionally
not part of the normal build. Regenerate and commit the complete affected set
only when the public contract changes.

## Shared values

`TimeoutMs=-1` uses the timeout configured by `SRCSerial_start`. A read timeout
usually returns `Success=1, TimedOut=1`, possibly with partial data. Escaped
strings support `\r`, `\n`, `\t`, `\\`, and `\xNN`.

Parity:

- `0` none, `1` odd, `2` even, `3` mark, `4` space.

Stop bits:

- `1` one, `15` one-and-a-half, `2` two.
- 1.5 stop bits require 5 data bits; 5 data bits cannot use two stop bits.

Flow control:

- `0` none, `1` XON/XOFF, `2` RTS/CTS, `3` DTR/DSR.

DTR/RTS mode:

- `-1` automatic: handshake when owned by selected flow control, otherwise
  enabled.
- `0` disabled, `1` enabled, `2` handshake.

Request format:

- `0` narrow text plus escaped suffix.
- `1` hexadecimal text.
- `2` Int32 byte array.

Response mode:

- `0` response ends after `InterByteTimeoutMs` of silence.
- `1` response ends after `ResponseCount` bytes.
- `2` response ends after the escaped `Terminator`.

Hex input accepts paired digits with optional whitespace, comma, colon, hyphen,
or per-byte `0x` prefixes. Hex output is uppercase, space-separated text.

## Session and discovery actions

### `SRCSerial_start`

Closes any prior session, resets diagnostics, validates all configuration, opens
the COM port exclusively, applies a fully specified DCB, reads back the effective
settings, and optionally purges queues. Failure leaves the driver closed.

| Order | Name | Type | Direction | Default | Meaning |
|---:|---|---|---|---:|---|
| 1 | `Port` | String | Input | `COM1` | COM1 through COM999; `\\.\COMn` also accepted. |
| 2 | `BaudRate` | Int32 | Input | 9600 | Positive rate supported by adapter. |
| 3 | `DataBits` | Int32 | Input | 8 | 5 through 8. |
| 4 | `StopBits` | Int32 | Input | 1 | Shared stop-bit enum. |
| 5 | `Parity` | Int32 | Input | 0 | Shared parity enum. |
| 6 | `FlowControl` | Int32 | Input | 0 | Shared flow-control enum. |
| 7 | `DTRMode` | Int32 | Input | -1 | Automatic/disabled/enabled/handshake. |
| 8 | `RTSMode` | Int32 | Input | -1 | Automatic/disabled/enabled/handshake. |
| 9 | `ReadTimeoutMs` | Int32 | Input | 1000 | Default read deadline. |
| 10 | `WriteTimeoutMs` | Int32 | Input | 1000 | Default write deadline. |
| 11 | `FlushOnOpen` | Int32 | Input | 1 | Nonzero discards RX/TX queues after setup. |
| 12 | `Logging` | Int32 | Input | 0 | 0 off; 1 configuration, worker jobs, and errors; 2 asynchronous raw traffic and worker scheduling/source trace; 3 also logs queue polling. |

Example: open COM12 at 19200-8-E-1 with RTS/CTS:

```text
Port="COM12", BaudRate=19200, DataBits=8, StopBits=1,
Parity=2, FlowControl=2, DTRMode=1, RTSMode=-1,
ReadTimeoutMs=2000, WriteTimeoutMs=1000, FlushOnOpen=1, Logging=1
```

### `SRCSerial_stop`

No action-specific parameters. Cancels I/O, purges both queues, closes the COM
handle and log, and succeeds if already stopped.

### `SRCSerial_cancel`

No action-specific parameters. Thread-safely requests cancellation and invokes
`CancelIoEx`. Use from a parallel TestExec abort/cleanup path. The active action
normally returns Win32 error 995. It does not close the session.

### `SRCSerial_isOpen`

| Order | Name | Type | Direction | Default | Meaning |
|---:|---|---|---|---|---|
| 1 | `Open` | Int32 | Output | 0 | 1 when this DLL owns an open COM session. |
| 2 | `Port` | String | Output | empty | Active normalized COM name, or empty while closed. |

This query succeeds even while closed.

### `SRCSerial_getConfiguration`

All parameters are outputs:

| Order | Name | Type | Direction | Default | Meaning |
|---:|---|---|---|---:|---|
| 1 | `Open` | Int32 | Output | 0 | 1 when a session is open. |
| 2 | `Port` | String | Output | empty | Active COM name; empty while closed. |
| 3 | `BaudRate` | Int32 | Output | 0 | Baud rate accepted by the driver. |
| 4 | `DataBits` | Int32 | Output | 0 | Effective data bits. |
| 5 | `StopBits` | Int32 | Output | 0 | Effective shared stop-bit enum. |
| 6 | `Parity` | Int32 | Output | 0 | Effective shared parity enum. |
| 7 | `FlowControl` | Int32 | Output | 0 | Effective shared flow-control enum. |
| 8 | `DTRMode` | Int32 | Output | 0 | Effective DTR mode. |
| 9 | `RTSMode` | Int32 | Output | 0 | Effective RTS mode. |
| 10 | `ReadTimeoutMs` | Int32 | Output | 0 | Configured default read timeout. |
| 11 | `WriteTimeoutMs` | Int32 | Output | 0 | Configured default write timeout. |
| 12 | `Logging` | Int32 | Output | 0 | Active logging level, 0 through 3. |

When open, values reflect the configuration accepted by the adapter driver.

### `SRCSerial_enumeratePorts`

| Order | Name | Type | Direction | Default | Meaning |
|---:|---|---|---|---|---|
| 1 | `Ports` | String | Output | empty | CR/LF records containing COM name, friendly name, IDs, and location. |
| 2 | `Count` | Int32 | Output | 0 | Number of present Ports-class COM devices returned. |

Returns one CR/LF-separated record per present Windows Ports-class COM device:

```text
COM name|friendly name|hardware ID|device instance ID|location
```

The device-instance ID commonly contains a USB serial identity. Enumeration
does not open a port and can run while the serial session is closed.

### `SRCSerial_getMoxaPortMode`

Read-only experimental query for the registry layout observed with Moxa UPort
driver 4.3.0.0. It enumerates `HKLM\SYSTEM\CurrentControlSet\Enum\MXUPORT\COM`,
matches the requested COM name using `Device Parameters\PortName`, and never
hardcodes an instance suffix.

| Order | Name | Type | Direction | Default | Meaning |
|---:|---|---|---|---|---|
| 1 | `Port` | String | Input | `COM1` | Moxa COM port to match. |
| 2 | `Found` | Int32 | Output | 0 | 1 after an exact Moxa instance match. |
| 3 | `InterfaceMode` | Int32 | Output | -1 | Observed `SerInterface`: 0 RS-232, 1 RS-422, 2 RS-485 2W, 3 RS-485 4W. |
| 4 | `TxMode` | Int32 | Output | -1 | Raw diagnostic value; meaning is not assumed. |
| 5 | `InstanceId` | String | Output | empty | Full dynamic PnP instance ID. |
| 6 | `DriverVersion` | String | Output | empty | Version read through the instance's class-driver registry key. |

This action does not open the COM port, write the registry, restart a device,
or require Administrator privileges under the locally observed ACLs.

### `SRCSerial_setMoxaPortMode`

Experimental Moxa-specific registry update. All outputs are populated as far as
possible even when the action returns an error after finding the device.

| Order | Name | Type | Dir. | Default | Meaning |
|---:|---|---|---|---|---|
| 1 | `Port` | String | In | `COM1` | Moxa COM port to match dynamically. |
| 2 | `InterfaceMode` | Int32 | In | 0 | 0 RS-232, 1 RS-422, 2 RS-485 2W, 3 RS-485 4W. |
| 3 | `ExpectedDriverVersion` | String | In | `4.3.0.0` | Required exact version unless override is explicit. |
| 4 | `AllowUnverifiedDriver` | Int32 | In | 0 | Nonzero bypasses version mismatch protection. |
| 5 | `RestartDevice` | Int32 | In | 0 | Nonzero requests SetupAPI `DIF_PROPERTYCHANGE`. |
| 6 | `Found` | Int32 | Out | 0 | Exact Moxa COM instance was found. |
| 7 | `PreviousMode` | Int32 | Out | -1 | Mode before any registry update. |
| 8 | `CurrentMode` | Int32 | Out | -1 | Verified registry value after the operation. |
| 9 | `TxMode` | Int32 | Out | -1 | Raw diagnostic value; never modified. |
| 10 | `InstanceId` | String | Out | empty | Full matched PnP instance ID. |
| 11 | `DriverVersion` | String | Out | empty | Installed port-driver version. |
| 12 | `RegistryUpdated` | Int32 | Out | 0 | `SerInterface` was changed and read back. |
| 13 | `RestartAttempted` | Int32 | Out | 0 | SetupAPI refresh was requested. |
| 14 | `RestartSucceeded` | Int32 | Out | 0 | SetupAPI call completed successfully. |
| 15 | `RestartRequired` | Int32 | Out | 0 | Reconnect/restart is still required. |

The action rejects an open matching session with -1010. Changing an existing
`HKLM\...\Enum` value and refreshing a PnP device normally requires an elevated
TestExec process; the DLL never prompts for elevation. With `RestartDevice=0`,
a changed registry value returns `RestartRequired=1`. With `RestartDevice=1`,
success only means Windows accepted the property-change request; confirm the
electrical mode with Device Manager and hardware traffic.

Example:

```text
SRCSerial_stop()
SRCSerial_setMoxaPortMode(
    Port="COM1", InterfaceMode=2,
    ExpectedDriverVersion="4.3.0.0",
    AllowUnverifiedDriver=0, RestartDevice=0)
```

Keep `PreviousMode` for rollback. Driver v3.2 and other versions are unverified;
do not use `AllowUnverifiedDriver=1` on a production station without separately
confirming registry mapping, application behavior, and recovery.

## Read actions

### `SRCSerial_getBufferLength`

| Order | Name | Type | Direction | Default | Meaning |
|---:|---|---|---|---:|---|
| 1 | `BytesAvailable` | Int32 | Output | 0 | Bytes currently queued by Windows for receive; data is not consumed. |

Returns the current receive-queue depth without consuming bytes.

### `SRCSerial_readBytes`

| Order | Name | Type | Direction | Default | Meaning |
|---:|---|---|---|---:|---|
| 1 | `RequestedCount` | Int32 | Input | 0 | Positive: exact bytes to await; 0: nonblocking queue snapshot. |
| 2 | `TimeoutMs` | Int32 | Input | -1 | Exact-count deadline; -1 uses session default. |
| 3 | `Data` | Int32 array 0..4095 | Output | zeros | Unsigned received bytes represented as Int32 0..255. |
| 4 | `BytesRead` | Int32 | Output | 0 | Valid elements placed in `Data`. |
| 5 | `TimedOut` | Int32 | Output | 0 | 1 when exact-count reading ended at its deadline. |

Positive `RequestedCount` waits for exactly that many bytes or the deadline.
Zero performs a nonblocking snapshot of all currently queued bytes up to array
capacity.

### `SRCSerial_readString`

| Order | Name | Type | Direction | Default | Meaning |
|---:|---|---|---|---|---|
| 1 | `MaxChars` | Int32 | Input | 1024 | Maximum bytes retained as narrow text. |
| 2 | `Terminator` | String | Input | empty | Escaped terminator; empty selects idle-framed text. |
| 3 | `TimeoutMs` | Int32 | Input | -1 | Overall deadline; -1 uses session default. |
| 4 | `IncludeTerminator` | Int32 | Input | 0 | Nonzero retains the matched terminator in `Text`. |
| 5 | `Text` | String | Output | empty | Received non-NUL narrow text. |
| 6 | `BytesRead` | Int32 | Output | 0 | Total received bytes, including a consumed terminator. |
| 7 | `TimedOut` | Int32 | Output | 0 | 1 when no complete result arrived by the deadline. |

With a terminator, reads through the complete escaped sequence without consuming
later bytes. With an empty terminator, returns the first group separated by a
short idle interval. A received NUL returns -1004; use bytes or hex instead.
Filling `MaxChars` without finding the requested terminator returns -1003 so a
truncated frame cannot be mistaken for a complete response.

### `SRCSerial_readUntilIdle`

| Order | Name | Type | Direction | Default | Meaning |
|---:|---|---|---|---:|---|
| 1 | `MaxBytes` | Int32 | Input | 4096 | Maximum frame bytes, 1..4096. |
| 2 | `TimeoutMs` | Int32 | Input | -1 | Deadline for the first byte; -1 uses session default. |
| 3 | `InterByteTimeoutMs` | Int32 | Input | 20 | Silence after first byte that completes the frame. |
| 4 | `Data` | Int32 array 0..4095 | Output | zeros | Raw received bytes as Int32 0..255. |
| 5 | `Hex` | String | Output | empty | Same bytes as uppercase space-separated hex. |
| 6 | `BytesRead` | Int32 | Output | 0 | Valid output byte count. |
| 7 | `TimedOut` | Int32 | Output | 0 | 1 only when no first byte arrived by the deadline. |

`TimeoutMs` is the deadline for receiving the frame. After the first byte,
`InterByteTimeoutMs` of silence completes the frame. `TimedOut=1` means no byte
arrived by the overall deadline; reaching `MaxBytes` also completes normally.

### `SRCSerial_readHex`

| Order | Name | Type | Direction | Default | Meaning |
|---:|---|---|---|---:|---|
| 1 | `RequestedCount` | Int32 | Input | 0 | Positive exact count; 0 snapshots queued bytes. |
| 2 | `MaxBytes` | Int32 | Input | 1024 | Maximum bytes represented in `Hex`. |
| 3 | `TimeoutMs` | Int32 | Input | -1 | Exact-count deadline; -1 uses session default. |
| 4 | `Hex` | String | Output | empty | Uppercase space-separated received bytes. |
| 5 | `BytesRead` | Int32 | Output | 0 | Bytes represented in `Hex`. |
| 6 | `TimedOut` | Int32 | Output | 0 | 1 when exact-count reading timed out. |

The count semantics match `readBytes`; output resembles `02 31 FF 7A`.

## Write and transaction actions

### `SRCSerial_writeBytes`

| Order | Name | Type | Direction | Default | Meaning |
|---:|---|---|---|---:|---|
| 1 | `Data` | Int32 array 0..4095 | Input | zeros | Unsigned bytes represented as Int32 0..255. |
| 2 | `Count` | Int32 | Input | 0 | Number of leading array elements to transmit. |
| 3 | `TimeoutMs` | Int32 | Input | -1 | Write deadline; -1 uses session default. |
| 4 | `BytesWritten` | Int32 | Output | 0 | Bytes accepted by the Windows write operation. |

`Count` must fit the array and every used value must be `0..255`.

### `SRCSerial_writeString`

| Order | Name | Type | Direction | Default | Meaning |
|---:|---|---|---|---|---|
| 1 | `Text` | String | Input | empty | Narrow string bytes sent without Unicode conversion. |
| 2 | `Suffix` | String | Input | empty | Escaped bytes appended after `Text`. |
| 3 | `TimeoutMs` | Int32 | Input | -1 | Write deadline; -1 uses session default. |
| 4 | `BytesWritten` | Int32 | Output | 0 | Total text plus suffix bytes written. |

Sends TestExec narrow string bytes without Unicode conversion, then the decoded
suffix. Example: `Text="READ?", Suffix="\r\n"`.

### `SRCSerial_writeHex`

| Order | Name | Type | Direction | Default | Meaning |
|---:|---|---|---|---|---|
| 1 | `Hex` | String | Input | empty | Paired hexadecimal byte text to decode and send. |
| 2 | `TimeoutMs` | Int32 | Input | -1 | Write deadline; -1 uses session default. |
| 3 | `BytesWritten` | Int32 | Output | 0 | Decoded bytes accepted by the write. |

Example: `Hex="0x02 52 44 03 0D"`.

### `SRCSerial_transact`

The complete request/write/response sequence holds the one-session lock, so no
other serial action can interleave between its write and read. A retry occurs
only after a response timeout; transport and validation failures return
immediately. Filling `ResponseData` without finding a mode-2 terminator returns
-1003.

| Order | Name | Type | Dir. | Default | Meaning |
|---:|---|---|---|---|---|
| 1 | `RequestFormat` | Int32 | In | 0 | 0 text, 1 hex, 2 byte array. |
| 2 | `RequestText` | String | In | empty | Used by format 0. |
| 3 | `RequestSuffix` | String | In | empty | Escaped suffix for format 0. |
| 4 | `RequestHex` | String | In | empty | Used by format 1. |
| 5 | `RequestData` | Int32 array 0..4095 | In | zeros | Used by format 2. |
| 6 | `RequestCount` | Int32 | In | 0 | Used array elements. |
| 7 | `FlushBeforeWrite` | Int32 | In | 1 | Nonzero discards stale RX bytes each attempt. |
| 8 | `ResponseMode` | Int32 | In | 0 | 0 idle, 1 count, 2 terminator. |
| 9 | `ResponseCount` | Int32 | In | 0 | Required by mode 1. |
| 10 | `Terminator` | String | In | empty | Escaped; required by mode 2. |
| 11 | `TimeoutMs` | Int32 | In | -1 | Response deadline per attempt. |
| 12 | `InterByteTimeoutMs` | Int32 | In | 20 | Required by mode 0. |
| 13 | `PreTransmitDelayMs` | Int32 | In | 0 | Delay before request; maximum 60000. |
| 14 | `PostTransmitDelayMs` | Int32 | In | 0 | Delay before receiving; maximum 60000. |
| 15 | `Retries` | Int32 | In | 0 | Additional attempts, 0 through 100. |
| 16 | `ResponseData` | Int32 array 0..4095 | Out | zeros | Raw response. |
| 17 | `ResponseHex` | String | Out | empty | Same response as hex. |
| 18 | `BytesWritten` | Int32 | Out | 0 | Bytes sent on final attempt. |
| 19 | `BytesRead` | Int32 | Out | 0 | Bytes retained from final attempt. |
| 20 | `TimedOut` | Int32 | Out | 0 | 1 if every attempt timed out. |
| 21 | `Attempts` | Int32 | Out | 0 | Attempts actually performed. |

Text/terminator example:

```text
RequestFormat=0, RequestText="READ?", RequestSuffix="\r",
FlushBeforeWrite=1, ResponseMode=2, Terminator="\r",
TimeoutMs=1000, Retries=1
```

Binary/count example:

```text
RequestFormat=1, RequestHex="02 31 03",
ResponseMode=1, ResponseCount=8, TimeoutMs=500
```

## Queue and line-control actions

### `SRCSerial_flush`

| Order | Name | Type | Direction | Default | Meaning |
|---:|---|---|---|---:|---|
| 1 | `FlushMask` | Int32 | Input | 3 | 1 discard RX, 2 discard TX, 3 discard both. |

Mask `1` discards receive, `2` discards transmit, and `3` discards both. This
purges bytes; it does not wait for transmit completion.

### `SRCSerial_drainTransmit`

| Order | Name | Type | Direction | Default | Meaning |
|---:|---|---|---|---:|---|
| 1 | `TimeoutMs` | Int32 | Input | -1 | Maximum wait for the driver TX queue to empty. |
| 2 | `TimedOut` | Int32 | Output | 0 | 1 if queued transmit bytes remained at the deadline. |

Waits for the driver transmit queue to reach zero without discarding it.

### `SRCSerial_setControlLines`

| Order | Name | Type | Direction | Default | Meaning |
|---:|---|---|---|---:|---|
| 1 | `DTR` | Int32 | Input | -1 | -1 unchanged, 0 clear, 1 set DTR. |
| 2 | `RTS` | Int32 | Input | -1 | -1 unchanged, 0 clear, 1 set RTS. |
| 3 | `Break` | Int32 | Input | -1 | -1 unchanged, 0 clear, 1 assert break. |

Each value is `-1` unchanged, `0` clear, or `1` set. Manual DTR/RTS changes are
rejected when the line is owned by handshake mode.

### `SRCSerial_pulseControlLine`

| Order | Name | Type | Direction | Default | Meaning |
|---:|---|---|---|---:|---|
| 1 | `Line` | Int32 | Input | 0 | 0 DTR, 1 RTS, 2 break. |
| 2 | `State` | Int32 | Input | 0 | Temporary state, 0 clear or 1 set. |
| 3 | `DurationMs` | Int32 | Input | 100 | Pulse duration, 0..60000 ms. |
| 4 | `RestoreState` | Int32 | Input | -1 | -1 restores tracked prior state; 0/1 selects explicit restore state. |

`Line` is `0` DTR, `1` RTS, or `2` break. `State` is 0/1.
`RestoreState=-1` restores the tracked prior state; otherwise it restores the
specified 0/1 value. Maximum duration is 60000 ms.

### `SRCSerial_getLineStatus`

| Order | Name | Type | Direction | Default | Meaning |
|---:|---|---|---|---:|---|
| 1 | `CTS` | Int32 | Output | 0 | 1 when Clear To Send is asserted. |
| 2 | `DSR` | Int32 | Output | 0 | 1 when Data Set Ready is asserted. |
| 3 | `DCD` | Int32 | Output | 0 | 1 when carrier detect is asserted. |
| 4 | `Ring` | Int32 | Output | 0 | 1 when ring indicator is asserted. |

An adapter that does not expose a signal reports it inactive.

## Diagnostics action

### `SRCSerial_getDiagnostics`

| Order | Name | Type | Direction | Default | Meaning |
|---:|---|---|---|---:|---|
| 1 | `ResetAfterRead` | Int32 | Input | 0 | Nonzero clears accumulated counters after returning this snapshot. |
| 2 | `FrameErrors` | Int32 | Output | 0 | Observed Windows `CE_FRAME` indications. |
| 3 | `ParityErrors` | Int32 | Output | 0 | Observed Windows `CE_RXPARITY` indications. |
| 4 | `OverrunErrors` | Int32 | Output | 0 | UART character-overrun indications. |
| 5 | `BufferOverrunErrors` | Int32 | Output | 0 | Windows receive-buffer overrun indications. |
| 6 | `BreakCount` | Int32 | Output | 0 | Observed receive-break indications. |
| 7 | `RxBytesQueued` | Int32 | Output | 0 | Current driver receive queue depth. |
| 8 | `TxBytesQueued` | Int32 | Output | 0 | Current driver transmit queue depth. |
| 9 | `TotalRxBytes` | Int32 | Output | 0 | Bytes read successfully since session start/reset. |
| 10 | `TotalTxBytes` | Int32 | Output | 0 | Bytes written successfully since session start/reset. |
| 11 | `LastWin32Error` | Int32 | Output | 0 | Most recent transport-level Win32 error, or 0. |

The first five counters count observations of their corresponding Win32
communication error flags. Totals saturate at `2,147,483,647` and reset on a
successful new start. If `ResetAfterRead` is nonzero, accumulated counters are
cleared after copying the returned values; current queue depths are retained.

Framing and parity counts are strong evidence of electrical polarity, signal
level, baud, parity, stop-bit, grounding, or noise problems. They do not identify
the physical cause by themselves.

## Background fixed-frame worker

The worker is for protocols whose timing is faster than a TestExec action loop.
It uses the open COM session, reassembles one configured fixed-length RX frame
type, validates ID/checksum, schedules response and cyclic jobs, and stores
bounded RX/event histories. While it runs, normal `read*`, `write*`, `transact`,
`flush`, `drainTransmit`, and control-line mutation actions return -1012. Worker
queries, RX/event actions, `getConfiguration`, and `getDiagnostics` remain safe.

Worker checksum modes are `0` none, `1` one's-complement sum, `2` low byte of
sum, and `3` XOR. Offsets are zero based. A nonzero mode requires a payload
range and checksum offset that fit the frame; the checksum byte cannot be inside
the payload range.

### `SRCSerial_workerStart`

Requires an open session and resets earlier jobs, queues, counters, and timing.

| Order | Name | Type | Dir. | Default | Meaning |
|---:|---|---|---|---:|---|
| 1 | `RxFrameLength` | Int32 | In | 1 | Fixed RX length, 1..4096. |
| 2 | `RxIdOffset` | Int32 | In | 0 | ID byte offset. |
| 3 | `RxIdValue` | Int32 | In | 0 | Expected ID, 0..255. |
| 4 | `RxIdMask` | Int32 | In | 0 | ID mask; 0 accepts any ID. |
| 5 | `RxChecksumMode` | Int32 | In | 0 | Shared worker checksum enum. |
| 6 | `RxChecksumStart` | Int32 | In | 0 | RX checksum payload start. |
| 7 | `RxChecksumLength` | Int32 | In | 0 | RX checksum payload length. |
| 8 | `RxChecksumOffset` | Int32 | In | -1 | RX checksum byte offset. |
| 9 | `RxQueueCapacity` | Int32 | In | 256 | Frame ring size, 1..4096. |
| 10 | `EventQueueCapacity` | Int32 | In | 512 | Event ring size, 1..8192. |
| 11 | `SilenceTimeoutMs` | Int32 | In | 1000 | 0 disables silence episodes. |
| 12 | `PollIntervalMs` | Int32 | In | 1 | Maximum fallback scheduler wait, 1..50 ms. Incoming serial activity wakes the worker immediately through `WaitCommEvent`; this value is no longer the RX polling cadence. |
| 13 | `MinimumInterTxMs` | Int32 | In | 0 | TX guard, 0..60000 ms. |
| 14 | `WorkerPriority` | Int32 | In | 1 | 0 normal, 1 above normal, 2 highest. |
| 15 | `WorkerRunning` | Int32 | Out | 0 | 1 after thread creation. |

JLG RX example: length 6, ID offset 0/value 106/mask 255, checksum
mode 1/start 0/length 5/offset 5.

### `SRCSerial_workerStop`

| Order | Name | Type | Dir. | Default | Meaning |
|---:|---|---|---|---:|---|
| 1 | `ClearState` | Int32 | In | 0 | Nonzero also clears retained frames, events, counters, and last error. |
| 2 | `WorkerRunning` | Int32 | Out | 0 | 0 after successful shutdown. |

Stops within five seconds and destroys all jobs/pending traffic. Nonzero
`ClearState` also clears retained frames, events, counters, and the last worker
error. The COM port remains open. `SRCSerial_start`, `stop`, and `cancel` stop
an existing worker before their normal session operation.

### `SRCSerial_workerGetStatus`

| Order | Name | Type | Dir. | Default | Meaning |
|---:|---|---|---|---:|---|
| 1 | `ResetCounters` | Int32 | In | 0 | Nonzero returns then clears counters and latency maxima. |
| 2 | `WorkerRunning` | Int32 | Out | 0 | 1 while the worker thread owns COM traffic. |
| 3 | `RxFrameCount` | Int32 | Out | 0 | Candidate fixed-length frames processed. |
| 4 | `ValidRxFrameCount` | Int32 | Out | 0 | Frames with matching ID and valid checksum. |
| 5 | `InvalidRxFrameCount` | Int32 | Out | 0 | Candidate frames rejected for checksum. |
| 6 | `ChecksumErrorCount` | Int32 | Out | 0 | RX checksum failures. |
| 7 | `BadIdCount` | Int32 | Out | 0 | Leading bytes discarded while searching for the configured ID. |
| 8 | `DroppedByteCount` | Int32 | Out | 0 | Bytes dropped during resynchronization or stream limiting. |
| 9 | `TxFrameCount` | Int32 | Out | 0 | All successful worker response, cyclic, and manual frames. |
| 10 | `ResponseTxCount` | Int32 | Out | 0 | Successful RX-triggered response frames. |
| 11 | `ResponseSuppressedCount` | Int32 | Out | 0 | Matching responses intentionally withheld because accumulated or next-frame RX bytes made a late transmission unsafe. |
| 12 | `CyclicTxCount` | Int32 | Out | 0 | Successful periodic cyclic frames; should remain 0 for JLG. |
| 13 | `ManualTxCount` | Int32 | Out | 0 | Successful `workerQueueTx` frames. |
| 14 | `RxSilenceTimeoutCount` | Int32 | Out | 0 | Distinct configured receive-silence episodes. |
| 15 | `RxFramesQueued` | Int32 | Out | 0 | Complete retained frames awaiting `rxReadFrame`. |
| 16 | `EventsQueued` | Int32 | Out | 0 | Retained worker event records. |
| 17 | `PendingTxCount` | Int32 | Out | 0 | Pending response plus manual TX entries; excludes cyclic jobs. |
| 18 | `LastRxAgeMs` | Int32 | Out | -1 | Milliseconds since any received byte, or -1 before RX. |
| 19 | `LastResponseLatencyUs` | Int32 | Out | -1 | Latest trigger-to-completed-write software latency. |
| 20 | `MaxResponseLatencyUs` | Int32 | Out | 0 | Largest response latency since reset. |
| 21 | `LastValidRxAgeMs` | Int32 | Out | -1 | Milliseconds since a valid configured frame, or -1. |
| 22 | `LastTxAgeMs` | Int32 | Out | -1 | Milliseconds since successful worker TX, or -1. |
| 23 | `WorkerLastErrorCode` | Int32 | Out | 0 | Last fatal worker transport/project error. |
| 24 | `WorkerLastErrorMessage` | String | Out | empty | Human-readable worker error context. |

Ages are -1 before the first event. Response latency is measured with
`QueryPerformanceCounter` from trigger to completion of the Win32 write; use a
scope to validate physical two-wire transmitter timing. `ResetCounters=1`
returns the pre-reset snapshot, then resets counters and latency maxima.

### `SRCSerial_workerReadEvents`

| Order | Name | Type | Dir. | Default | Meaning |
|---:|---|---|---|---:|---|
| 1 | `MaxEvents` | Int32 | In | 20 | Oldest records to return, 1..1000. |
| 2 | `ClearAfterRead` | Int32 | In | 1 | Nonzero consumes returned records; 0 peeks. |
| 3 | `EventsText` | String | Out | empty | UTC CR/LF event lines with type, job, bytes, and message. |
| 4 | `EventsReturned` | Int32 | Out | 0 | Records included in `EventsText`. |
| 5 | `EventsRemaining` | Int32 | Out | 0 | Records still queued after the requested operation. |

Returns oldest UTC, CR/LF-separated events with an event sequence and
high-resolution `worker_us` offset: worker start/stop, valid/checksum
RX, response/cycle/manual TX, `RESPONSE_CANCEL` when later RX invalidates a
stale quiet-gap response, `RESPONSE_SUPPRESSED_BACKLOG` when a late reply is
withheld to protect the next RX window, silence, and transport errors. At capacity the oldest
event is dropped. Every actual transmit is labeled `TX_RESPONSE`, `TX_CYCLE`,
or `TX_MANUAL`, which is the preferred way to identify an unexpected sender.

### `SRCSerial_workerQueueTx`

| Order | Name | Type | Dir. | Default | Meaning |
|---:|---|---|---|---:|---|
| 1 | `Hex` | String | In | empty | Raw 1..4096-byte manual frame; no checksum is generated. |
| 2 | `Mode` | Int32 | In | 0 | 0 next opportunity, 1 after next valid RX, 2 after quiet gap. |
| 3 | `QuietGapMs` | Int32 | In | 1 | Required RX silence for mode 2; ignored by modes 0/1. |
| 4 | `Queued` | Int32 | Out | 0 | 1 when the frame entered the manual queue. |
| 5 | `QueueDepth` | Int32 | Out | 0 | Manual entries queued after this call. |

Mode 0 sends at the next scheduler opportunity, 1 after the next valid RX, and
2 after the current/next quiet gap. The 32-entry queue accepts 1..4096 raw
bytes and does not generate a checksum.

## Cyclic TX jobs

Up to 32 positive IDs exist in the cyclic table. These jobs are independent of
RX and can collide on a two-wire bus; use response jobs for request/response.

### `SRCSerial_cycleCreate`

| Order | Name | Type | Dir. | Default | Meaning |
|---:|---|---|---|---:|---|
| 1 | `JobId` | Int32 | In | 1 | Positive identifier unique within the cyclic-job table. |
| 2 | `FrameHex` | String | In | empty | Periodic 1..4096-byte frame. |
| 3 | `PeriodMs` | Int32 | In | 1000 | Repeat interval, 1..86400000 ms. |
| 4 | `InitialDelayMs` | Int32 | In | 0 | Delay from creation to first TX, 0..86400000 ms. |
| 5 | `Enabled` | Int32 | In | 1 | Nonzero enables scheduling immediately. |
| 6 | `ChecksumMode` | Int32 | In | 0 | 0 none, 1 one's-complement, 2 sum, 3 XOR. |
| 7 | `ChecksumStart` | Int32 | In | 0 | First included payload byte, zero based. |
| 8 | `ChecksumLength` | Int32 | In | 0 | Included payload byte count; positive when checksum enabled. |
| 9 | `ChecksumOffset` | Int32 | In | -1 | Output checksum byte offset; outside payload range. |
| 10 | `AppliedHex` | String | Out | empty | Stored frame after checksum generation. |
| 11 | `ActiveCycles` | Int32 | Out | 0 | Cyclic jobs in the table, enabled or disabled. |

Frame size is 1..4096. Period is 1..86400000 ms; initial delay is
0..86400000 ms. `AppliedHex` contains the generated checksum.

### `SRCSerial_cycleUpdate`

| Order | Name | Type | Dir. | Default | Meaning |
|---:|---|---|---|---:|---|
| 1 | `JobId` | Int32 | In | 1 | Existing cyclic job to update. |
| 2 | `FrameHex` | String | In | empty | Nonempty complete replacement; empty preserves frame. |
| 3 | `ByteOffset` | Int32 | In | -1 | Single-byte update offset; -1 disables byte update. |
| 4 | `ByteValue` | Int32 | In | 0 | Replacement byte 0..255 when offset is enabled. |
| 5 | `PeriodMs` | Int32 | In | -1 | New positive period; -1 preserves current period. |
| 6 | `Enabled` | Int32 | In | -1 | -1 unchanged, 0 disabled, 1 enabled. |
| 7 | `RecalculateChecksum` | Int32 | In | 1 | Nonzero regenerates the create-time checksum after edits. |
| 8 | `AppliedHex` | String | Out | empty | Complete stored frame after atomic update. |
| 9 | `ActiveCycles` | Int32 | Out | 0 | Cyclic jobs remaining in the table. |

Empty `FrameHex`, offset -1, period -1, and enabled -1 mean unchanged. A
nonempty frame replacement occurs before the optional byte update. The complete
frame/checksum swap is atomic relative to worker scheduling.

### `SRCSerial_cycleDestroy`

| Order | Name | Type | Dir. | Default | Meaning |
|---:|---|---|---|---:|---|
| 1 | `JobId` | Int32 | In | 1 | Cyclic job to remove. |
| 2 | `Found` | Int32 | Out | 0 | 1 if the job existed and was removed. |
| 3 | `ActiveCycles` | Int32 | Out | 0 | Cyclic jobs remaining. |

An absent ID succeeds with `Found=0`.

## RX-triggered response jobs

Up to 32 response IDs exist. Each matching job sends; make multiple job match
conditions or trigger-count ranges mutually exclusive.

Response mode 0 sends after `ResponseDelayMs`, even if later RX activity occurs.
Mode 1 additionally requires `QuietGapMs` of silence. For collision safety, any
later RX activity cancels a pending mode-1 response; if those new bytes complete
a matching valid frame, that new frame schedules a fresh response.
`ReplacePending=1` replaces/reschedules a pending response on a newer valid
matching frame received before it was selected for TX.

All response modes also apply a backlog safety guard. A response scheduled by
the current receive pass is withheld when that pass receives more than one
frame's worth of bytes, parses multiple valid frames, or leaves bytes from the
next frame buffered. A late response is already outside its intended slot and
could collide with the next request. Each suppression increments
`ResponseSuppressedCount` and records `RESPONSE_SUPPRESSED_BACKLOG` with the
received, parsed-frame, and residual-byte counts.

### `SRCSerial_responseCreate`

| Order | Name | Type | Dir. | Default | Meaning |
|---:|---|---|---|---:|---|
| 1 | `JobId` | Int32 | In | 1 | Positive identifier unique within the response-job table. |
| 2 | `FrameHex` | String | In | empty | 1..4096-byte response template. |
| 3 | `TriggerOffset` | Int32 | In | -1 | -1 matches every valid frame; otherwise zero-based RX byte offset. |
| 4 | `TriggerValue` | Int32 | In | 0 | Expected byte value before masking, 0..255. |
| 5 | `TriggerMask` | Int32 | In | 255 | Bit mask applied to RX byte and trigger value; 0 matches any value. |
| 6 | `ResponseMode` | Int32 | In | 1 | 0 delay-only; 1 collision-safe quiet-gap response. |
| 7 | `ResponseDelayMs` | Int32 | In | 0 | Earliest delay after a matching valid frame, 0..60000 ms. |
| 8 | `QuietGapMs` | Int32 | In | 1 | Mode-1 silence required after trigger; later RX cancels the stale response. |
| 9 | `ReplacePending` | Int32 | In | 1 | Nonzero lets a newer matching trigger replace an existing pending response. |
| 10 | `Enabled` | Int32 | In | 1 | Nonzero permits matching frames to schedule responses. |
| 11 | `TriggerSkipCount` | Int32 | In | 0 | Initial matching valid frames ignored before scheduling. |
| 12 | `SendCountLimit` | Int32 | In | 0 | Maximum successful sends; 0 is unlimited. |
| 13 | `ChecksumMode` | Int32 | In | 0 | 0 none, 1 one's-complement, 2 sum, 3 XOR. |
| 14 | `ChecksumStart` | Int32 | In | 0 | First response payload byte included in checksum. |
| 15 | `ChecksumLength` | Int32 | In | 0 | Included response payload byte count. |
| 16 | `ChecksumOffset` | Int32 | In | -1 | Generated checksum byte offset; outside payload range. |
| 17 | `AppliedHex` | String | Out | empty | Stored response after checksum generation. |
| 18 | `ActiveResponses` | Int32 | Out | 0 | Response jobs in the table, enabled or disabled. |

`TriggerOffset=-1` matches every valid configured RX frame. Otherwise the
masked RX byte must match the masked value. `TriggerSkipCount` ignores initial
matches; `SendCountLimit=0` is unlimited. A first/steady sequence uses one job
with skip 0/limit 1 and another with skip 1/limit 0.

### `SRCSerial_responseUpdate`

| Order | Name | Type | Dir. | Default | Meaning |
|---:|---|---|---|---:|---|
| 1 | `JobId` | Int32 | In | 1 | Existing response job to update. |
| 2 | `FrameHex` | String | In | empty | Nonempty complete replacement; empty preserves frame. |
| 3 | `ByteOffset` | Int32 | In | -1 | Single-byte update offset; -1 disables byte update. |
| 4 | `ByteValue` | Int32 | In | 0 | Replacement byte 0..255 when offset is enabled. |
| 5 | `ResponseMode` | Int32 | In | -1 | -1 unchanged, 0 delay-only, 1 quiet-gap. |
| 6 | `ResponseDelayMs` | Int32 | In | -1 | -1 unchanged; otherwise new 0..60000 ms delay. |
| 7 | `QuietGapMs` | Int32 | In | -1 | -1 unchanged; otherwise new mode-1 silence requirement. |
| 8 | `ReplacePending` | Int32 | In | -1 | -1 unchanged, 0 retain pending, 1 replace on newer match. |
| 9 | `Enabled` | Int32 | In | -1 | -1 unchanged, 0 disable/cancel pending, 1 enable. |
| 10 | `ResetTriggerCounter` | Int32 | In | 0 | Nonzero resets skip/send counters and cancels pending TX. |
| 11 | `RecalculateChecksum` | Int32 | In | 1 | Nonzero regenerates create-time checksum after frame edits. |
| 12 | `AppliedHex` | String | Out | empty | Complete stored response after atomic update. |
| 13 | `ActiveResponses` | Int32 | Out | 0 | Response jobs remaining in the table. |

Empty/-1 values mean unchanged. Disabling cancels pending TX. Resetting the
trigger counter restarts skip/limit sequencing and cancels pending TX. Frame and
single-byte updates are atomic and can regenerate the create-time checksum.

### `SRCSerial_responseDestroy`

| Order | Name | Type | Dir. | Default | Meaning |
|---:|---|---|---|---:|---|
| 1 | `JobId` | Int32 | In | 1 | Response job to remove. |
| 2 | `Found` | Int32 | Out | 0 | 1 if the job existed and was removed. |
| 3 | `ActiveResponses` | Int32 | Out | 0 | Response jobs remaining. |

## Worker RX queue

The ring retains candidates that began with the configured ID, including bad-
checksum frames. Bad-ID bytes used for resynchronization are counted and
dropped. At capacity, the oldest frame is discarded.

### `SRCSerial_rxGetCount`

| Order | Name | Type | Dir. | Default | Meaning |
|---:|---|---|---|---:|---|
| 1 | `FramesAvailable` | Int32 | Out | 0 | Complete retained frames available to read. |
| 2 | `EventsAvailable` | Int32 | Out | 0 | Worker events available to read. |
| 3 | `StreamBytes` | Int32 | Out | 0 | Incomplete bytes currently held by frame reassembly. |

`StreamBytes` is the incomplete reassembly depth.

### `SRCSerial_rxReadFrame`

| Order | Name | Type | Dir. | Default | Meaning |
|---:|---|---|---|---:|---|
| 1 | `Remove` | Int32 | In | 1 | 0 peeks oldest frame; nonzero consumes it. |
| 2 | `Data` | Int32 array 0..4095 | Out | zeros | Raw frame bytes as Int32 0..255. |
| 3 | `Found` | Int32 | Out | 0 | 1 when a frame was available. |
| 4 | `BytesRead` | Int32 | Out | 0 | Valid elements in `Data`. |
| 5 | `Hex` | String | Out | empty | Same frame as uppercase space-separated hex. |
| 6 | `Sequence` | Int32 | Out | 0 | Monotonic retained-frame sequence number. |
| 7 | `TimestampUtc` | String | Out | empty | UTC timestamp captured when parsed. |
| 8 | `AgeMs` | Int32 | Out | -1 | Software age from parse timestamp to read. |
| 9 | `ChecksumValid` | Int32 | Out | 0 | 1 when configured RX checksum validated. |
| 10 | `FramesRemaining` | Int32 | Out | 0 | Complete frames queued after peek/consume. |

Returns the oldest frame. `Remove=0` peeks; `Remove=1` consumes. An empty queue
is successful with `Found=0`.

### `SRCSerial_rxClear`

| Order | Name | Type | Dir. | Default | Meaning |
|---:|---|---|---|---:|---|
| 1 | `ClearFrames` | Int32 | In | 1 | Nonzero clears complete frames and incomplete stream bytes. |
| 2 | `ClearEvents` | Int32 | In | 1 | Nonzero clears the worker event ring. |
| 3 | `ClearCounters` | Int32 | In | 0 | Nonzero clears counters and latency maxima. |
| 4 | `Cleared` | Int32 | Out | 0 | 1 after the requested clear operation succeeds. |

Clearing frames also discards incomplete reassembly bytes. This action does not
destroy jobs or stop the worker.
