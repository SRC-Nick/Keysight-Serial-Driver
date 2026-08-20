# JLG safe serial startup routine

## Purpose

This startup-only TestExec pseudocode prepares `SRCSerial.dll` to emulate the
JLG ECM on a qualified Moxa UPort station. It keeps UUT power off until the
adapter is present, the Moxa registry mode is RS-485 2-wire, the COM port has
accepted the required framing, the background worker is running, and a valid
ECM response job is active.

The complete application and validation sequence is in
[`JLG_Joystick_TestExec_Test_Plan.md`](JLG_Joystick_TestExec_Test_Plan.md).
The current protocol finding is 19200 baud, 8 data bits, even parity, and one
stop bit. Resolve any observed 19600-versus-19200 discrepancy with a capture
before changing the station constant.

## Station constants

```text
Station.SerialPort             = "COM1"       // station-specific
Station.RequiredMoxaMode       = 2            // RS-485 2-wire
Station.QualifiedDriverVersion = "4.3.0.0"    // qualify before changing
Station.PortReturnTimeoutMs    = 10000
```

Use a fixed, qualified driver version rather than copying the value returned by
the query into `ExpectedDriverVersion`; copying it would defeat the setter's
version guard. TestExec must be elevated if the routine is permitted to change
the Moxa registry mode or request a device restart.

## Startup pseudocode

Every action must branch to `SerialStartupFailure` when `Success != 1`, unless
the pseudocode explicitly applies a stronger result check. Preserve each
action's `ErrorCode` and `ErrorMessage` before another cleanup action overwrites
the action-result variables.

```text
ROUTINE JLG_SerialStartup

    SerialReady = 0
    SavedErrorCode = 0
    SavedErrorMessage = ""

    // The UUT must not begin its short startup exchange before the worker and
    // response job are active.
    CALL Fixture.PowerUutOff()

    // ------------------------------------------------------------
    // 1. Record and close a session left by an earlier testplan.
    // ------------------------------------------------------------

    CALL SRCSerial_isOpen()
    IF Success != 1
        GOTO SerialStartupFailure
    END IF

    IF Open == 1
        LOG "Closing stale SRCSerial session on " + Port
    END IF

    // stop is idempotent. It also stops a surviving worker, destroys worker
    // jobs, cancels/purges I/O, and closes this DLL's COM handle.
    CALL SRCSerial_stop()
    IF Success != 1
        GOTO SerialStartupFailure
    END IF

    // ------------------------------------------------------------
    // 2. Confirm that the configured Windows port is present.
    // ------------------------------------------------------------

    CALL SRCSerial_enumeratePorts()
    IF Success != 1
        GOTO SerialStartupFailure
    END IF

    LOG Ports
    IF Ports does not contain Station.SerialPort
        SavedErrorCode = -1008
        SavedErrorMessage =
            "Configured Moxa port is not present: " + Station.SerialPort
        GOTO SerialStartupFailureWithSavedError
    END IF

    // ------------------------------------------------------------
    // 3. Match the COM name to a Moxa instance and inspect its mode.
    // ------------------------------------------------------------

    CALL SRCSerial_getMoxaPortMode(
        Port=Station.SerialPort)

    IF Success != 1 OR Found != 1
        GOTO SerialStartupFailure
    END IF

    LOG InterfaceMode, TxMode, InstanceId, DriverVersion

    IF DriverVersion != Station.QualifiedDriverVersion
        SavedErrorCode = -1009
        SavedErrorMessage =
            "Unqualified Moxa driver: " + DriverVersion
        GOTO SerialStartupFailureWithSavedError
    END IF

    // ------------------------------------------------------------
    // 4. Correct a qualified adapter that is in the wrong mode.
    // ------------------------------------------------------------

    IF InterfaceMode != Station.RequiredMoxaMode

        LOG "Changing Moxa mode from " + InterfaceMode +
            " to RS-485 2-wire"

        // The matching SRCSerial session is closed. No terminal program or
        // second TestExec process may own the port.
        CALL SRCSerial_setMoxaPortMode(
            Port=Station.SerialPort,
            InterfaceMode=Station.RequiredMoxaMode,
            ExpectedDriverVersion=Station.QualifiedDriverVersion,
            AllowUnverifiedDriver=0,
            RestartDevice=1)

        IF Success != 1
            GOTO SerialStartupFailure
        END IF

        LOG PreviousMode, CurrentMode, RegistryUpdated,
            RestartAttempted, RestartSucceeded, RestartRequired

        IF Found != 1 OR CurrentMode != Station.RequiredMoxaMode
            SavedErrorCode = -1011
            SavedErrorMessage =
                "Moxa registry mode did not verify as RS-485 2-wire"
            GOTO SerialStartupFailureWithSavedError
        END IF

        IF RestartRequired == 1
            SavedErrorCode = -1011
            SavedErrorMessage =
                "Moxa mode changed; reconnect the USB adapter and rerun startup"
            GOTO SerialStartupFailureWithSavedError
        END IF

        // A successful SetupAPI refresh can temporarily remove the COM port.
        ElapsedMs = 0
        PortPresent = 0
        WHILE ElapsedMs < Station.PortReturnTimeoutMs AND PortPresent == 0
            WAIT 250 ms
            CALL SRCSerial_enumeratePorts()
            IF Success == 1 AND Ports contains Station.SerialPort
                PortPresent = 1
            END IF
            ElapsedMs = ElapsedMs + 250
        END WHILE

        IF PortPresent == 0
            SavedErrorCode = -1008
            SavedErrorMessage =
                "Moxa COM port did not return after mode change"
            GOTO SerialStartupFailureWithSavedError
        END IF

        CALL SRCSerial_getMoxaPortMode(
            Port=Station.SerialPort)
        IF Success != 1 OR Found != 1 OR
           InterfaceMode != Station.RequiredMoxaMode
            GOTO SerialStartupFailure
        END IF
    END IF

    // ------------------------------------------------------------
    // 5. Open the port with the JLG framing.
    // ------------------------------------------------------------

    CALL SRCSerial_start(
        Port=Station.SerialPort,
        BaudRate=19200,
        DataBits=8,
        StopBits=1,
        Parity=2,                 // even
        FlowControl=0,
        DTRMode=-1,
        RTSMode=-1,               // Moxa controls 2-wire TX direction
        ReadTimeoutMs=100,
        WriteTimeoutMs=100,
        FlushOnOpen=1,
        Logging=1)

    IF Success != 1
        // Win32 access denied commonly means another process owns the COM
        // port. stop cannot close a handle owned by another process.
        GOTO SerialStartupFailure
    END IF

    // ------------------------------------------------------------
    // 6. Verify the effective configuration accepted by Windows.
    // ------------------------------------------------------------

    CALL SRCSerial_getConfiguration()
    IF Success != 1 OR Open != 1
        GOTO SerialStartupFailure
    END IF

    IF Port != Station.SerialPort OR
       BaudRate != 19200 OR DataBits != 8 OR StopBits != 1 OR
       Parity != 2 OR FlowControl != 0
        SavedErrorCode = -1001
        SavedErrorMessage =
            "Effective COM configuration does not match JLG requirements"
        GOTO SerialStartupFailureWithSavedError
    END IF

    // ------------------------------------------------------------
    // 7. Start the fixed-frame worker.
    // ------------------------------------------------------------

    CALL SRCSerial_workerStart(
        RxFrameLength=6,
        RxIdOffset=0,
        RxIdValue=106,            // 0x6A
        RxIdMask=255,
        RxChecksumMode=1,         // one's-complement sum
        RxChecksumStart=0,
        RxChecksumLength=5,
        RxChecksumOffset=5,
        RxQueueCapacity=512,
        EventQueueCapacity=1024,
        SilenceTimeoutMs=50,
        PollIntervalMs=1,
        MinimumInterTxMs=0,
        WorkerPriority=1)

    IF Success != 1 OR WorkerRunning != 1
        GOTO SerialStartupFailure
    END IF

    // ------------------------------------------------------------
    // 8. Install a baseline response before applying UUT power.
    // ------------------------------------------------------------

    CALL SRCSerial_responseCreate(
        JobId=100,
        FrameHex="74 0A 00 00",
        TriggerOffset=-1,         // every valid configured RX frame
        TriggerValue=0,
        TriggerMask=255,
        ResponseMode=1,           // quiet-gap response
        ResponseDelayMs=0,
        QuietGapMs=1,
        ReplacePending=1,
        Enabled=1,
        TriggerSkipCount=0,
        SendCountLimit=0,         // unlimited
        ChecksumMode=1,
        ChecksumStart=0,
        ChecksumLength=3,
        ChecksumOffset=3)

    IF Success != 1 OR AppliedHex != "74 0A 00 81"
        GOTO SerialStartupFailure
    END IF

    // ------------------------------------------------------------
    // 9. Final readiness gate.
    // ------------------------------------------------------------

    CALL SRCSerial_workerGetStatus(ResetCounters=0)
    IF Success != 1 OR WorkerRunning != 1 OR WorkerLastErrorCode != 0
        GOTO SerialStartupFailure
    END IF

    SerialReady = 1
    LOG "JLG serial subsystem ready; UUT may now be powered"
    RETURN PASS


SerialStartupFailure:
    SavedErrorCode = ErrorCode
    SavedErrorMessage = ErrorMessage

SerialStartupFailureWithSavedError:
    // Remove power before stopping the timing worker. Preserve the original
    // failure because cleanup actions publish their own action results.
    CALL Fixture.PowerUutOff()
    CALL SRCSerial_cancel()
    CALL SRCSerial_workerStop(ClearState=1)
    CALL SRCSerial_stop()

    SerialReady = 0
    LOG SavedErrorCode, SavedErrorMessage
    RETURN FAIL
END ROUTINE
```

## Required abort and normal-shutdown behavior

Put equivalent cleanup in TestExec's abort/finally path. A later application
sequence may update or inspect the worker, but it must not bypass this cleanup:

```text
CALL Fixture.PowerUutOff()
CALL SRCSerial_cancel()
CALL SRCSerial_workerStop(ClearState=1)
CALL SRCSerial_stop()
```

`SRCSerial_stop` can close only a handle owned by this DLL in the current
TestExec process. It cannot close PuTTY, a Moxa terminal, a second TestExec
process, or another program that owns the COM port. If `SRCSerial_start` returns
Win32 access denied after the stale-session cleanup, leave the UUT off and
identify the external owner.

While the worker is running, do not call foreground `read*`, `write*`,
`transact`, `flush`, `drainTransmit`, or control-line mutation actions. Use the
worker status, RX queue, response job, and event actions described in the full
JLG plan.
