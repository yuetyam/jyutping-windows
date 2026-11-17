# Windows installers and updates

The language-bar settings menu launches the native `JyutpingUpdater.exe` from `Program Files\Jyutping\Updater`. The helper uses WinSparkle 0.9.4 and checks `https://jyutping.app/windows.xml` only when the user selects **Check for updates**. Automatic checks and their permission prompt are disabled before every initialization. Downloaded installers open with the full existing Inno Setup UI and request elevation through the installer.

## Build and staging

`version.txt` supplies the default product version. `Version.targets` generates matching version definitions for both C++ projects. Packaging passes `-AppVersion` through to MSBuild and validates every product binary, including when using `-SkipBuild`.

```powershell
pwsh -File packaging\Build-Installers.ps1
pwsh -File packaging\Build-Installers.ps1 -AppVersion 0.12.0
```

The solution maps x64 to the x64 helper, and ARM64/ARM64EC to the native ARM64 helper. Win32 builds compile the IME without a separate x86 helper. Packaging retains the existing x64+x86 IME and ARM64X IME layouts, adds one native helper per package, and installs WinSparkle's complete bundled notices. The ARM64 package requires Windows 11 because it ships the ARM64X IME.

The installer records its native location in the 64-bit view of `HKLM\Software\Jyutping\InstallPath`. Both x86 and x64 callers read that same location. Developer DLL registration alone does not install the updater: use an installer for end-to-end checks, or stage `Updater\JyutpingUpdater.exe` and its matching `WinSparkle.dll` under a test installation and explicitly provision that installation value.

The updater uses `HKCU\Software\Jyutping\WinSparkle` for its preferences. Input settings and input memory are separate. The helper is unique per user/session; repeat invocations activate its current dialog without restarting the network request.

## Upgrading while files are in use

Before unregistering or copying files, the installer checks whether existing IME, lexicon, and updater files can be replaced. If files remain in use, it asks the user to close applications and retry. Switching to another input method and signing out or restarting Windows may be necessary. The installer does not automatically close host applications. Replacement flags also cover the lexicon and updater if a file becomes busy after that check. Follow any restart prompt before relying on the new IME version.

## Sign and prepare a release

Build final installers first. If Authenticode signing is used, apply it to staged product binaries and final installers through `-SignToolCommand` before generating Ed25519 signatures. The checksum file is generated from the final installer bytes.

The shared public key is defined in `Shared\UpdateConfiguration.h`. Provision the existing Mac release private key in the signing environment; the public key alone cannot sign a Windows release. Never generate a replacement production key or put private key material in this repository. `winsparkle-tool` accepts Sparkle's base64 seed and legacy key formats.

To sign and generate a feed:

```powershell
pwsh -File packaging\New-WindowsAppcast.ps1 -PrivateKeyFile C:\ReleaseSecrets\jyutping-ed25519 -ReleaseNotes 'Release notes for this version.'
```

Alternatively, sign final Windows installer files in the existing Mac release environment and provide their signatures:

```powershell
pwsh -File packaging\New-WindowsAppcast.ps1 -X64Signature '<base64 signature>' -Arm64Signature '<base64 signature>' -ReleaseNotes 'Release notes for this version.'
```

By default, generation includes only the current release's x64 and ARM64 items. Pass `-PreviousAppcast <path>` only when earlier signed release items need to be retained. Generation verifies both final installers against the embedded production public key, checks version and architecture metadata, and writes `packaging\output\windows.xml` with correct lengths and HTTPS URLs. A failed validation leaves the previous output untouched. Rebuilding installers invalidates their old signatures and removes the generated output feed.

Upload installers under `https://dl.jyutping.app/jyutping-windows/<version>/` first, verify the downloaded bytes, then copy the generated feed to the website repository's root `windows.xml` and publish through its usual process. Coordinate the four localized Windows download pages, checksums, release notes, and sitemap dates. Feed publication is a release operation; ordinary development builds do not publish files or require a signing key.

Earlier installations without the updater helper must first update through the website's installer.

## Validation

Routine verification uses `msbuild Jyutping.sln /p:Configuration=Debug /p:Platform=ARM64`. The installer integration gate builds the required Release payloads through `Build-Installers.ps1`.

```powershell
pwsh -File tests\Test-UpdateRelease.ps1
pwsh -File tests\Test-UpdateRelease.ps1 -CreateFixtures
pwsh -File tests\Test-UpdaterInteraction.ps1
```

The release tests use temporary keys and an isolated copy of the release scripts to exercise valid and invalid signatures, modified installer bytes, product versions, architecture metadata, feed regeneration, XML escaping, dates, and lengths against the actual built installers. Production configuration is unchanged.

`-CreateFixtures` runs the same release tests with small version-resource fixtures instead of built installers; CI uses this mode without needing Inno Setup or production secrets. The interaction test briefly opens and closes the helper's own dialogs, checks repeat activation and subsequent launch, and confirms that the saved automatic-check state is disabled. It contacts the configured feed and never selects Install.

Installed manual testing remains necessary for both menu surfaces, native x64 and ARM64 systems, x86-on-x64 and x64-on-ARM64 hosts, repeated invocations, restricted/elevated hosts, result-dialog dismissal, download cancellation, UAC cancellation, files in use, sign-out/reboot, and uninstall. These are release gates in `plans\19.winsparkle-updates.md`.
