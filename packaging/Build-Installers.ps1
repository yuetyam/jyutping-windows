[CmdletBinding()]
param(
    [string]$AppVersion = "",
    [string]$Configuration = "Release",
    [string]$MSBuildPath = "msbuild",
    [string]$InnoSetupCompiler = "",
    [string]$SignToolCommand = "",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
Import-Module (Join-Path $PSScriptRoot 'ReleaseTools.psm1') -Force
$AppVersion = Get-JyutpingVersion $AppVersion

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$StageRoot = Join-Path $PSScriptRoot "stage"
$OutputDirectory = Join-Path $PSScriptRoot "output"

function Invoke-External {
    param(
        [Parameter(Mandatory = $true)]
        [string]$FilePath,

        [Parameter(Mandatory = $true)]
        [string[]]$Arguments
    )

    Write-Host "Running: $FilePath $($Arguments -join ' ')"
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$FilePath failed with exit code $LASTEXITCODE."
    }
}

function Resolve-InnoSetupCompiler {
    if (-not [string]::IsNullOrWhiteSpace($InnoSetupCompiler)) {
        return $InnoSetupCompiler
    }

    if (-not [string]::IsNullOrWhiteSpace($env:INNOSETUP_COMPILER)) {
        return $env:INNOSETUP_COMPILER
    }

    $candidatePaths = @()
    foreach ($programFilesRoot in @(${env:ProgramFiles(x86)}, $env:ProgramFiles)) {
        if ([string]::IsNullOrWhiteSpace($programFilesRoot)) {
            continue
        }

        $candidatePaths += Join-Path $programFilesRoot "Inno Setup 7\ISCC.exe"
        $candidatePaths += Join-Path $programFilesRoot "Inno Setup 6\ISCC.exe"
    }

    foreach ($candidatePath in $candidatePaths) {
        if (Test-Path -LiteralPath $candidatePath -PathType Leaf) {
            return $candidatePath
        }
    }

    $command = Get-Command "ISCC.exe" -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }

    throw "Could not find ISCC.exe. Install Inno Setup or pass -InnoSetupCompiler."
}

function Assert-FileExists {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required payload is missing: $Path"
    }
}

function Copy-RequiredFile {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Source,

        [Parameter(Mandatory = $true)]
        [string]$Destination
    )

    Assert-FileExists -Path $Source
    $destinationDirectory = Split-Path -Parent $Destination
    New-Item -ItemType Directory -Path $destinationDirectory -Force | Out-Null
    Copy-Item -LiteralPath $Source -Destination $Destination -Force
}

function Invoke-SignTool {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$Paths
    )

    if ([string]::IsNullOrWhiteSpace($SignToolCommand)) {
        return
    }

    foreach ($path in $Paths) {
        Assert-FileExists -Path $path
        $escapedPath = $path.Replace('"', '\"')
        Invoke-External -FilePath "cmd.exe" -Arguments @("/d", "/s", "/c", "$SignToolCommand `"$escapedPath`"")
    }
}

function Build-Platform {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Platform
    )

    Invoke-External -FilePath $MSBuildPath -Arguments @(
        (Join-Path $RepoRoot "Jyutping.sln"),
        "/m",
        "/v:minimal",
        "/nologo",
        "/p:Configuration=$Configuration",
        "/p:AppVersion=$AppVersion",
        "/p:Platform=$Platform"
    )
}

if (-not $SkipBuild) {
    foreach ($platform in @("x64", "Win32", "ARM64EC")) {
        Build-Platform -Platform $platform
    }
}

$payloads = @{
    X64Dll = Join-Path $RepoRoot "x64\$Configuration\Jyutping.dll"
    X86Dll = Join-Path $RepoRoot "Win32\$Configuration\Jyutping.dll"
    Arm64Dll = Join-Path $RepoRoot "ARM64EC\$Configuration\Jyutping.dll"
    Database = Join-Path $RepoRoot "Jyutping\Resources\ime.sqlite3"
    X64Updater = Join-Path $RepoRoot "JyutpingUpdater\bin\x64\$Configuration\JyutpingUpdater.exe"
    Arm64Updater = Join-Path $RepoRoot "JyutpingUpdater\bin\ARM64\$Configuration\JyutpingUpdater.exe"
}

$versionInfoVersion = Convert-ToFileVersion -Version $AppVersion

foreach ($payloadPath in $payloads.Values) {
    Assert-FileExists -Path $payloadPath
}
foreach ($payloadName in @('X64Dll', 'X86Dll', 'Arm64Dll', 'X64Updater', 'Arm64Updater')) {
    Assert-ProductVersion -Path $payloads[$payloadName] -Version $AppVersion
}
Assert-BinaryArchitecture -Path $payloads.X64Dll -Architecture x64
Assert-BinaryArchitecture -Path $payloads.X86Dll -Architecture x86
Assert-BinaryArchitecture -Path $payloads.Arm64Dll -Architecture ARM64X
Assert-BinaryArchitecture -Path $payloads.X64Updater -Architecture x64
Assert-BinaryArchitecture -Path $payloads.Arm64Updater -Architecture ARM64

if (Test-Path -LiteralPath $StageRoot) {
    Get-ChildItem -LiteralPath $StageRoot -Force |
        Where-Object { $_.Name -ne ".gitkeep" } |
        Remove-Item -Recurse -Force
} else {
    New-Item -ItemType Directory -Path $StageRoot -Force | Out-Null
}

New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
foreach ($outputPattern in @("*.exe", "SHA256SUMS.txt", "windows.xml")) {
    Get-ChildItem -LiteralPath $OutputDirectory -Filter $outputPattern -File -Force |
        Remove-Item -Force
}

$x64StageRoot = Join-Path $StageRoot "x64"
$arm64StageRoot = Join-Path $StageRoot "ARM64"

Copy-RequiredFile -Source $payloads.X64Dll -Destination (Join-Path $x64StageRoot "x64\Jyutping.dll")
Copy-RequiredFile -Source $payloads.X86Dll -Destination (Join-Path $x64StageRoot "x86\Jyutping.dll")
Copy-RequiredFile -Source $payloads.Database -Destination (Join-Path $x64StageRoot "ime.sqlite3")

Copy-RequiredFile -Source $payloads.Arm64Dll -Destination (Join-Path $arm64StageRoot "Jyutping.dll")
Copy-RequiredFile -Source $payloads.Database -Destination (Join-Path $arm64StageRoot "ime.sqlite3")

$sdkRoot = Join-Path $RepoRoot 'third_party\WinSparkle\0.9.4'
foreach ($package in @(
    @{ Stage = $x64StageRoot; Platform = 'x64'; Updater = $payloads.X64Updater },
    @{ Stage = $arm64StageRoot; Platform = 'ARM64'; Updater = $payloads.Arm64Updater }
)) {
    Copy-RequiredFile -Source $package.Updater -Destination (Join-Path $package.Stage 'Updater\JyutpingUpdater.exe')
    $sparkleDll = Join-Path $sdkRoot "$($package.Platform)\Release\WinSparkle.dll"
    Assert-BinaryArchitecture -Path $sparkleDll -Architecture $package.Platform
    Copy-RequiredFile -Source $sparkleDll -Destination (Join-Path $package.Stage 'Updater\WinSparkle.dll')
    foreach ($notice in @('COPYING', 'COPYING.expat', 'AUTHORS')) {
        Copy-RequiredFile -Source (Join-Path $sdkRoot $notice) -Destination (Join-Path $package.Stage "Updater\$notice")
    }
}

$stagedBinaries = @(
    (Join-Path $x64StageRoot "x64\Jyutping.dll"),
    (Join-Path $x64StageRoot "x86\Jyutping.dll"),
    (Join-Path $arm64StageRoot "Jyutping.dll"),
    (Join-Path $x64StageRoot "Updater\JyutpingUpdater.exe"),
    (Join-Path $arm64StageRoot "Updater\JyutpingUpdater.exe")
)
Invoke-SignTool -Paths $stagedBinaries

$iscc = Resolve-InnoSetupCompiler

Invoke-External -FilePath $iscc -Arguments @(
    "/DAppVersion=$AppVersion",
    "/DVersionInfoVersion=$versionInfoVersion",
    "/DStageRoot=$x64StageRoot",
    "/DOutputDir=$OutputDirectory",
    (Join-Path $PSScriptRoot "Jyutping-x64.iss")
)

Invoke-External -FilePath $iscc -Arguments @(
    "/DAppVersion=$AppVersion",
    "/DVersionInfoVersion=$versionInfoVersion",
    "/DStageRoot=$arm64StageRoot",
    "/DOutputDir=$OutputDirectory",
    (Join-Path $PSScriptRoot "Jyutping-ARM64.iss")
)

$installers = @(
    (Join-Path $OutputDirectory "jyutping-v$AppVersion-x64.exe"),
    (Join-Path $OutputDirectory "jyutping-v$AppVersion-arm64.exe")
)

foreach ($installer in $installers) {
    Assert-FileExists -Path $installer
}

Invoke-SignTool -Paths $installers

$checksumPath = Join-Path $OutputDirectory "SHA256SUMS.txt"
$checksumLines = foreach ($installer in $installers) {
    $hash = Get-FileHash -LiteralPath $installer -Algorithm SHA256
    "$($hash.Hash.ToLowerInvariant())  $(Split-Path -Leaf $installer)"
}
Set-Content -LiteralPath $checksumPath -Value $checksumLines -Encoding utf8

Write-Host "Created installers:"
foreach ($installer in $installers) {
    Write-Host "  $installer"
}
Write-Host "Created checksums: $checksumPath"
