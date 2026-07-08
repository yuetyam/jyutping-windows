Jyutping
======

Cantonese input method for Windows using Text Services Framework (TSF).

## Build

Requirements:

- Windows 11 or later for development
- Visual Studio 2026 with the C++ desktop toolchain
- Windows 10 SDK or later
- `Jyutping\Resources\ime.sqlite3`

Build from the repository root:

```powershell
msbuild Jyutping.sln /p:Configuration=Debug /p:Platform=x64
msbuild Jyutping.sln /p:Configuration=Release /p:Platform=x64
msbuild Jyutping.sln /p:Configuration=Debug /p:Platform=Win32
msbuild Jyutping.sln /p:Configuration=Release /p:Platform=Win32
msbuild Jyutping.sln /p:Configuration=Debug /p:Platform=ARM64
msbuild Jyutping.sln /p:Configuration=Release /p:Platform=ARM64
msbuild Jyutping.sln /p:Configuration=Debug /p:Platform=ARM64EC
msbuild Jyutping.sln /p:Configuration=Release /p:Platform=ARM64EC
```

The build copies `Jyutping\Resources\ime.sqlite3` beside each built
`Jyutping.dll`.

## Package

Requirements:

- Release payloads for `x64`, `Win32`, and `ARM64EC`
- Inno Setup 7

Create both installer EXEs from the repository root:

```powershell
pwsh -File installer\Build-Installers.ps1
```

The script builds the required Release configurations, stages the installer
payloads under `installer\stage`, runs Inno Setup, and writes release artifacts
under `installer\output`:

```text
installer\output\jyutping-v0.1.0-x64.exe
installer\output\jyutping-v0.1.0-arm64.exe
installer\output\SHA256SUMS.txt
```

Use `-AppVersion` to package a different version:

```powershell
pwsh -File installer\Build-Installers.ps1 -AppVersion 0.1.1
```

The x64 installer contains x64 and x86 DLL payloads. The ARM64 installer uses
the `ARM64EC\Release\Jyutping.dll` ARM64X root DLL and does not install an x86
fallback. Uninstall is handled by Inno Setup's generated `unins000.exe` and the
Windows Apps & Features uninstall entry.

See also:
- [iOS and macOS](https://github.com/yuetyam/jyutping)
- [Android](https://github.com/yuetyam/jyutping-android)
- [HarmonyOS](https://github.com/yuetyam/jyutping-harmony)
