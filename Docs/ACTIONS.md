# TestExec action contract

The complete Action Wizard recreation contract—including exact parameter order,
UTA types, directions, defaults, enum values, array bounds, and examples—is
maintained in [`../actions/README.md`](../actions/README.md). That document and
the checked-in UMD files are the authoritative public interface.

All exports are standard C actions in `SRCSerial.dll`, use `UTAAPI` with an
`HUTAPB` parameter block, and contain exceptions at the action boundary. Every
action returns output `Success`, `ErrorCode`, and `ErrorMessage`; no action
displays a modal dialog.

## Action groups

- Session/discovery: `start`, `stop`, `cancel`, `isOpen`,
  `getConfiguration`, `enumeratePorts`.
- Receive: `getBufferLength`, `readBytes`, `readString`, `readUntilIdle`,
  `readHex`.
- Transmit/transaction: `writeBytes`, `writeString`, `writeHex`, `transact`.
- Queues/control: `flush`, `drainTransmit`, `setControlLines`,
  `pulseControlLine`, `getLineStatus`.
- Health: `getDiagnostics`.

## Important semantic distinctions

- `flush` purges queued data; `drainTransmit` waits for queued output.
- A read/drain/transaction response timeout normally returns `Success=1` with
  `TimedOut=1`; a partial write is an error.
- `readUntilIdle` is the generic choice for devices whose only frame boundary
  is a quiet interval.
- `transact` keeps write/read atomic relative to other serial actions and
  retries only response timeouts.
- `cancel` is lock-free so a parallel TestExec cleanup path can interrupt an
  action holding the session lock.
- `getDiagnostics` accumulates Windows framing, parity, overrun, buffer-overrun,
  and break observations that would otherwise be cleared by `ClearCommError`.

## Validation errors

| Code | Meaning |
|---:|---|
| -1001 | Invalid or missing parameter. |
| -1002 | No COM port is open. |
| -1003 | Request or response exceeds its UMD array. |
| -1004 | NUL encountered by a string action. |
| -1005 | Manual line control conflicts with handshake ownership. |
| -1006 | Invalid hexadecimal input. |
| -1007 | Text output is too large. |
| -1099 | Unexpected exception contained at the DLL boundary. |
