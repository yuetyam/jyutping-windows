[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true
$architecture = [Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString().ToLowerInvariant()
if ($env:VSCMD_ARG_TGT_ARCH -ne $architecture) {
    throw "Run in a Visual Studio Developer PowerShell targeting $architecture"
}
$repository = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$sources = Join-Path $repository 'Jyutping'
$output = Join-Path $repository 'backend/target/engine-parity'
$dictionary = (Resolve-Path (Join-Path $sources 'Resources/ime.sqlite3')).Path
New-Item -ItemType Directory -Force $output | Out-Null
$previousBaseline = $env:JYUTPING_ENGINE_BASELINE
$previousDictionary = $env:JYUTPING_ENGINE_DICTIONARY

Push-Location $output
try {
    cl.exe /nologo /W4 /MT /std:c++latest /EHsc /utf-8 /DUNICODE /D_UNICODE /FeWindowsEngineBaseline.exe `
        (Join-Path $PSScriptRoot 'WindowsEngineBaseline.cpp') `
        (Join-Path $sources 'InputEngine.cpp') `
        (Join-Path $sources 'InputEngineCangjie.cpp') `
        (Join-Path $sources 'InputEnginePinyin.cpp') `
        (Join-Path $sources 'InputEngineReverseLookup.cpp') `
        (Join-Path $sources 'InputEngineStroke.cpp') `
        (Join-Path $sources 'InputEngineStructure.cpp') `
        (Join-Path $sources 'InputMemory.cpp') `
        (Join-Path $sources 'ImeDatabase.cpp') `
        (Join-Path $sources 'Segmenter.cpp') `
        (Join-Path $sources 'PinyinSegmenter.cpp') `
        (Join-Path $sources 'ImeTypes.cpp') `
        (Join-Path $sources 'VirtualInputKey.cpp') `
        (Join-Path $sources 'CharacterStandard.cpp') `
        (Join-Path $sources 'Candidate.cpp') `
        (Join-Path $sources 'Converter.cpp') `
        (Join-Path $sources 'ExtraEntry.cpp') winsqlite3.lib shlwapi.lib
    $env:JYUTPING_ENGINE_BASELINE = Join-Path $output 'WindowsEngineBaseline.exe'
    $env:JYUTPING_ENGINE_DICTIONARY = $dictionary
    cargo test --locked --offline --manifest-path (Join-Path $repository 'backend/Cargo.toml') --test windows_engine_parity -- --ignored
}
finally {
    $env:JYUTPING_ENGINE_BASELINE = $previousBaseline
    $env:JYUTPING_ENGINE_DICTIONARY = $previousDictionary
    Pop-Location
}
