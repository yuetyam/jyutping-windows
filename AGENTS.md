# AGENTS.md

Guidance for coding agents working in this repository.

## Project Overview

Jyutping is a Windows Input Method Editor for Cantonese Jyutping romanization. It is written in C++ and implements the Windows Text Services Framework (TSF) as a COM DLL integrated with the Windows text input system.

- Development platform: Windows 11 or later, x64 and ARM64
- Target platform: Windows 10 or later, x64 and ARM64
- Language: C++23
- Build system: Visual Studio 2026 / MSBuild
- Output: `Debug/Jyutping.dll` or `Release/Jyutping.dll`

This project currently cannot be built on macOS or Linux.

## Local Terminal and Shell

The user is using Windows Terminal and PowerShell.

Prefer `rg` for searching files and text when available.

## Build Commands

Build from the repository root:

```powershell
msbuild Jyutping.sln /p:Configuration=Debug /p:Platform=x64
msbuild Jyutping.sln /p:Configuration=Release /p:Platform=x64
msbuild Jyutping.sln /p:Configuration=Debug /p:Platform=ARM64
msbuild Jyutping.sln /p:Configuration=Release /p:Platform=ARM64
```

## Architecture

Core components:

- `Jyutping/Jyutping.h` and `Jyutping/Jyutping.cpp`: main IME class implementing TSF COM interfaces, including `ITfTextInputProcessorEx`, `ITfKeyEventSink`, and `ITfCompositionSink`.
- `Jyutping/CompositionProcessorEngine.h` and `Jyutping/CompositionProcessorEngine.cpp`: keystroke processing, virtual key buffer management, candidate list generation, and dictionary lookup coordination.
- `Jyutping/TableDictionaryEngine.h`, `Jyutping/TableDictionaryEngine.cpp`, `Jyutping/DictionaryParser.h`, and `Jyutping/DictionaryParser.cpp`: dictionary parsing and memory-mapped lookup support.
- `Jyutping/CandidateWindow.h`, `Jyutping/CandidateWindow.cpp`, `Jyutping/CandidateListUIPresenter.h`, and `Jyutping/CandidateListUIPresenter.cpp`: candidate UI rendering with Direct2D and DirectWrite.
- `Jyutping/Define.h`: project configuration constants.

Input flow:

```text
Keyboard input -> ITfKeyEventSink -> KeyHandler -> CompositionProcessorEngine
    -> TableDictionaryEngine lookup -> CandidateWindow display
    -> composition finalization -> text insertion
```

## Code Style

Always check `.editorconfig` before editing files and follow its rules.

Current formatting rules include:

- Use UTF-8 encoding.
- Use LF line endings.
- Insert a final newline.
- Trim trailing whitespace.
- Use 4-space indentation for C, C++, C#, XML, and XAML files.
- Use 2-space indentation for Visual Studio project files such as `.vcxproj` and `.vcxproj.filters`.
- Use tabs for `.sln` files.

For C++ code:

- Prefer descriptive names over cryptic abbreviations.
- Use private member names with a leading underscore, for example `_renderTarget`.
- Use camelCase for multi-word names, for example `_pageStartIndices`.
- Avoid Hungarian notation that repeats type information.
- Use descriptive boolean names.
- Add comments only where they clarify non-obvious behavior or important state.

## Debugging

Runtime logging is written to:

```text
%LOCALAPPDATA%\Jyutping\Jyutping.log
```

If that path is unavailable, logging falls back to the temp path.

Use `Global::Log()` from `Jyutping/Logger.h` for debug output.

## Testing and Verification

There is no dedicated automated test suite documented in this repository. For code changes, at minimum run the relevant MSBuild configuration when the Windows build tools are available. Prefer the architecture and configuration touched by the change, and broaden to both x64 and ARM64 when changing shared TSF, dictionary, or UI behavior.

If a build cannot be run, report that clearly with the reason.
