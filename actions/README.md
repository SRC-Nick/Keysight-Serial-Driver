# Action definitions

These `.umd` files are generated using the installed TestExec UTA serialization
API by `SRCSerialTools generate`. They are standard C measurement definitions,
target `SRCSerial.dll`, and invoke the matching export as the initiate entry.

Regenerate them from the repository root after changing the public parameter
contract. Inspect any file with:

```powershell
Test\Release\SRCSerialTools.exe inspect actions\SRCSerial_start.umd
```

