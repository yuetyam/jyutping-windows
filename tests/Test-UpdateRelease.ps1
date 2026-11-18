[CmdletBinding()]
param(
    [string]$InstallerDirectory = (Join-Path $PSScriptRoot '..\packaging\output'),
    [switch]$CreateFixtures
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Import-Module (Join-Path $repoRoot 'packaging\ReleaseTools.psm1') -Force
$version = Get-JyutpingVersion
$temporaryRoot = [System.IO.Path]::GetFullPath((Join-Path ([System.IO.Path]::GetTempPath()) ('JyutpingUpdateTests-' + [guid]::NewGuid())))
$tool = Join-Path $repoRoot 'third_party\WinSparkle\0.9.4\bin\winsparkle-tool.exe'

function Assert-True([bool]$Value, [string]$Message) {
    if (-not $Value) { throw $Message }
}

function Assert-Rejected([scriptblock]$Action, [string]$Message) {
    $rejected = $false
    try { & $Action | Out-Null } catch { $rejected = $true }
    Assert-True $rejected $Message
}

try {
    New-Item -ItemType Directory -Path $temporaryRoot -Force | Out-Null
    foreach ($hashLine in Get-Content -LiteralPath (Join-Path $repoRoot 'third_party\WinSparkle\0.9.4\SHA256SUMS.txt')) {
        $expectedHash, $relativePath = $hashLine -split '  ', 2
        $actualHash = (Get-FileHash -LiteralPath (Join-Path $repoRoot "third_party\WinSparkle\0.9.4\$relativePath") -Algorithm SHA256).Hash
        Assert-True ($actualHash -eq $expectedHash) "Pinned SDK hash mismatch: $relativePath"
    }
    if ($CreateFixtures) {
        $InstallerDirectory = Join-Path $temporaryRoot 'fixtures'
        New-Item -ItemType Directory -Path $InstallerDirectory | Out-Null
        $compiler = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
        foreach ($architecture in @('x64', 'arm64')) {
            $fixtureSource = Join-Path $InstallerDirectory "$architecture.cs"
            $fixtureCode = @"
using System.Reflection;
[assembly: AssemblyTitle("Jyutping Installer ($architecture)  ")]
[assembly: AssemblyProduct("Jyutping  ")]
[assembly: AssemblyFileVersion("$(Convert-ToFileVersion $version)")]
[assembly: AssemblyInformationalVersion("$version  ")]
class Fixture { static void Main() {} }
"@
            [System.IO.File]::WriteAllText($fixtureSource, $fixtureCode)
            & $compiler /nologo /target:winexe "/out:$InstallerDirectory\jyutping-v$version-$architecture.exe" $fixtureSource
            if ($LASTEXITCODE -ne 0) { throw 'Could not compile version-resource fixture.' }
        }
    }
    $fixturePackaging = Join-Path $temporaryRoot 'packaging'
    $fixtureSdk = Join-Path $temporaryRoot 'third_party\WinSparkle\0.9.4\bin'
    $fixtureShared = Join-Path $temporaryRoot 'Shared'
    New-Item -ItemType Directory -Path $fixturePackaging, $fixtureSdk, $fixtureShared -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $repoRoot 'packaging\ReleaseTools.psm1'), (Join-Path $repoRoot 'packaging\New-WindowsAppcast.ps1') -Destination $fixturePackaging
    Copy-Item -LiteralPath $tool -Destination $fixtureSdk
    Copy-Item -LiteralPath (Join-Path $repoRoot 'version.txt') -Destination $temporaryRoot
    $keyFile = Join-Path $temporaryRoot 'test-private-key'
    $keyOutput = & $tool generate-key --file $keyFile
    if ($LASTEXITCODE -ne 0) { throw 'Test key generation failed.' }
    $testPublicKey = [regex]::Match(($keyOutput -join "`n"), 'Public key: ([A-Za-z0-9+/=]+)').Groups[1].Value
    Assert-True ([Convert]::FromBase64String($testPublicKey).Length -eq 32) 'Invalid test public key.'
    $configuration = (Get-Content -LiteralPath (Join-Path $repoRoot 'Shared\UpdateConfiguration.h') -Raw).Replace((Get-UpdatePublicKey), $testPublicKey)
    [System.IO.File]::WriteAllText((Join-Path $fixtureShared 'UpdateConfiguration.h'), $configuration)

    $signatures = @{}
    foreach ($architecture in @('x64', 'arm64')) {
        $installer = Join-Path $InstallerDirectory "jyutping-v$version-$architecture.exe"
        Assert-ProductVersion -Path $installer -Version $version
        $signatures[$architecture] = (& $tool sign --private-key-file $keyFile $installer) -join ''
        if ($LASTEXITCODE -ne 0) { throw 'Test signing failed.' }
    }
    # A signature from an unrelated key must never pass production verification.
    Assert-Rejected { Assert-UpdateSignature -Path (Join-Path $InstallerDirectory "jyutping-v$version-x64.exe") -Signature $signatures.x64 } 'Production accepted a test signing key.'

    $appcastScript = Join-Path $fixturePackaging 'New-WindowsAppcast.ps1'
    $outputPath = Join-Path $temporaryRoot 'windows.xml'
    $arguments = @{
        InstallerDirectory = $InstallerDirectory
        OutputPath = $outputPath
        X64Signature = $signatures.x64
        Arm64Signature = $signatures.arm64
        ReleaseNotes = 'Release notes: <>& and 粵拼'
        ReleaseDate = [datetimeoffset]'2026-11-17T12:34:56+08:00'
    }
    & $appcastScript @arguments
    $feed = [xml](Get-Content -LiteralPath $outputPath -Raw)
    $namespaces = [System.Xml.XmlNamespaceManager]::new($feed.NameTable)
    $namespaces.AddNamespace('sparkle', 'http://www.andymatuschak.org/xml-namespaces/sparkle')
    $items = $feed.SelectNodes('/rss/channel/item')
    Assert-True ($items.Count -eq 2) 'Expected two architecture-specific items.'
    foreach ($architecture in @('x64', 'arm64')) {
        $item = $feed.SelectSingleNode("/rss/channel/item[enclosure/@sparkle:os='windows-$architecture']", $namespaces)
        Assert-True ($null -ne $item) "Missing $architecture item."
        Assert-True ($item.SelectSingleNode('sparkle:version', $namespaces).InnerText -eq $version) 'Wrong feed version.'
        $expectedMinimum = @{ x64 = '10.0.17763'; arm64 = '10.0.22000' }[$architecture]
        Assert-True ($item.SelectSingleNode('sparkle:minimumSystemVersion', $namespaces).InnerText -eq $expectedMinimum) "Wrong minimum Windows version for $architecture."
        Assert-True ($item.pubDate -eq 'Tue, 17 Nov 2026 12:34:56 +0800') 'Invalid RSS publication date.'
        Assert-True ($item.description -eq $arguments.ReleaseNotes) 'Release notes were not escaped correctly.'
        Assert-True (-not $item.enclosure.HasAttribute('installerArguments', $namespaces.LookupNamespace('sparkle'))) 'Installer arguments should be omitted.'
        $installer = Join-Path $InstallerDirectory "jyutping-v$version-$architecture.exe"
        Assert-True ([long]$item.enclosure.length -eq (Get-Item -LiteralPath $installer).Length) 'Incorrect enclosure byte length.'
    }
    & $appcastScript @arguments -PreviousAppcast $outputPath
    $feed = [xml](Get-Content -LiteralPath $outputPath -Raw)
    Assert-True ($feed.SelectNodes('/rss/channel/item').Count -eq 2) 'Regeneration duplicated release items.'
    $originalOutput = Get-Content -LiteralPath $outputPath -Raw
    Assert-Rejected { & $appcastScript @arguments -AppVersion '99.0.0' } 'Version mismatch was accepted.'
    $badArguments = $arguments.Clone()
    $badArguments.X64Signature = [Convert]::ToBase64String([byte[]]::new(64))
    Assert-Rejected { & $appcastScript @badArguments } 'Invalid signature was accepted.'

    $tamperedDirectory = Join-Path $temporaryRoot 'tampered'
    New-Item -ItemType Directory -Path $tamperedDirectory | Out-Null
    foreach ($architecture in @('x64', 'arm64')) {
        Copy-Item -LiteralPath (Join-Path $InstallerDirectory "jyutping-v$version-$architecture.exe") -Destination $tamperedDirectory
    }
    $tamperedInstaller = Join-Path $tamperedDirectory "jyutping-v$version-x64.exe"
    $stream = [System.IO.File]::Open($tamperedInstaller, [System.IO.FileMode]::Open, [System.IO.FileAccess]::ReadWrite)
    try {
        $stream.Position = $stream.Length - 1
        $lastByte = $stream.ReadByte()
        $stream.Position = $stream.Length - 1
        $stream.WriteByte($lastByte -bxor 1)
    } finally { $stream.Dispose() }
    $badArguments = $arguments.Clone()
    $badArguments.InstallerDirectory = $tamperedDirectory
    Assert-Rejected { & $appcastScript @badArguments } 'Tampered installer was accepted.'
    Copy-Item -LiteralPath (Join-Path $InstallerDirectory "jyutping-v$version-arm64.exe") -Destination $tamperedInstaller -Force
    Copy-Item -LiteralPath (Join-Path $InstallerDirectory "jyutping-v$version-x64.exe") -Destination (Join-Path $tamperedDirectory "jyutping-v$version-arm64.exe") -Force
    $badArguments.X64Signature = $signatures.arm64
    $badArguments.Arm64Signature = $signatures.x64
    Assert-Rejected { & $appcastScript @badArguments } 'Swapped architecture payloads with valid signatures were accepted.'
    Assert-True ((Get-Content -LiteralPath $outputPath -Raw) -eq $originalOutput) 'A failed validation changed the output feed.'
    Write-Host 'Update release tests passed: signatures, tampering, versions, architecture metadata, RSS dates, escaping, lengths, and regeneration.'
} finally {
    $temporaryParent = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if ($temporaryRoot.StartsWith($temporaryParent, [System.StringComparison]::OrdinalIgnoreCase) -and (Split-Path -Leaf $temporaryRoot).StartsWith('JyutpingUpdateTests-')) {
        Remove-Item -LiteralPath $temporaryRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}

# Expected native-tool rejections leave LASTEXITCODE nonzero; report success to CI only after all assertions and cleanup complete.
exit 0
