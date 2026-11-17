[CmdletBinding(DefaultParameterSetName = 'Signatures')]
param(
    [string]$AppVersion = '',
    [string]$InstallerDirectory = (Join-Path $PSScriptRoot 'output'),
    [string]$OutputPath = (Join-Path $PSScriptRoot 'output\windows.xml'),
    [Parameter(Mandatory, ParameterSetName = 'Signatures')][string]$X64Signature,
    [Parameter(Mandatory, ParameterSetName = 'Signatures')][string]$Arm64Signature,
    [Parameter(Mandatory, ParameterSetName = 'PrivateKey')][string]$PrivateKeyFile,
    [Parameter(Mandatory)][string]$ReleaseNotes,
    [datetimeoffset]$ReleaseDate = [datetimeoffset]::Now,
    [string]$PreviousAppcast = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Import-Module (Join-Path $PSScriptRoot 'ReleaseTools.psm1') -Force
$AppVersion = Get-JyutpingVersion $AppVersion
$installers = @{
    x64 = Join-Path $InstallerDirectory "jyutping-v$AppVersion-x64.exe"
    arm64 = Join-Path $InstallerDirectory "jyutping-v$AppVersion-arm64.exe"
}
$signatures = @{ x64 = $X64Signature; arm64 = $Arm64Signature }
foreach ($architecture in @('x64', 'arm64')) {
    $installer = (Resolve-Path -LiteralPath $installers[$architecture]).Path
    Assert-ProductVersion -Path $installer -Version $AppVersion
    $info = [System.Diagnostics.FileVersionInfo]::GetVersionInfo($installer)
    if (([string]$info.FileDescription).Trim() -ne "Jyutping Installer ($architecture)") { throw "Wrong installer architecture metadata: $installer" }
    if ($PSCmdlet.ParameterSetName -eq 'PrivateKey') {
        $signatures[$architecture] = Invoke-WinSparkleTool -Arguments @('sign', '--private-key-file', (Resolve-Path -LiteralPath $PrivateKeyFile).Path, $installer)
    }
    Assert-UpdateSignature -Path $installer -Signature $signatures[$architecture]
}

$sparkle = 'http://www.andymatuschak.org/xml-namespaces/sparkle'
$feed = [System.Xml.XmlDocument]::new()
$feed.XmlResolver = $null
if ($PreviousAppcast) {
    $feed.Load((Resolve-Path -LiteralPath $PreviousAppcast).Path)
    if ($feed.DocumentType -or $feed.DocumentElement.Name -ne 'rss' -or -not $feed.SelectSingleNode('/rss/channel')) { throw 'PreviousAppcast must be an RSS feed without a document type.' }
} else {
    $feed.LoadXml('<?xml version="1.0" encoding="utf-8"?><rss version="2.0" xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle"><channel><title>Jyutping for Windows</title><link>https://jyutping.app/windows</link><description>Jyutping Windows updates</description></channel></rss>')
}
$channel = $feed.SelectSingleNode('/rss/channel')
$publicationDate = $ReleaseDate.ToString('ddd, dd MMM yyyy HH:mm:ss zzz', [System.Globalization.CultureInfo]::InvariantCulture) -replace '([+-]\d{2}):(\d{2})$', '$1$2'
foreach ($oldItem in @($channel.SelectNodes('item'))) {
    $oldVersion = $oldItem.GetElementsByTagName('version', $sparkle)
    if ($oldVersion.Count -eq 1 -and $oldVersion[0].InnerText -eq $AppVersion) { [void]$channel.RemoveChild($oldItem) }
}
foreach ($architecture in @('arm64', 'x64')) {
    $item = $feed.CreateElement('item')
    foreach ($field in @(
        @('title', "Jyutping $AppVersion ($architecture)", ''),
        @('pubDate', $publicationDate, ''),
        @('version', $AppVersion, $sparkle),
        @('shortVersionString', $AppVersion, $sparkle),
        @('minimumSystemVersion', $(if ($architecture -eq 'arm64') { '10.0.22000' } else { '10.0' }), $sparkle),
        @('description', $ReleaseNotes, '')
    )) {
        $element = if ($field[2]) { $feed.CreateElement('sparkle', $field[0], $sparkle) } else { $feed.CreateElement($field[0]) }
        $element.InnerText = $field[1]
        [void]$item.AppendChild($element)
    }
    $enclosure = $feed.CreateElement('enclosure')
    $enclosure.SetAttribute('url', "https://dl.jyutping.app/jyutping-windows/$AppVersion/jyutping-v$AppVersion-$architecture.exe")
    $enclosure.SetAttribute('length', ([System.IO.FileInfo]$installers[$architecture]).Length.ToString([System.Globalization.CultureInfo]::InvariantCulture))
    $enclosure.SetAttribute('type', 'application/octet-stream')
    foreach ($attribute in @(@('os', "windows-$architecture"), @('edSignature', $signatures[$architecture]))) {
        $xmlAttribute = $feed.CreateAttribute('sparkle', $attribute[0], $sparkle)
        $xmlAttribute.Value = $attribute[1]
        [void]$enclosure.Attributes.Append($xmlAttribute)
    }
    [void]$item.AppendChild($enclosure)
    $firstItem = $channel.SelectSingleNode('item')
    if ($firstItem) { [void]$channel.InsertBefore($item, $firstItem) } else { [void]$channel.AppendChild($item) }
}

$outputFullPath = [System.IO.Path]::GetFullPath($OutputPath)
if ($outputFullPath -in @($installers.Values | ForEach-Object { [System.IO.Path]::GetFullPath($_) })) { throw 'OutputPath must not overwrite an installer.' }
if ($PrivateKeyFile -and $outputFullPath -eq (Resolve-Path -LiteralPath $PrivateKeyFile).Path) { throw 'OutputPath must not overwrite the signing key.' }
New-Item -ItemType Directory -Path (Split-Path -Parent $outputFullPath) -Force | Out-Null
$settings = [System.Xml.XmlWriterSettings]::new()
$settings.Encoding = [System.Text.UTF8Encoding]::new($false)
$settings.Indent = $true
$settings.IndentChars = '    '
$settings.NewLineChars = "`n"
$settings.NewLineHandling = [System.Xml.NewLineHandling]::Entitize
$temporaryOutput = $outputFullPath + '.' + [guid]::NewGuid() + '.tmp'
try {
    $writer = [System.Xml.XmlWriter]::Create($temporaryOutput, $settings)
    try { $feed.Save($writer) } finally { $writer.Dispose() }
    [System.IO.File]::AppendAllText($temporaryOutput, "`n", $settings.Encoding)
    [System.IO.File]::Move($temporaryOutput, $outputFullPath, $true)
} finally {
    if (Test-Path -LiteralPath $temporaryOutput) { Remove-Item -LiteralPath $temporaryOutput -Force }
}
Write-Host "Created verified appcast: $outputFullPath"
