# JLG Joystick UUT: TestExec serial-worker example

## Purpose and current confidence

This example shows how a Keysight TestExec plan can emulate the JLG ground
controller/ECM without putting the approximately 10 ms request/response loop in
TestExec. `SRCSerial.dll` owns the COM port on a background thread; TestExec
configures response jobs, updates response bytes, and reads buffered UUT frames.

This is both a sample product plan and a hardware-validation plan. The frame
format and checksum are well supported by the available captures, but the exact
startup acceptance sequence remains an open hardware finding. In particular,
the capture contains an initial `74 00 00 8B` ECM frame followed by normal
`74 0A 00 81` frames, while other trials reportedly entered the latched fault
despite repeated checksum-correct `74 0A 00 81` responses. Do not turn this
example into a production limit until scope-correlated testing proves the
required startup response sequence, count, and timing.

Source evidence:

- `Findings_protocol_analysis.xlsx`, `Protocol Analysis` and `Startup Fault Modes`.
- `Keysight_Serial_Driver_Protocol_Worker_Requirements.md`.

## Known protocol

| Property | Value |
|---|---|
| Physical interface | 2-wire RS-485 |
| Serial framing | 19200 baud, 8 data bits, even parity, 1 stop bit |
| UUT frame | 6 bytes, ID `6A` at offset 0 |
| UUT checksum | `0xFF - (sum(bytes[0..4]) & 0xFF)` at offset 5 |
| ECM frame | 4 bytes, ID `74` at offset 0 |
| ECM checksum | `0xFF - (sum(bytes[0..2]) & 0xFF)` at offset 3 |
| Normal ECM candidate | `74 0A 00 81` |
| Captured first ECM candidate | `74 00 00 8B` |
| Approximate UUT period | 10 ms |

UUT byte offsets are zero based in this document and in the worker actions:

| Offset | Meaning |
|---:|---|
| 0 | ID, normally `6A` |
| 1 | Forward/back signed Int8, or `6D`/`6E` fault marker |
| 2 | Left/right signed Int8, or `6D`/`6E` fault marker |
| 3 | Speed code: `00`, `03`, `06`, `09`, `0C` for speeds 1-5 |
| 4 | Buttons: `08` Lift, `10` Tract, `20` Trigger, `40` Horn |
| 5 | Checksum |

ECM response offsets:

| Offset | Meaning |
|---:|---|
| 0 | ID `74` |
| 1 | Candidate battery value; `0A` represents 100%, `08` represents 80% |
| 2 | Status: bit `02` Lift LED, bit `04` Tract LED |
| 3 | Checksum |

## Station preparation

1. Use an isolated RS-485 adapter and validate D+/D- polarity, signal reference,
   termination, and bias with the actual fixture topology.
2. Ensure no terminal program has the COM port open.
3. Configure the Moxa UPort for RS-485 2-wire in Device Manager. The experimental
   `SRCSerial_setMoxaPortMode` action may be used only after its registry mapping
   and driver version have been qualified on that station.
4. Keep UUT power off until the COM session, worker, and response jobs are all
   active.
5. Attach an oscilloscope or logic analyzer for initial acceptance. Software
   timestamps measure the DLL path; the scope establishes physical bus release,
   adapter direction latency, polarity, and collision behavior.

## Preferred TestExec sequence

The following is pseudocode. Each call must check `Success`; a failed setup call
branches to cleanup without applying UUT power.

For a reusable startup-only routine that detects and closes a stale DLL
session, verifies or corrects the Moxa electrical mode, waits for the COM port
to return, confirms the effective serial configuration, and installs the worker
response before UUT power is enabled, see
[`JLG_Startup_Routine.md`](JLG_Startup_Routine.md). That routine is the preferred
entry point for a station testplan; the shorter steps below explain the
protocol-worker configuration used by the rest of this example.

### 1. Open and configure the port

```text
CALL SRCSerial_stop()

CALL SRCSerial_start(
    Port=Station.SerialPort,
    BaudRate=19200,
    DataBits=8,
    StopBits=1,
    Parity=2,                 // even
    FlowControl=0,
    DTRMode=-1,
    RTSMode=-1,
    ReadTimeoutMs=100,
    WriteTimeoutMs=100,
    FlushOnOpen=1,
    Logging=1)

ASSERT Success == 1
```

Use logging level 1 for production. Level 2 writes every RX/TX frame to disk and
is intended for bounded engineering runs; the worker's in-memory event ring is
the preferred timing-path trace.

### 2. Start the fixed-frame receive worker

```text
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

ASSERT Success == 1 AND WorkerRunning == 1
```

While the worker is running, do not call the normal `read*`, `write*`,
`transact`, `flush`, or line-changing actions. They return error `-1012` because
the worker exclusively owns COM traffic. `getConfiguration`, `getDiagnostics`,
worker status, RX queue, and event actions remain available.

### 3A. Baseline single-response experiment

Use this configuration when testing the working assumption that every valid UUT
frame accepts `74 0A 00 81`:

```text
CALL SRCSerial_responseCreate(
    JobId=100,
    FrameHex="74 0A 00 00",   // checksum byte is recalculated
    TriggerOffset=-1,          // every valid configured RX frame
    TriggerValue=0,
    TriggerMask=255,
    ResponseMode=1,            // quiet-gap mode
    ResponseDelayMs=0,
    QuietGapMs=1,
    ReplacePending=1,
    Enabled=1,
    TriggerSkipCount=0,
    SendCountLimit=0,          // unlimited
    ChecksumMode=1,
    ChecksumStart=0,
    ChecksumLength=3,
    ChecksumOffset=3)

ASSERT Success == 1
ASSERT AppliedHex == "74 0A 00 81"
```

`QuietGapMs=1` is the starting value, not a universal limit. Compare worker
`LastResponseLatencyUs`/`MaxResponseLatencyUs` with scope measurements and tune
the quiet gap if the adapter releases or enables the two-wire transmitter late.

### 3B. Optional two-stage startup experiment

Do not enable this at the same time as job 100. It expresses the captured
first-frame/steady-frame sequence without hardcoding JLG behavior in the DLL:

```text
// First valid UUT frame only -> 74 00 00 8B
CALL SRCSerial_responseCreate(
    JobId=101, FrameHex="74 00 00 00",
    TriggerOffset=-1, TriggerValue=0, TriggerMask=255,
    ResponseMode=1, ResponseDelayMs=0, QuietGapMs=1,
    ReplacePending=1, Enabled=1,
    TriggerSkipCount=0, SendCountLimit=1,
    ChecksumMode=1, ChecksumStart=0, ChecksumLength=3, ChecksumOffset=3)

ASSERT AppliedHex == "74 00 00 8B"

// Skip first valid UUT frame, then respond indefinitely -> 74 0A 00 81
CALL SRCSerial_responseCreate(
    JobId=102, FrameHex="74 0A 00 00",
    TriggerOffset=-1, TriggerValue=0, TriggerMask=255,
    ResponseMode=1, ResponseDelayMs=0, QuietGapMs=1,
    ReplacePending=1, Enabled=1,
    TriggerSkipCount=1, SendCountLimit=0,
    ChecksumMode=1, ChecksumStart=0, ChecksumLength=3, ChecksumOffset=3)

ASSERT AppliedHex == "74 0A 00 81"
```

`TriggerSkipCount` and `SendCountLimit` are generic response-job sequencing
controls. Use `SRCSerial_responseUpdate(ResetTriggerCounter=1)` before repeating
the startup experiment without recreating the jobs.

### 4. Power the UUT and qualify startup

```text
CALL Fixture.SetInputsNeutral()
CALL Fixture.PowerUutOn()

WAIT 100 ms                         // not part of the 10 ms response loop

CALL SRCSerial_workerGetStatus(ResetCounters=0)
ASSERT WorkerRunning == 1
ASSERT WorkerLastErrorCode == 0
ASSERT ValidRxFrameCount >= Station.MinimumStartupFrames
ASSERT ResponseTxCount >= Station.MinimumStartupResponses
ASSERT ChecksumErrorCount == 0
ASSERT LastValidRxAgeMs >= 0 AND LastValidRxAgeMs < 30
LOG LastResponseLatencyUs, MaxResponseLatencyUs
```

Do not lock `MinimumStartupFrames`, the exact wait, or a response-latency limit
until the real ECM startup sequence is captured and accepted on repeated power
cycles. The workbook shows observed startup-neutral windows of roughly 136-204
ms in failed trials, which is why all jobs must exist before power is applied.

### 5. Read and decode buffered frames

```text
CALL SRCSerial_rxGetCount()

WHILE FramesAvailable > 0
    CALL SRCSerial_rxReadFrame(Remove=1)
    ASSERT Success == 1 AND Found == 1 AND BytesRead == 6

    UutId       = Data[0]
    ForwardBack = SignedInt8(Data[1])
    LeftRight   = SignedInt8(Data[2])
    SpeedCode   = Data[3]
    Buttons     = Data[4]

    Lift    = (Buttons AND 0x08) != 0
    Tract   = (Buttons AND 0x10) != 0
    Trigger = (Buttons AND 0x20) != 0
    Horn    = (Buttons AND 0x40) != 0

    IF Data[1] == 0x6E AND Data[2] == 0x6E
        State = "LATCHED_STARTUP_FAULT"
        BRANCH PowerCycleAndFailStartup
    ELSE IF Data[1] == 0x6D AND Data[2] == 0x6D
        State = "RECOVERABLE_ECM_LIVE_LOSS"
    ELSE
        State = "POSITION_VALID"
    END IF
END WHILE
```

The DLL deliberately returns raw generic frames instead of embedding this JLG
field map. TestExec owns product-specific decoding and limits.

### 6. Atomically update ECM values

For the single-response job, set battery to 80%:

```text
CALL SRCSerial_responseUpdate(
    JobId=100,
    FrameHex="",              // keep existing frame
    ByteOffset=1,
    ByteValue=8,
    ResponseMode=-1,
    ResponseDelayMs=-1,
    QuietGapMs=-1,
    ReplacePending=-1,
    Enabled=-1,
    ResetTriggerCounter=0,
    RecalculateChecksum=1)

ASSERT AppliedHex == "74 08 00 83"
```

Set Lift LED status, then clear it:

```text
CALL SRCSerial_responseUpdate(JobId=100, ByteOffset=2, ByteValue=2,
                              RecalculateChecksum=1, all-other-fields=-1)
ASSERT AppliedHex == "74 08 02 81"

CALL SRCSerial_responseUpdate(JobId=100, ByteOffset=2, ByteValue=0,
                              RecalculateChecksum=1, all-other-fields=-1)
ASSERT AppliedHex == "74 08 00 83"
```

For the two-stage experiment, update job 102 instead. Each update swaps the
complete active frame while holding the short worker-state lock, so the timing
thread sees either the old complete frame or the new complete frame.

### 7. Recoverable live-loss test

Run only after startup has been proven stable:

```text
CALL SRCSerial_responseUpdate(JobId=ActiveSteadyJob,
                              Enabled=0, all-other-fields=-1)
WAIT Station.LiveLossWaitMs
READ queued frames until Data[1]==0x6D AND Data[2]==0x6D
ASSERT recoverable-live-loss observed

CALL SRCSerial_responseUpdate(JobId=ActiveSteadyJob,
                              Enabled=1, all-other-fields=-1)
WAIT Station.RecoveryWaitMs
ASSERT latest valid frame no longer contains 6D/6D
```

If `6E/6E` appears, classify the test as a latched startup/no-valid-ECM fault,
capture events and scope evidence, then power-cycle. Do not attempt to treat
`6E` as a joystick position.

### 8. Drain diagnostic evidence and clean up

```text
CALL SRCSerial_workerGetStatus(ResetCounters=0)
LOG all worker counters and latency outputs

DO
    CALL SRCSerial_workerReadEvents(MaxEvents=100, ClearAfterRead=1)
    APPEND EventsText TO TestReport.SerialTrace
WHILE EventsRemaining > 0

CALL SRCSerial_responseDestroy(JobId=each-created-response)
CALL SRCSerial_workerStop(ClearState=1)
CALL SRCSerial_stop()
CALL Fixture.PowerUutOff()
```

Put equivalent calls in the TestExec abort/finally path. If a plan aborts but
the TestExec process remains alive and no cleanup step runs, the worker remains
active by design and continues servicing the UUT. A later `SRCSerial_start` or
`SRCSerial_stop` stops the worker first. If the process terminates, Windows
closes the COM handle. Never leave an energized fixture depending on implicit
process cleanup.

## Cyclic-job validation

The JLG ECM message should normally use an RX-triggered response job. A free-
running cyclic job can collide with UUT traffic on a 2-wire bus, so do not add
one to the JLG production sequence unless scope testing proves that the protocol
requires it.

Validate the generic cyclic API on a loopback or second adapter:

```text
CALL SRCSerial_cycleCreate(
    JobId=200, FrameHex="55 AA 00",
    PeriodMs=20, InitialDelayMs=10, Enabled=1,
    ChecksumMode=3, ChecksumStart=0, ChecksumLength=2, ChecksumOffset=2)
ASSERT AppliedHex == "55 AA FF"

CALL SRCSerial_cycleUpdate(
    JobId=200, FrameHex="", ByteOffset=0, ByteValue=0x5A,
    PeriodMs=-1, Enabled=-1, RecalculateChecksum=1)
ASSERT AppliedHex == "5A AA F0"

CALL SRCSerial_cycleDestroy(JobId=200)
ASSERT Found == 1
```

## Production acceptance gates

- At least 30 repeated power cycles start without `6E/6E` or hard silence.
- The selected startup response sequence is proven against a real controller
  capture, not inferred only from checksum validity.
- Scope evidence shows no 2-wire collision and confirms the chosen quiet gap.
- `MaxResponseLatencyUs` remains below the station limit under normal TestExec
  load; the limit is established from successful hardware trials.
- RX checksum, bad-ID, dropped-byte, Windows framing, parity, and overrun counts
  remain zero during the functional sequence.
- Disabling steady responses produces `6D/6D` and re-enabling responses recovers
  without a power cycle.
- Button, speed, and signed-axis decoding matches every electrical stimulus.
- USB disconnect/reconnect, TestExec abort cleanup, and repeated start/stop are
  exercised on the Windows 7 station.

## Manual and fault-injection TX

`SRCSerial_workerQueueTx` is available while the worker owns the port:

- Mode 0: send at the next scheduler opportunity.
- Mode 1: send after the next valid RX frame.
- Mode 2: send after the current/next configured quiet gap.

Malformed or colliding messages reportedly can produce a hard shutdown. Use
manual TX only in a bounded engineering test with scope capture and an automatic
power-removal path.
