# AGENTS.md

## Project Overview
This project is a Windows Input Method Editor for Cantonese Jyutping romanization. It is a native Visual Studio C++ project that implements the Windows Text Services Framework (TSF) as an in-process COM DLL integrated with the Windows text input system. The input engine supports normal Jyutping input and reverse lookup from other input methods back to Cantonese romanizations.

## Build
- Development platform: Windows 11 or later, x64 and ARM64
- Target platform: Windows 10 version 1809 (build 17763) or later for x64 and Win32, and Windows 11 (build 22000) or later for ARM64 and ARM64EC/ARM64X
- Language: C++23 and C17
- Build system: Visual Studio 2026 / MSBuild

Build from the repository root:

For verification builds, run only the `Debug|ARM64` variant to save build time. Use the full matrix below when a broader platform or configuration build is explicitly required.

```powershell
cargo run --release --manifest-path preparing\Cargo.toml
msbuild Jyutping.sln /p:Configuration=Debug /p:Platform=x64
msbuild Jyutping.sln /p:Configuration=Release /p:Platform=x64
msbuild Jyutping.sln /p:Configuration=Debug /p:Platform=Win32
msbuild Jyutping.sln /p:Configuration=Release /p:Platform=Win32
msbuild Jyutping.sln /p:Configuration=Debug /p:Platform=ARM64
msbuild Jyutping.sln /p:Configuration=Release /p:Platform=ARM64
msbuild Jyutping.sln /p:Configuration=Debug /p:Platform=ARM64EC
msbuild Jyutping.sln /p:Configuration=Release /p:Platform=ARM64EC
```

The build links against `d2d1.lib`, `dwrite.lib`, `shlwapi.lib`, and `winsqlite3.lib`, and copies the generated `Jyutping\Resources\ime.sqlite3` into the target output after build.

## Packaging
- Packaging requires Inno Setup 7. `packaging\Build-Installers.ps1` searches `Program Files\Inno Setup 7\ISCC.exe` first, then Inno Setup 6, then `ISCC.exe` on `PATH`.
- The selected Inno Setup compiler must have the official `ChineseSimplified.isl` and `ChineseTraditional.isl` files installed in its `Languages` directory. For the standard Inno Setup 7 installation, place them under `C:\Program Files\Inno Setup 7\Languages`.
- Build both installer packages from the repository root:

```powershell
pwsh -File packaging\Build-Installers.ps1
```

- The script builds `Release|x64`, `Release|Win32`, and `Release|ARM64EC`, including the native x64/ARM64 updater helpers, stages payloads under `packaging\stage`, compiles both Inno Setup scripts, and writes artifacts to `packaging\output`.
- Output filenames are:

```text
packaging\output\jyutping-v<version>-x64.exe
packaging\output\jyutping-v<version>-arm64.exe
packaging\output\SHA256SUMS.txt
```

- The default product version comes from `version.txt`. `Version.targets` generates matching IME and updater metadata. Use `-AppVersion <version>` to override the version consistently in product builds, installer metadata, and filenames. Packaging rejects mismatched payload versions, including with `-SkipBuild`.
- Use `-SkipBuild` only when the required Release payloads already exist.
- Use `-SignToolCommand "<command>"` to sign staged DLLs before packaging and final installer EXEs after packaging. The script appends each file path to the command.
- x64 installer payloads:

```text
packaging\stage\x64\x64\Jyutping.dll
packaging\stage\x64\x86\Jyutping.dll
packaging\stage\x64\ime.sqlite3
packaging\stage\x64\Updater\JyutpingUpdater.exe
packaging\stage\x64\Updater\WinSparkle.dll
```

- ARM64 installer payloads:

```text
packaging\stage\ARM64\Jyutping.dll
packaging\stage\ARM64\ime.sqlite3
packaging\stage\ARM64\Updater\JyutpingUpdater.exe
packaging\stage\ARM64\Updater\WinSparkle.dll
```

- The ARM64 installer uses `ARM64EC\Release\Jyutping.dll` as the ARM64X root DLL. Do not add an x86 fallback to the ARM64 installer.
- The installers use Inno Setup's generated `unins000.exe` and Windows Apps & Features uninstall entry. Do not add a dedicated `uninstall.exe` launcher unless the roadmap is explicitly changed.
- Manual update checks run in the separate native helper, with WinSparkle 0.9.4 pinned under `third_party`. Automatic checks and their opt-in prompt stay disabled. Both IME architectures read the native installation path from the 64-bit view of `HKLM\Software\Jyutping\InstallPath`.
- `packaging\New-WindowsAppcast.ps1` verifies final installer signatures against the shared Mac public key before generating the Windows feed. Production signing and publication require the existing private key in the release environment. See `packaging\README.md` for the release workflow and test commands.

## Debugging
Runtime logging is implemented in `Jyutping\Logger.cpp`.

- `Global::Log` writes each line to `OutputDebugStringW`.
- `Global::LogFilePath()` currently writes the file log under the temp directory, normally `%TEMP%\Jyutping\Logs\Jyutping.log`.
- `Global::UserDataDirectory()` is separate from logging; it prefers `%LOCALAPPDATA%\Jyutping` and falls back to the temp directory.

## Coding
Check the root `.editorconfig` for code style and formatting.

C++ code:
- Match the style of the surrounding file, especially in COM and TSF interface implementations.
- Prefer descriptive names over cryptic abbreviations.
- For new private members, use a leading underscore, for example `_renderTarget`.
- Use camelCase for multi-word names, for example `_pageStartIndices`.
- Avoid adding Hungarian notation that only repeats type information.
- Use descriptive boolean names.
- Add comments where they clarify non-obvious behavior or important state.
- When adding/deleting source files, update both `Jyutping\Jyutping.vcxproj` and `Jyutping\Jyutping.vcxproj.filters`.

## Porting from the Swift/macOS version of code
- The Windows version should remain dedicated to the 26-key QWERTY desktop input; 9-key and other layouts are excluded.
