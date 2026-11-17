Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-JyutpingVersion {
    param([string]$Version = '')
    if ([string]::IsNullOrWhiteSpace($Version)) {
        $Version = (Get-Content -LiteralPath (Join-Path $PSScriptRoot '..\version.txt') -Raw).Trim()
    }
    if ($Version -notmatch '^\d+\.\d+\.\d+(\.\d+)?$') {
        throw 'AppVersion must contain three or four numeric components.'
    }
    $parsed = [version]$Version
    foreach ($component in @($parsed.Major, $parsed.Minor, $parsed.Build, $parsed.Revision)) {
        if ($component -gt 65535) { throw 'AppVersion components must not exceed 65535.' }
    }
    return $Version
}

function Convert-ToFileVersion {
    param([Parameter(Mandatory)][string]$Version)
    $Version = Get-JyutpingVersion $Version
    if ($Version.Split('.').Count -eq 3) { return "$Version.0" }
    return $Version
}

function Assert-ProductVersion {
    param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)][string]$Version)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Required payload is missing: $Path" }
    $info = [System.Diagnostics.FileVersionInfo]::GetVersionInfo((Resolve-Path -LiteralPath $Path).Path)
    # Inno Setup reserves fixed-size string resources and pads them with spaces.
    $productName = ([string]$info.ProductName).Trim()
    $productVersion = ([string]$info.ProductVersion).Trim()
    if ($productName -notin @('Jyutping', '粵拼', '粤拼')) { throw "Unexpected product identity: $Path" }
    if ($productVersion -ne $Version) { throw "Payload version '$productVersion' does not match '$Version': $Path" }
    $actualFileVersion = '{0}.{1}.{2}.{3}' -f $info.FileMajorPart, $info.FileMinorPart, $info.FileBuildPart, $info.FilePrivatePart
    if ($actualFileVersion -ne (Convert-ToFileVersion $Version)) { throw "Numeric file version does not match '$Version': $Path" }
}

function Assert-BinaryArchitecture {
    param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)][ValidateSet('x64', 'x86', 'ARM64', 'ARM64X')][string]$Architecture)
    $stream = [System.IO.File]::OpenRead((Resolve-Path -LiteralPath $Path).Path)
    $reader = [System.IO.BinaryReader]::new($stream)
    try {
        if ($reader.ReadUInt16() -ne 0x5a4d) { throw "Not a PE file: $Path" }
        $stream.Position = 0x3c
        $stream.Position = $reader.ReadInt32()
        if ($reader.ReadUInt32() -ne 0x00004550) { throw "Invalid PE header: $Path" }
        $machine = $reader.ReadUInt16()
        # ARM64X root DLLs identify as ARM64 in the PE file header.
        $expected = @{ x64 = 0x8664; x86 = 0x014c; ARM64 = 0xaa64; ARM64X = 0xaa64 }[$Architecture]
        if ($machine -ne $expected) { throw "Wrong architecture for $Architecture payload: $Path (0x$($machine.ToString('x4')))" }
    } finally { $reader.Dispose() }
}

function Get-UpdatePublicKey {
    $configuration = Get-Content -LiteralPath (Join-Path $PSScriptRoot '..\Shared\UpdateConfiguration.h') -Raw
    $match = [regex]::Match($configuration, 'PublicKey\[\] = "([A-Za-z0-9+/=]+)";')
    if (-not $match.Success -or [Convert]::FromBase64String($match.Groups[1].Value).Length -ne 32) { throw 'Invalid embedded update public key.' }
    return $match.Groups[1].Value
}

function Invoke-WinSparkleTool {
    param([Parameter(Mandatory)][string[]]$Arguments)
    $tool = Join-Path $PSScriptRoot '..\third_party\WinSparkle\0.9.4\bin\winsparkle-tool.exe'
    $output = & $tool @Arguments
    if ($LASTEXITCODE -ne 0) { throw 'WinSparkle signing or signature verification failed.' }
    return ($output -join "`n").Trim()
}

function Assert-UpdateSignature {
    param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)][string]$Signature)
    if ([Convert]::FromBase64String($Signature).Length -ne 64) { throw "Invalid Ed25519 signature for $Path" }
    Invoke-WinSparkleTool -Arguments @('verify', '--public-key', (Get-UpdatePublicKey), '--signature', $Signature, $Path) | Out-Null
}

Export-ModuleMember -Function Get-JyutpingVersion, Convert-ToFileVersion, Assert-ProductVersion, Assert-BinaryArchitecture, Get-UpdatePublicKey, Invoke-WinSparkleTool, Assert-UpdateSignature
