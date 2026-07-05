# Public Release Roadmap

This roadmap covers the work needed to ship the Windows Jyutping IME publicly.
It focuses on release engineering, packaging, signing, documentation, and launch
readiness. Core IME feature roadmaps live in the other files in this folder.

## Current Baseline

The project is currently a native Visual Studio C++ TSF IME DLL with x64 and
ARM64 build configurations. It has CI coverage for Debug and Release builds, but
there is no public distribution package yet.

Available today:

- `Jyutping.sln` builds the TSF DLL.
- `Jyutping\Resources\ime.sqlite3` is copied beside the DLL during build.
- `DllRegisterServer` and `DllUnregisterServer` implement COM and TSF
  registration.
- Version metadata exists in `Jyutping\Jyutping.rc`.
- GitHub Actions builds Debug and Release for x64 and ARM64.

Missing for public release:

- Installer or package.
- Code signing.
- Release artifact workflow.
- Fresh install, upgrade, and uninstall validation.
- Public user documentation.
- Privacy and support documentation.
- Clear versioning and release process.

## Release Goals

- Provide a downloadable installer that works for normal users.
- Support x64 and ARM64 Windows.
- Install and unregister the TSF text service cleanly.
- Sign all public binaries and installer artifacts.
- Preserve user settings and input memory across upgrades.
- Make uninstall behavior explicit and predictable.
- Publish enough documentation for users to install, use, troubleshoot, and
  report issues.
- Keep the first public release scoped and stable rather than feature-complete.

## Phase 1: Decide Distribution Shape

Choose the first public distribution format.

Preferred first release path:

- Use a classic desktop installer, such as MSI via WiX.
- Install the native DLL and resource database.
- Run elevated registration for COM and TSF profile registration.
- Publish separate architecture-specific packages if that keeps installation
  simpler.

Decisions to make:

- Whether to ship one combined installer or separate x64 and ARM64 installers.
- Whether to support 32-bit host applications with a Win32 build.
- Whether the installer keeps user data on uninstall by default.
- Whether a per-machine install is required, or whether per-user installation is
  feasible with the current registration model.

Completion criteria:

- Distribution format is selected.
- Architecture policy is documented.
- Install and uninstall ownership of files, registry keys, and user data is
  documented.

## Phase 2: Build Installer

Add installer project files to the repository.

Installer responsibilities:

- Install `Jyutping.dll`.
- Install `Resources\ime.sqlite3`.
- Run `DllRegisterServer` during install.
- Run `DllUnregisterServer` during uninstall.
- Install the correct architecture build.
- Refuse unsupported architectures with a clear error.
- Support upgrade over an older installed version.
- Leave `%LOCALAPPDATA%\Jyutping` in place unless an explicit clear-data action
  is implemented.

Suggested files:

- `installer\Jyutping.wxs`
- `installer\Jyutping.wixproj`
- `installer\README.md`

If new files are added, keep the main native project unchanged unless build
integration requires it.

Completion criteria:

- A clean Windows machine can install the IME from the generated package.
- Windows input settings show the Jyutping profile after install.
- Uninstall removes the profile and COM registration.
- Reinstall works after uninstall.

## Phase 3: Code Signing

Set up public-trust signing for release artifacts.

Artifacts to sign:

- `Jyutping.dll`
- Installer package.
- Optional bootstrapper or archive metadata.

Signing requirements:

- Use a certificate or signing service suitable for public Windows software.
- Timestamp every signature.
- Verify signatures in CI or before release publication.
- Keep private signing credentials out of the repository.

Completion criteria:

- Release DLLs are signed and timestamped.
- Installer artifacts are signed and timestamped.
- Signature verification is part of the release checklist.

## Phase 4: Release CI

Extend CI with a dedicated release workflow.

Release workflow responsibilities:

- Build `Release|x64`.
- Build `Release|ARM64`.
- Build installer artifacts.
- Sign binaries and installers.
- Generate checksums.
- Upload artifacts to a draft GitHub Release or workflow run.

Suggested workflow:

- Keep pull request CI as build-only.
- Use a separate manually triggered release workflow.
- Require a version input for release builds.
- Fail the release workflow if signing or package validation fails.

Completion criteria:

- One command or workflow dispatch produces all release artifacts.
- Artifacts are versioned consistently.
- Checksums are published with the artifacts.

## Phase 5: Versioning And Metadata

Make release identity deliberate.

Items to finalize:

- `FILEVERSION` and `PRODUCTVERSION` in `Jyutping\Jyutping.rc`.
- `FileVersion` and `ProductVersion` strings.
- Company or publisher display name.
- Product name shown in Windows.
- Installer product name and upgrade code.
- Copyright/license text.
- Icon assets.

Rules:

- Bump versions for every public release.
- Keep DLL and installer versions aligned.
- Avoid reusing a published version number for different binaries.

Completion criteria:

- Version values are updated for the first public release.
- Version bump process is documented.
- Windows file properties and installer metadata show expected names.

## Phase 6: Privacy And Data Handling

Document what the IME stores locally.

Known local data:

- Runtime logs at `%LOCALAPPDATA%\Jyutping\Jyutping.log`.
- Input memory at `%LOCALAPPDATA%\Jyutping\memory.sqlite3`.
- Settings under `HKCU\Software\Jyutping\Settings`.

Tasks:

- Add a privacy note.
- Document that input memory is local.
- Document how to clear input memory.
- Decide whether uninstall should offer a clear-user-data option.
- Confirm the IME does not make network requests.

Completion criteria:

- Privacy documentation exists before public release.
- Data locations are documented.
- Users have a documented way to remove personal input memory.

## Phase 7: Public Documentation

Replace the current WIP README with user-facing documentation.

README topics:

- What Jyutping IME is.
- Supported Windows versions and architectures.
- Install instructions.
- Uninstall instructions.
- Basic Jyutping input.
- Reverse lookup modes.
- Candidate navigation.
- Character variant shortcuts.
- ABC/Cantonese, full-width, and punctuation toggles.
- Input memory behavior.
- Troubleshooting.
- How to report issues.

Additional documentation:

- `CHANGELOG.md`
- `SECURITY.md`
- GitHub issue templates.
- Release notes template.

Completion criteria:

- A user can install and start using the IME from the README alone.
- Shortcuts and reverse lookup modes are documented.
- Known limitations are documented.

## Phase 8: QA Matrix

Run manual verification on clean systems.

Target systems:

- Windows 10 x64.
- Windows 11 x64.
- Windows 11 ARM64.
- Windows 11 ARM64 running x64 applications, if supported.

Install scenarios:

- Fresh install.
- Upgrade install.
- Uninstall.
- Reinstall after uninstall.
- Install with existing `%LOCALAPPDATA%\Jyutping` data.

Runtime scenarios:

- Normal Jyutping input.
- Candidate selection by number.
- Candidate paging and navigation.
- Reverse lookup through Pinyin, Cangjie, Stroke, and Structure.
- ABC/Cantonese mode persistence.
- Full-width and half-width output.
- Cantonese and English punctuation.
- Traditional, Hong Kong, Taiwan, and Simplified character variants.
- Input memory creation, promotion, forgetting, and clearing.
- Light and dark themes.
- High DPI displays.
- Secure desktop or elevated app behavior where applicable.
- 32-bit application behavior, if Win32 support is shipped or intentionally
  omitted.

Completion criteria:

- QA checklist is completed for every supported architecture.
- Known release-blocking bugs are fixed.
- Non-blocking known issues are listed in release notes.

## Phase 9: Public Release

Prepare the first public release.

Release checklist:

- Build clean release artifacts.
- Verify signatures.
- Verify checksums.
- Install artifacts on clean test systems.
- Confirm README and release notes match the shipped version.
- Create a GitHub Release.
- Attach installers and checksums.
- Mark architecture support clearly.
- Include upgrade and uninstall notes.

Completion criteria:

- Public artifacts are available.
- Documentation points users to the correct installer.
- Issues are enabled and triage labels exist.

## Phase 10: Post-Release Maintenance

Plan for support after launch.

Tasks:

- Monitor crash reports and user issues.
- Track installer failures separately from IME runtime bugs.
- Keep a known-issues list.
- Publish patch releases with clear changelogs.
- Avoid changing profile GUIDs, CLSIDs, or upgrade codes unless there is a
  deliberate migration plan.

Completion criteria:

- There is a repeatable patch release process.
- User-reported installation and runtime issues have triage paths.
- Public releases remain reproducible from source tags.

## First Public Release Exit Criteria

The IME is ready for a first public release when all of the following are true:

- Release x64 and ARM64 builds succeed.
- Installer packages install, upgrade, and uninstall cleanly.
- Public binaries and installers are signed and timestamped.
- Version metadata is final for the release.
- Privacy and data handling are documented.
- README is user-facing rather than WIP-only.
- QA matrix passes on supported systems.
- GitHub Release artifacts include checksums and release notes.
- Known limitations are documented.
