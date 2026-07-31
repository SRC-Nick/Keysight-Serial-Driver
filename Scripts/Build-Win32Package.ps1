[CmdletBinding()]
param(
    [ValidateSet("v142", "v143")]
    [string]$Toolset = "v142",
    [string]$OutputRoot = "artifacts\windows7-full-build",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$artifactRoot = [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot $OutputRoot))
$repositoryPrefix = $repositoryRoot.TrimEnd("\") + "\"
if (-not $artifactRoot.StartsWith($repositoryPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "OutputRoot must resolve beneath the repository root."
}

function Find-MSBuild {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere) {
        $installation = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
        if ($installation) {
            $candidate = Join-Path $installation "MSBuild\Current\Bin\MSBuild.exe"
            if (Test-Path -LiteralPath $candidate) { return $candidate }
        }
    }
    $fallbacks = @(
        "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe",
        "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
    )
    foreach ($candidate in $fallbacks) {
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }
    throw "MSBuild.exe was not found."
}

function Find-DumpBin {
    $command = Get-Command "dumpbin.exe" -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    $vcToolsRoot = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC"
    if (Test-Path -LiteralPath $vcToolsRoot) {
        $candidate = Get-ChildItem -LiteralPath $vcToolsRoot -Directory |
            Sort-Object Name -Descending |
            ForEach-Object { Join-Path $_.FullName "bin\Hostx64\x64\dumpbin.exe" } |
            Where-Object { Test-Path -LiteralPath $_ } |
            Select-Object -First 1
        if ($candidate) { return $candidate }
    }
    throw "dumpbin.exe was not found; PE compatibility cannot be recorded."
}

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(ValueFromRemainingArguments = $true)][string[]]$Arguments
    )
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$FilePath failed with exit code $LASTEXITCODE."
    }
}

$msbuild = Find-MSBuild
$dumpbin = Find-DumpBin
Push-Location $repositoryRoot
try {
    if (-not $SkipBuild) {
        Invoke-Checked $msbuild "SRCSerial.sln" "/m" "/t:Rebuild" `
            "/p:Configuration=Release" "/p:Platform=Win32" `
            "/p:PlatformToolset=$Toolset" "/v:minimal"
    }

    $tools = Join-Path $repositoryRoot "Test\Release\SRCSerialTools.exe"
    $dll = Join-Path $repositoryRoot "Release\SRCSerial.dll"
    if (-not (Test-Path -LiteralPath $tools) -or -not (Test-Path -LiteralPath $dll)) {
        throw "Release binaries are missing. Build the Win32 Release solution first."
    }

    Invoke-Checked $tools "self-test"
    Invoke-Checked $tools "action-smoke" $dll

    $umdFiles = @(Get-ChildItem -LiteralPath (Join-Path $repositoryRoot "actions") -Filter "SRCSerial_*.umd")
    if ($umdFiles.Count -ne 23) {
        throw "Expected 23 UMD files but found $($umdFiles.Count)."
    }
    foreach ($umd in $umdFiles) {
        Invoke-Checked $tools "inspect" $umd.FullName
    }

    if (Test-Path -LiteralPath $artifactRoot) {
        $resolvedArtifact = (Resolve-Path -LiteralPath $artifactRoot).Path
        if (-not $resolvedArtifact.StartsWith($repositoryPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to remove an output path outside the repository."
        }
        Remove-Item -LiteralPath $resolvedArtifact -Recurse -Force
    }

    $deployRoot = Join-Path $artifactRoot "deploy"
    $actionsRoot = Join-Path $deployRoot "actions"
    $developmentRoot = Join-Path $artifactRoot "development"
    $symbolsRoot = Join-Path $artifactRoot "symbols"
    $toolsRoot = Join-Path $artifactRoot "tools"
    New-Item -ItemType Directory -Path $actionsRoot, $developmentRoot, $symbolsRoot, $toolsRoot -Force | Out-Null

    Copy-Item -LiteralPath $dll -Destination $deployRoot
    foreach ($umd in $umdFiles) {
        Copy-Item -LiteralPath $umd.FullName -Destination $actionsRoot
    }
    Copy-Item -LiteralPath (Join-Path $repositoryRoot "README.md") -Destination $deployRoot
    Copy-Item -LiteralPath (Join-Path $repositoryRoot "CHANGELOG.md") -Destination $deployRoot
    Copy-Item -LiteralPath (Join-Path $repositoryRoot "actions\README.md") -Destination (Join-Path $actionsRoot "README.md")
    Copy-Item -LiteralPath (Join-Path $repositoryRoot "Docs\HARDWARE_SETUP.md") -Destination $deployRoot
    Copy-Item -LiteralPath (Join-Path $repositoryRoot "Docs\TESTING.md") -Destination $deployRoot

    Copy-Item -LiteralPath (Join-Path $repositoryRoot "Release\SRCSerial.lib") -Destination $developmentRoot
    Copy-Item -LiteralPath (Join-Path $repositoryRoot "Release\SRCSerial.exp") -Destination $developmentRoot
    Copy-Item -LiteralPath (Join-Path $repositoryRoot "Release\SRCSerial.pdb") -Destination $symbolsRoot
    Copy-Item -LiteralPath $tools -Destination $toolsRoot
    Copy-Item -LiteralPath (Join-Path $repositoryRoot "Test\Release\SRCSerialTools.pdb") -Destination $symbolsRoot
    Copy-Item -LiteralPath (Join-Path $repositoryRoot "Test\README.md") -Destination (Join-Path $toolsRoot "README.md")

    $sourceCommit = (& git rev-parse HEAD).Trim()
    $sourceBranch = (& git branch --show-current).Trim()
    $buildTime = [DateTime]::UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ")
    $msbuildVersion = (& $msbuild -version -nologo | Select-Object -Last 1).Trim()
    $metadata = @(
        "SRCSerial Win32 build metadata"
        "SourceCommit=$sourceCommit"
        "SourceBranch=$sourceBranch"
        "BuildTimeUtc=$buildTime"
        "Configuration=Release"
        "Platform=Win32"
        "PlatformToolset=$Toolset"
        "MSBuildVersion=$msbuildVersion"
        "RuntimeLibrary=Static"
        "MinimumConfiguredWindowsVersion=0x0601"
        "HardwareQualified=No"
        "TestExecWindows7Qualified=No"
        ""
        "This package passed the repository's local self-tests, DLL/UTA smoke test,"
        "UMD restore inspection, and export checks. It still requires acceptance on"
        "the target Windows 7/TestExec station and physical RS-232/RS-485 hardware."
    )
    Set-Content -LiteralPath (Join-Path $artifactRoot "BUILD-METADATA.txt") -Value $metadata -Encoding ASCII
    Copy-Item -LiteralPath (Join-Path $repositoryRoot "Docs\FULL_BUILD_BRANCH.md") `
        -Destination (Join-Path $artifactRoot "README.md")

    $peReport = & $dumpbin /headers /exports /imports $dll 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "dumpbin failed with exit code $LASTEXITCODE."
    }
    $peText = $peReport -join [Environment]::NewLine
    if ($peText -notmatch "14C machine \(x86\)" -or
        $peText -notmatch "23 number of functions" -or
        $peText -notmatch "23 number of names") {
        throw "The DLL does not have the expected x86/23-export PE structure."
    }
    Set-Content -LiteralPath (Join-Path $artifactRoot "PE-REPORT.txt") `
        -Value $peReport -Encoding ASCII

    $checksumLines = Get-ChildItem -LiteralPath $artifactRoot -File -Recurse |
        Where-Object { $_.Name -ne "SHA256SUMS.txt" } |
        Sort-Object FullName |
        ForEach-Object {
            $relative = $_.FullName.Substring($artifactRoot.Length + 1).Replace("\", "/")
            $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
            "$hash *$relative"
        }
    Set-Content -LiteralPath (Join-Path $artifactRoot "SHA256SUMS.txt") `
        -Value $checksumLines -Encoding ASCII

    $zipPath = "$artifactRoot.zip"
    if (Test-Path -LiteralPath $zipPath) { Remove-Item -LiteralPath $zipPath -Force }
    Compress-Archive -Path (Join-Path $artifactRoot "*") -DestinationPath $zipPath -CompressionLevel Optimal
    $zipHash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
    Set-Content -LiteralPath "$zipPath.sha256" `
        -Value "$zipHash *$([System.IO.Path]::GetFileName($zipPath))" -Encoding ASCII

    Write-Host "Package created at $artifactRoot"
    Write-Host "Archive created at $zipPath"
}
finally {
    Pop-Location
}
