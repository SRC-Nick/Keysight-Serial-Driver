@echo off
setlocal
set "MSBUILD=C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
if not exist "%MSBUILD%" (
  echo MSBuild was not found.
  exit /b 1
)
"%MSBUILD%" SRCSerial.sln /m /p:Configuration=Release /p:Platform=Win32 /p:PlatformToolset=v143 /v:minimal
if errorlevel 1 exit /b 1
Test\Release\SRCSerialTools.exe self-test
if errorlevel 1 exit /b 1
Test\Release\SRCSerialTools.exe action-smoke Release\SRCSerial.dll
if errorlevel 1 exit /b 1
for %%F in (actions\SRCSerial_*.umd) do (
  Test\Release\SRCSerialTools.exe inspect "%%F"
  if errorlevel 1 exit /b 1
)
where dumpbin >nul 2>nul
if not errorlevel 1 (
  dumpbin /headers Release\SRCSerial.dll | findstr /c:"machine (x86)"
  dumpbin /exports Release\SRCSerial.dll
  dumpbin /imports Release\SRCSerial.dll
)
endlocal
