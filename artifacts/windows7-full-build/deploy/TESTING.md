# Verification procedure

## Local automated checks

Run `Test\build_v143.cmd` on the current developer machine. It builds the DLL
and tools, inspects all UMDs, tests pure parsing/validation behavior, loads
the DLL through the UTA runtime, checks structured closed-port errors, and
lists PE exports/imports.

The current public surface contains 21 exports and 21 matching UMD files.

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
- Tag `v0.1.0` only after both required electrical modes pass on the target.
