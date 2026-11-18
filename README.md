Jyutping
======

<a href="https://www.instagram.com/jyutping_app">
        <img src="images/badge-instagram.png" alt="Instagram" width="150"/>
</a>
<a href="https://www.threads.net/@jyutping_app">
        <img src="images/badge-threads.png" alt="Threads" width="150"/>
</a>
<a href="https://t.me/jyutping">
        <img src="images/badge-telegram.png" alt="Telegram" width="150"/>
</a>
<a href="https://jq.qq.com/?k=4PR17m3t">
        <img src="images/badge-qq.png" alt="QQ" width="150"/>
</a>
<br>
<br>

Cantonese input method for Windows.

Also available on other systems:
- [iOS and macOS](https://github.com/yuetyam/jyutping)
- [Android](https://github.com/yuetyam/jyutping-android)
- [HarmonyOS](https://github.com/yuetyam/jyutping-harmony)

## Screenshots
<img src="images/screenshot.png" alt="Screenshot of Jyutping for Windows" width="316"/>

## Download and install
Please visit our website: https://jyutping.app

## How to build

Requirements:

- Windows 11
- Visual Studio 2026, with Windows 11 SDK, the C++ desktop toolchain, and the WinUI toolchain
- Rust toolchain
- SQLite3

Generate `Jyutping\Resources\ime.sqlite3` from the repository root:

```powershell
cargo run --release --manifest-path preparing\Cargo.toml
```

Then build from the repository root:

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

The build copies `Jyutping\Resources\ime.sqlite3` beside each built `Jyutping.dll` and fails when the generated database is missing.

The solution also builds the native updater for x64 and ARM64. Release versions come from `version.txt`; packaging can override them consistently with `-AppVersion`. See [Windows installers and updates](packaging/README.md) for manual update checks, signing, appcast generation, and validation.

## Packaging

Requirements:

- Release payloads for `x64`, `Win32`, and `ARM64EC`
- Inno Setup 7
- The official Inno Setup `ChineseSimplified.isl` and `ChineseTraditional.isl` language files installed in the compiler's `Languages` directory, normally `C:\Program Files\Inno Setup 7\Languages`

Create both installer EXEs from the repository root:

```powershell
pwsh -File packaging\Build-Installers.ps1
```

The script builds the required Release configurations, stages the installer payloads under `packaging\stage`, runs Inno Setup, and writes release artifacts under `packaging\output`:

```text
packaging\output\jyutping-v0.1.0-x64.exe
packaging\output\jyutping-v0.1.0-arm64.exe
packaging\output\SHA256SUMS.txt
```

The x64 installer contains x64 and x86 DLL payloads.

The ARM64 installer uses the `ARM64EC\Release\Jyutping.dll` ARM64X root DLL and does not install an x86 fallback.

Uninstall is handled by Inno Setup's generated `unins000.exe` and the Windows Apps & Features uninstall entry.

## Credits
- [Rime-Cantonese](https://github.com/rime/rime-cantonese) (Cantonese Lexicon)
- [OpenCC](https://github.com/BYVoid/OpenCC) (Traditional-Simplified Character Conversion)
- [JetBrains](https://www.jetbrains.com/) (Licenses for Open Source Development)

## Support this project
Website: https://jyutping.app/donate

愛發電: https://afdian.com/a/jyutping

Ko-fi: https://ko-fi.com/zheung

Patreon: https://patreon.com/bingzheung

PayPal: https://paypal.me/bingzheung

Bitcoin: `bc1qx5tjmlvq8ydmfzxt5fru7vqq0khjkhf2savheh`

<img src="images/sponsor.jpg" alt="WeChat Sponsor" width="180"/>
