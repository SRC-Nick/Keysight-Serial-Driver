# Changelog

## Unreleased

- Fixed `SRCSerial_setMoxaPortMode` overwriting its `InterfaceMode` input with
  the output sentinel `-1` before validation, and added an exported-action UTA
  regression test for the RS-485 2-wire value.
- Added a dedicated JLG TestExec startup routine covering stale-session
  cleanup, Moxa identity/mode checks, guarded mode correction, COM return and
  effective-configuration checks, worker/response readiness, and abort cleanup.
- Added the initial Win32 TestExec serial action library.
- Added text and binary reads/writes, queue inspection, flushing, modem-line
  access, structured errors, and optional logging.
- Added generated TestExec UMD definitions, validation tools, and Moxa UPort
  deployment documentation.
- Added overlapped I/O with deterministic write deadlines and parallel
  cancellation.
- Added effective-configuration, session-state, Windows COM-port enumeration,
  accumulated framing/parity/overrun diagnostics, and bounded traffic logging.
- Added explicit startup DTR/RTS modes, timed control-line pulses, and transmit
  draining.
- Added hex read/write helpers, inter-byte-idle framing, and atomic
  text/hex/byte-array transactions with delays and timeout retries.
- Expanded the Action Wizard reference with every parameter, direction,
  default, enum, array bound, and usage example.
- Added a reproducible Win32 package builder and a separate
  `windows7-full-build` branch workflow for checked, transferable DLL, UMD,
  symbol, test-tool, metadata, PE-report, ZIP, and checksum artifacts.
- Documented source/full-build cloning, private-repository authentication, the
  final Git for Windows version supporting Windows 7, and a preferred
  no-build/no-Git station deployment workflow.
- Added experimental, driver-version-guarded Moxa UPort electrical-mode query
  and set actions using the locally observed 4.3.0.0 registry convention,
  dynamic COM-to-instance matching, readback verification, and optional
  SetupAPI property refresh.
- Added a generic fixed-frame background protocol worker with exclusive COM
  ownership, bounded RX/event queues, high-resolution timing/status, silence
  detection, and worker-safe manual TX.
- Added create/update/destroy cyclic TX and valid-RX-triggered response jobs,
  immediate/quiet-gap scheduling, checksum generation, atomic TX byte updates,
  and first/steady response sequencing controls.
- Added 14 TestExec actions/UMDs plus a product-specific JLG joystick TestExec
  pseudocode plan covering startup, frame decoding, ECM value updates, live-loss
  recovery, fault handling, cyclic validation, and production acceptance gates.
