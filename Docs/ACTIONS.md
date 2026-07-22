# TestExec action contract

All actions are standard C actions exported from `SRCSerial.dll`. Parameters
marked output are written by the action. Every action includes these outputs:

- `Success` (Int32): 1 for a completed operation, otherwise 0.
- `ErrorCode` (Int32): zero, a Win32 error, or a negative validation error.
- `ErrorMessage` (String): empty on success.

## SRCSerial_start

Closes any existing session, then opens and configures a single COM port. A
failed open leaves the driver closed.

| Parameter | Type | Default | Meaning |
|---|---:|---:|---|
| Port | String | COM1 | COM1 through COM999; `\\.\COMn` is also accepted. |
| BaudRate | Int32 | 9600 | Positive baud rate supported by the adapter. |
| DataBits | Int32 | 8 | 5 through 8. |
| StopBits | Int32 | 1 | 1=one, 15=one-and-a-half, 2=two. |
| Parity | Int32 | 0 | 0=none, 1=odd, 2=even, 3=mark, 4=space. |
| FlowControl | Int32 | 0 | 0=none, 1=XON/XOFF, 2=RTS/CTS, 3=DTR/DSR. |
| ReadTimeoutMs | Int32 | 1000 | Default read deadline. |
| WriteTimeoutMs | Int32 | 1000 | Default write deadline. |
| FlushOnOpen | Int32 | 1 | Nonzero clears receive and transmit queues. |
| Logging | Int32 | 0 | Nonzero creates a log beneath `logs` beside the DLL. |

## SRCSerial_stop

Purges and closes the session. Calling it while closed succeeds.

## SRCSerial_getBufferLength

Returns the current Windows input-queue size in output `BytesAvailable`.
Nothing is consumed.

## SRCSerial_readBytes

- `RequestedCount` (Int32): positive waits for that many bytes; zero takes a
  nonblocking snapshot of all currently queued bytes up to array capacity.
- `TimeoutMs` (Int32): `-1` uses the start-time default; zero is nonblocking.
- `Data` (Int32 array output): unsigned byte values `0..255`.
- `BytesRead`, `TimedOut` (Int32 outputs): partial reads are retained.

The generated UMD provides 4096 array elements. Regenerate the UMD or edit its
array definition in Action Wizard when a larger individual transfer is needed.

## SRCSerial_readString

- `MaxChars` (Int32, default 1024): maximum received byte count.
- `Terminator` (String): optional escaped byte sequence.
- `TimeoutMs` (Int32, default -1): per-call timeout.
- `IncludeTerminator` (Int32): nonzero retains the matched terminator.
- `Text` (String output), `BytesRead`, `TimedOut` (Int32 outputs).

With an empty terminator, the action waits for the first byte and then returns
the currently available group. A NUL byte fails with `-1004`; use `readBytes`
for binary data.

## Write actions

`SRCSerial_writeBytes` accepts `Data`, `Count`, and `TimeoutMs`, validates each
element as `0..255`, and returns `BytesWritten`.

`SRCSerial_writeString` accepts `Text`, an escaped `Suffix`, and `TimeoutMs`,
then returns `BytesWritten`. Strings are transmitted as TestExec's narrow bytes
without Unicode transcoding.

Supported escapes are `\r`, `\n`, `\t`, `\\`, and `\xNN`.

## Maintenance and status actions

- `SRCSerial_flush`: `FlushMask` 1=receive, 2=transmit, 3=both.
- `SRCSerial_setControlLines`: `DTR`, `RTS`, and `Break` use -1=unchanged,
  0=clear, 1=set. Manual RTS or DTR is rejected when its handshake owns it.
- `SRCSerial_getLineStatus`: output `CTS`, `DSR`, `DCD`, and `Ring`. The Moxa
  UPort 1150I does not expose RI, so `Ring` is expected to remain zero there.

## Validation errors

| Code | Meaning |
|---:|---|
| -1001 | Invalid or missing parameter. |
| -1002 | No COM port is open. |
| -1003 | Requested read exceeds the output array. |
| -1004 | Binary NUL encountered by the string API. |
| -1005 | Manual line control conflicts with handshake mode. |
| -1099 | Unexpected exception contained at the DLL boundary. |

