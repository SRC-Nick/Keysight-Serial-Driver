# Verification procedure

## Local automated checks

Run `Test\build_v143.cmd` on the current developer machine. It builds the DLL
and tools, inspects all UMDs, tests pure parsing/validation behavior, loads
the DLL through the UTA runtime, checks structured closed-port errors, and
lists PE exports/imports.

The current public surface contains 37 exports and 37 matching UMD files.

The checked-in release must additionally build with v142:

```powershell
msbuild SRCSerial.sln /m /p:Configuration=Release /p:Platform=Win32
```

## RS-232 loopback

1. Configure the UPort for RS-232 and connect TxD to RxD.
2. Start at 9600-8-N-1, write text with `\r\n`, and read it by terminator.
3. Send `00 01 7F 80 FF` with the byte actions and verify exact recovery.
4. Test a COM number above 9, nonblocking reads, partial timeout reads, receive
   flush, repeated start/stop, and USB disconnect/reconnect.
5. For hardware flow control, also loop RTS to CTS and validate RTS/CTS mode.
6. Exercise DTR/RTS disabled/enabled modes and verify actual connector voltage
   and polarity with appropriate instrumentation.
7. Create deliberate baud/parity mismatches and confirm framing/parity
   diagnostics increment, then reset them.
8. Verify `readUntilIdle`, all three transaction response modes, transaction
   retry counts, hex helpers, drain versus purge, control-line pulse restore,
   and parallel cancellation.

## RS-485 2-wire

1. Use two endpoints, a single twisted pair, common reference where required,
   and termination only at the physical ends.
2. Configure both adapters for 2-wire RS-485 with the same serial settings.
3. Exchange repeated request/response frames and back-to-back binary frames.
4. Verify there is no application RTS toggling and no self-echo assumption.
5. Disconnect/reconnect USB and confirm actions fail cleanly until start is
   called again.
6. Validate transaction pre/post delays and the Modbus/device-specific quiet
   interval selected by the testplan.

## Windows 7/TestExec acceptance

- Confirm the v142 Win32 DLL loads in TestExec SL 7.1 without missing imports.
- Confirm all UMD defaults, input/output directions, and array sizes.
- Exercise every export from a testplan and verify no modal dialogs appear.
- Compare `enumeratePorts` friendly name, hardware ID, instance ID, and location
  against Device Manager and verify the station can identify its intended
  adapter after USB-port changes.
- Abort a plan without stop, verify the next start replaces the stale session,
  and exercise cancel from a parallel cleanup path.
- Record adapter serial number, driver version, COM assignment, electrical
  mode, baud settings, and results in the release validation record.
- Run `SRCSerial_getMoxaPortMode` and compare its COM match, instance ID, driver
  version, `SerInterface`, and `TxMode` with Device Manager and Registry Editor.
- In a disposable/elevated development test, change each Moxa mode, preserve
  `PreviousMode`, refresh or reconnect the device, and verify the physical
  interface. Restore the original mode afterward.
- Confirm a non-elevated mode change returns access denied without modifying
  `SerInterface`; confirm an open matching session returns -1010.
- Confirm an unexpected driver version returns -1009 unless the test explicitly
  enables `AllowUnverifiedDriver`; never use that override for production until
  the driver layout and behavior have been independently qualified.
- Tag `v0.1.0` only after both required electrical modes pass on the target.

## Background-worker acceptance

### Diagnostic logging capture

Use `Logging=2` while investigating worker timing. Level 1 proves configuration
and which jobs were created but intentionally omits per-frame traffic. Level 2
adds asynchronous, high-resolution records for raw RX/TX, valid frame parsing,
response scheduling, sender selection, and completed writes.

Capture at least two seconds with the UUT silent and five seconds with its
normal traffic active. Stop the worker and session normally, then retain the
newest `logs\SRCSerial_*.log` beside the loaded DLL. Confirm that:

- Every `TX_RESPONSE` has a preceding `RX_VALID` and `RESPONSE_SCHEDULE`.
- No `CYCLE_CREATE` or `TX_CYCLE` exists in an RX-only JLG sequence.
- No `MANUAL_QUEUE` or `TX_MANUAL` exists unless explicitly requested.
- `trigger_to_select_us`, `select_to_complete_us`, and
  `trigger_to_complete_us` explain the DLL portion of the scope latency.
- A quiet-UUT capture contains no `RX_COMPLETE` or `TX_RESPONSE`; if it does,
  inspect the logged bytes for echo, noise, or another transmitter.

The file logger is asynchronous to reduce timing perturbation. It is not proof
of physical bus timing; correlate it with the oscilloscope.

Use two endpoints and a scope or logic analyzer. The worker endpoint must be the
only code path accessing its COM handle.

1. Configure fixed length, masked ID, and checksum. Inject split frames,
   multiple frames per read, garbage prefixes, bad IDs, and bad checksums.
2. Verify RX ordering, timestamps, checksum flags, resynchronization, bounded
   ring rollover, and clearing.
3. Measure immediate-response and resettable quiet-gap response behavior.
   Inject later RX bytes before the quiet gap and verify the stale response is
   canceled with a `RESPONSE_CANCEL` event and no delayed physical TX.
4. Update response bytes/checksum under sustained RX; observe only complete old
   or complete new frames.
5. Exercise trigger skip/send limits with first-frame and steady-frame jobs.
6. Create, update, enable/disable, and destroy cyclic jobs; verify periods and
   generated checksums at the other endpoint.
7. Exercise manual TX modes, silence events, event-ring rollover, disconnect,
   worker restart, TestExec abort cleanup, and -1012 foreground-I/O rejection.
8. Compare `LastResponseLatencyUs`/`MaxResponseLatencyUs` to physical scope
   timing; software timing cannot include unobservable adapter TX-enable lag.

Run the product-specific sequence in
[`../Examples/JLG_Joystick_TestExec_Test_Plan.md`](../Examples/JLG_Joystick_TestExec_Test_Plan.md)
with scope-correlated RS-485 timing.
