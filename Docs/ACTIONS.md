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
  `getConfiguration`, `enumeratePorts`, `getMoxaPortMode`, `setMoxaPortMode`.
- Receive: `getBufferLength`, `readBytes`, `readString`, `readUntilIdle`,
  `readHex`.
- Transmit/transaction: `writeBytes`, `writeString`, `writeHex`, `transact`.
- Queues/control: `flush`, `drainTransmit`, `setControlLines`,
  `pulseControlLine`, `getLineStatus`.
- Health: `getDiagnostics`.
- Background worker: `workerStart`, `workerStop`, `workerGetStatus`,
  `workerReadEvents`, `workerQueueTx`.
- Worker jobs: `cycleCreate`, `cycleUpdate`, `cycleDestroy`, `responseCreate`,
  `responseUpdate`, `responseDestroy`.
- Worker RX queue: `rxGetCount`, `rxReadFrame`, `rxClear`.

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
- While the background worker runs, it exclusively owns foreground serial I/O;
  TestExec updates jobs and reads worker queues/status instead of timing a loop.
- Response jobs can send immediately or after a resettable RX quiet gap. Cyclic
  jobs are free-running and should not be used on a two-wire bus without
  collision analysis.

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
| -1008 | No Moxa UPort instance matches the COM port. |
| -1009 | Moxa driver version is not the explicitly expected version. |
| -1010 | Matching serial port must be closed before mode change. |
| -1011 | Moxa registry data has an unexpected type or value. |
| -1012 | Background worker owns the COM port. |
| -1013 | Worker action requires a running worker. |
| -1014 | Worker job ID was not found. |
| -1015 | Worker job ID already exists. |
| -1016 | Bounded worker queue/table is full. |
| -1017 | Worker checksum definition is invalid. |
| -1099 | Unexpected exception contained at the DLL boundary. |
