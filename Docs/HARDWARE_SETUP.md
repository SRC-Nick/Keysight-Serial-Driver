# Hardware and Windows 7 deployment

## Recommended adapter

Use the Moxa UPort 1150I for a one-port station. It supports RS-232, RS-422,
2-wire RS-485, and 4-wire RS-485; includes 2 kV isolation; and exposes a normal
Windows COM port. Moxa's UPort 1100 Windows 7-to-10 WHQL driver is version 3.2.

Official references:

- Product: https://www.moxa.com/en/products/industrial-edge-connectivity/usb-to-serial-converters-usb-hubs/usb-to-serial-converters/uport-1100-series/uport-1150i
- Driver: https://www.moxa.com/en/support/product-support/software-and-documentation?psid=114473
- Manual: https://moxa.com/getmedia/a2924269-6076-4c8f-9c1e-7268e235dde1/moxa-uport-1100-series-manual-v9.0.pdf

For development, use two adapters unless the DUT is already a trustworthy
second endpoint.

## Installation

1. Download the Windows 7-to-10 v3.2 package from Moxa and verify the SHA-512
   shown on Moxa's download page before moving it to the offline tester.
2. Install the driver before connecting the USB cable.
3. Connect the adapter, open Device Manager, and assign a stable COM number.
   Record the adapter serial number. `SRCSerial_enumeratePorts` can report the
   Windows device-instance ID and location so the station can verify that the
   intended adapter is present even if Windows changes its COM number.
4. In the Moxa port configuration, select RS-232, RS-422, RS-485 2-wire, or
   RS-485 4-wire. The default is RS-232.
5. For RS-485, configure termination and bias DIP switches for the actual bus.
   Terminate only at the two physical ends. The UPort provides automatic data
   direction control, so application RTS toggling is not required.

### Experimental programmatic mode selection

The generic Win32 COM API cannot select the UPort electrical interface. The
optional `SRCSerial_getMoxaPortMode` and `SRCSerial_setMoxaPortMode` actions use
an undocumented registry convention observed locally with Moxa driver 4.3.0.0.
They are not a substitute for station qualification.

- Driver 4.3.0.0 observed mapping: 0 RS-232, 1 RS-422, 2 RS-485 2-wire,
  3 RS-485 4-wire.
- The DLL enumerates `MXUPORT\COM` instances and matches `PortName`; never
  hardcode the instance suffix.
- Stop the serial session before changing the mode.
- Registry writes and SetupAPI refresh normally require Administrator rights.
- Default `RestartDevice=0` writes and verifies the registry, then reports that
  restart is required. Restart through Device Manager or unplug/replug.
- `RestartDevice=1` requests a SetupAPI property refresh but may still report
  that Windows requires restart/reboot.
- `TxMode` is read for diagnostics and deliberately not modified.
- The Windows 7 v3.2 driver may use a different layout or behavior. The setter
  rejects it by default because only 4.3.0.0 has been observed. Verify v3.2
  manually before considering an explicit unverified-driver override.

## Pin summary

UPort 1150I DB9 male:

- RS-232: 1 DCD, 2 RxD, 3 TxD, 4 DTR, 5 GND, 6 DSR, 7 RTS, 8 CTS.
- RS-422/4-wire RS-485: 1 TxD-, 2 TxD+, 3 RxD+, 4 RxD-, 5 GND.
- 2-wire RS-485: 3 Data+, 4 Data-, 5 GND.

Confirm polarity against the DUT documentation; A/B labels are not consistent
between all vendors.

## TestExec deployment and rollback

1. Back up the station's TestExec search-path configuration.
2. Copy `SRCSerial.dll` and all 23 `.umd` files to controlled deployment
   folders and add those folders under TestExec System Options.
3. Restart TestExec and confirm the 23 `SRCSerial_*` actions are visible.
4. Run the smoke and loopback plans from `Docs/TESTING.md`.
5. To roll back, remove the new search paths/files and restore the saved system
   configuration. The installation does not replace TestExec's `scomm` files.
