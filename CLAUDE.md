# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Jyutping is a Windows Input Method Editor for Cantonese Jyutping romanization written in C++. It implements the Text Services Framework (TSF) and runs as a COM DLL that integrates with the Windows text input system.

- **Development Platform**: Windows 11+ (x64 and ARM64)
- **Target Platform**: Windows 10+ (x64 and ARM64)
- **Language**: C++ (C++23)
- **Build System**: Visual Studio 2026 / MSBuild

## Build Commands

```bash
# Build from command line
msbuild Jyutping.sln /p:Configuration=Debug /p:Platform=x64
msbuild Jyutping.sln /p:Configuration=Release /p:Platform=x64

# ARM64 builds
msbuild Jyutping.sln /p:Configuration=Debug /p:Platform=ARM64
msbuild Jyutping.sln /p:Configuration=Release /p:Platform=ARM64
```

Output: `Debug/Jyutping.dll` or `Release/Jyutping.dll`

Currently, it cannot be built on macOS or Linux

## Architecture

### Core Components

1. **CJyutping** (`Jyutping.h/cpp`) - Main IME class implementing TSF COM interfaces:
   - `ITfTextInputProcessorEx` - Text input processor
   - `ITfKeyEventSink` - Keyboard event handling
   - `ITfCompositionSink` - Composition state management

2. **CCompositionProcessorEngine** (`CompositionProcessorEngine.h/cpp`) - Input processing engine:
   - Keystroke processing and virtual key buffer management
   - Candidate list generation
   - Dictionary lookup coordination

3. **Dictionary Engine** (`TableDictionaryEngine.h/cpp`, `DictionaryParser.h/cpp`):
   - Parses dictionary files: `"keyword"="candidate word"="comment text"`
   - Memory-mapped file access via `CFileMapping`
   - Wildcard search support

4. **UI Components** (`CandidateWindow.h/cpp`, `CandidateListUIPresenter.h/cpp`):
   - Direct2D/DirectWrite rendering
   - Light/dark mode support
   - Scrolling and pagination

### Input Flow

```
Keyboard Input → ITfKeyEventSink → KeyHandler → CompositionProcessorEngine
    → TableDictionaryEngine (lookup) → CandidateWindow (display)
    → Composition finalization → Text insertion
```

### Key Files by Functionality

| Area | Files |
|------|-------|
| Entry point | `DllMain.cpp`, `Jyutping.h/cpp` |
| Keyboard handling | `KeyEventSink.cpp`, `KeyHandler.cpp` |
| Candidate generation | `CompositionProcessorEngine.cpp` |
| Dictionary lookup | `DictionaryParser.cpp`, `TableDictionaryEngine.cpp` |
| UI rendering | `CandidateWindow.cpp`, `CandidateListUIPresenter.cpp` |
| Configuration | `Define.h` |

## Debugging

Log file: `%LOCALAPPDATA%\Jyutping\Jyutping.log`, or fallback to the temp path

Use `Global::Log()` from `Logger.h` to add debug output.

## Code Style

- UTF-8 encoding
- `LF` for line break
- 4-space indentation for C++ files
- See `.editorconfig` for details
