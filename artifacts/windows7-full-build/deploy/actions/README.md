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

| Order | Name | UTA type | Direction | Default |
|---:|---|---|---|---:|
| last-2 | `Success` | `CUtaInt32` | Output | 0 |
| last-1 | `ErrorCode` | `CUtaInt32` | Output | 0 |
| last | `ErrorMessage` | `CUtaString` | Output | empty |

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
| 12 | `Logging` | Int32 | Input | 0 | 0 off, 1 errors/config, 2 traffic, 3 verbose queues. |

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

| Order | Name | Type | Direction | Default |
|---:|---|---|---|---|
| 1 | `Open` | Int32 | Output | 0 |
| 2 | `Port` | String | Output | empty |

This query succeeds even while closed.

### `SRCSerial_getConfiguration`

All parameters are outputs:

| Order | Name | Type | Default |
|---:|---|---|---:|
| 1 | `Open` | Int32 | 0 |
| 2 | `Port` | String | empty |
| 3 | `BaudRate` | Int32 | 0 |
| 4 | `DataBits` | Int32 | 0 |
| 5 | `StopBits` | Int32 | 0 |
| 6 | `Parity` | Int32 | 0 |
| 7 | `FlowControl` | Int32 | 0 |
| 8 | `DTRMode` | Int32 | 0 |
| 9 | `RTSMode` | Int32 | 0 |
| 10 | `ReadTimeoutMs` | Int32 | 0 |
| 11 | `WriteTimeoutMs` | Int32 | 0 |
| 12 | `Logging` | Int32 | 0 |

When open, values reflect the configuration accepted by the adapter driver.

### `SRCSerial_enumeratePorts`

| Order | Name | Type | Direction | Default |
|---:|---|---|---|---|
| 1 | `Ports` | String | Output | empty |
| 2 | `Count` | Int32 | Output | 0 |

Returns one CR/LF-separated record per present Windows Ports-class COM device:

```text
COM name|friendly name|hardware ID|device instance ID|location
```

The device-instance ID commonly contains a USB serial identity. Enumeration
does not open a port and can run while the serial session is closed.

## Read actions

### `SRCSerial_getBufferLength`

| Order | Name | Type | Direction | Default |
|---:|---|---|---|---:|
| 1 | `BytesAvailable` | Int32 | Output | 0 |

Returns the current receive-queue depth without consuming bytes.

### `SRCSerial_readBytes`

| Order | Name | Type | Direction | Default |
|---:|---|---|---|---:|
| 1 | `RequestedCount` | Int32 | Input | 0 |
| 2 | `TimeoutMs` | Int32 | Input | -1 |
| 3 | `Data` | Int32 array 0..4095 | Output | zeros |
| 4 | `BytesRead` | Int32 | Output | 0 |
| 5 | `TimedOut` | Int32 | Output | 0 |

Positive `RequestedCount` waits for exactly that many bytes or the deadline.
Zero performs a nonblocking snapshot of all currently queued bytes up to array
capacity.

### `SRCSerial_readString`

| Order | Name | Type | Direction | Default |
|---:|---|---|---|---|
| 1 | `MaxChars` | Int32 | Input | 1024 |
| 2 | `Terminator` | String | Input | empty |
| 3 | `TimeoutMs` | Int32 | Input | -1 |
| 4 | `IncludeTerminator` | Int32 | Input | 0 |
| 5 | `Text` | String | Output | empty |
| 6 | `BytesRead` | Int32 | Output | 0 |
| 7 | `TimedOut` | Int32 | Output | 0 |

With a terminator, reads through the complete escaped sequence without consuming
later bytes. With an empty terminator, returns the first group separated by a
short idle interval. A received NUL returns -1004; use bytes or hex instead.
Filling `MaxChars` without finding the requested terminator returns -1003 so a
truncated frame cannot be mistaken for a complete response.

### `SRCSerial_readUntilIdle`

| Order | Name | Type | Direction | Default |
|---:|---|---|---|---:|
| 1 | `MaxBytes` | Int32 | Input | 4096 |
| 2 | `TimeoutMs` | Int32 | Input | -1 |
| 3 | `InterByteTimeoutMs` | Int32 | Input | 20 |
| 4 | `Data` | Int32 array 0..4095 | Output | zeros |
| 5 | `Hex` | String | Output | empty |
| 6 | `BytesRead` | Int32 | Output | 0 |
| 7 | `TimedOut` | Int32 | Output | 0 |

`TimeoutMs` is the deadline for receiving the frame. After the first byte,
`InterByteTimeoutMs` of silence completes the frame. `TimedOut=1` means no byte
arrived by the overall deadline; reaching `MaxBytes` also completes normally.

### `SRCSerial_readHex`

| Order | Name | Type | Direction | Default |
|---:|---|---|---|---:|
| 1 | `RequestedCount` | Int32 | Input | 0 |
| 2 | `MaxBytes` | Int32 | Input | 1024 |
| 3 | `TimeoutMs` | Int32 | Input | -1 |
| 4 | `Hex` | String | Output | empty |
| 5 | `BytesRead` | Int32 | Output | 0 |
| 6 | `TimedOut` | Int32 | Output | 0 |

The count semantics match `readBytes`; output resembles `02 31 FF 7A`.

## Write and transaction actions

### `SRCSerial_writeBytes`

| Order | Name | Type | Direction | Default |
|---:|---|---|---|---:|
| 1 | `Data` | Int32 array 0..4095 | Input | zeros |
| 2 | `Count` | Int32 | Input | 0 |
| 3 | `TimeoutMs` | Int32 | Input | -1 |
| 4 | `BytesWritten` | Int32 | Output | 0 |

`Count` must fit the array and every used value must be `0..255`.

### `SRCSerial_writeString`

| Order | Name | Type | Direction | Default |
|---:|---|---|---|---|
| 1 | `Text` | String | Input | empty |
| 2 | `Suffix` | String | Input | empty |
| 3 | `TimeoutMs` | Int32 | Input | -1 |
| 4 | `BytesWritten` | Int32 | Output | 0 |

Sends TestExec narrow string bytes without Unicode conversion, then the decoded
suffix. Example: `Text="READ?", Suffix="\r\n"`.

### `SRCSerial_writeHex`

| Order | Name | Type | Direction | Default |
|---:|---|---|---|---|
| 1 | `Hex` | String | Input | empty |
| 2 | `TimeoutMs` | Int32 | Input | -1 |
| 3 | `BytesWritten` | Int32 | Output | 0 |

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

| Order | Name | Type | Direction | Default |
|---:|---|---|---|---:|
| 1 | `FlushMask` | Int32 | Input | 3 |

Mask `1` discards receive, `2` discards transmit, and `3` discards both. This
purges bytes; it does not wait for transmit completion.

### `SRCSerial_drainTransmit`

| Order | Name | Type | Direction | Default |
|---:|---|---|---|---:|
| 1 | `TimeoutMs` | Int32 | Input | -1 |
| 2 | `TimedOut` | Int32 | Output | 0 |

Waits for the driver transmit queue to reach zero without discarding it.

### `SRCSerial_setControlLines`

| Order | Name | Type | Direction | Default |
|---:|---|---|---|---:|
| 1 | `DTR` | Int32 | Input | -1 |
| 2 | `RTS` | Int32 | Input | -1 |
| 3 | `Break` | Int32 | Input | -1 |

Each value is `-1` unchanged, `0` clear, or `1` set. Manual DTR/RTS changes are
rejected when the line is owned by handshake mode.

### `SRCSerial_pulseControlLine`

| Order | Name | Type | Direction | Default |
|---:|---|---|---|---:|
| 1 | `Line` | Int32 | Input | 0 |
| 2 | `State` | Int32 | Input | 0 |
| 3 | `DurationMs` | Int32 | Input | 100 |
| 4 | `RestoreState` | Int32 | Input | -1 |

`Line` is `0` DTR, `1` RTS, or `2` break. `State` is 0/1.
`RestoreState=-1` restores the tracked prior state; otherwise it restores the
specified 0/1 value. Maximum duration is 60000 ms.

### `SRCSerial_getLineStatus`

| Order | Name | Type | Direction | Default |
|---:|---|---|---|---:|
| 1 | `CTS` | Int32 | Output | 0 |
| 2 | `DSR` | Int32 | Output | 0 |
| 3 | `DCD` | Int32 | Output | 0 |
| 4 | `Ring` | Int32 | Output | 0 |

An adapter that does not expose a signal reports it inactive.

## Diagnostics action

### `SRCSerial_getDiagnostics`

| Order | Name | Type | Direction | Default |
|---:|---|---|---|---:|
| 1 | `ResetAfterRead` | Int32 | Input | 0 |
| 2 | `FrameErrors` | Int32 | Output | 0 |
| 3 | `ParityErrors` | Int32 | Output | 0 |
| 4 | `OverrunErrors` | Int32 | Output | 0 |
| 5 | `BufferOverrunErrors` | Int32 | Output | 0 |
| 6 | `BreakCount` | Int32 | Output | 0 |
| 7 | `RxBytesQueued` | Int32 | Output | 0 |
| 8 | `TxBytesQueued` | Int32 | Output | 0 |
| 9 | `TotalRxBytes` | Int32 | Output | 0 |
| 10 | `TotalTxBytes` | Int32 | Output | 0 |
| 11 | `LastWin32Error` | Int32 | Output | 0 |

The first five counters count observations of their corresponding Win32
communication error flags. Totals saturate at `2,147,483,647` and reset on a
successful new start. If `ResetAfterRead` is nonzero, accumulated counters are
cleared after copying the returned values; current queue depths are retained.

Framing and parity counts are strong evidence of electrical polarity, signal
level, baud, parity, stop-bit, grounding, or noise problems. They do not identify
the physical cause by themselves.
