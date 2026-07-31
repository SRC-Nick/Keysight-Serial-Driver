# Changelog

## Unreleased

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
