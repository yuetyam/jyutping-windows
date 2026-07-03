# AGENTS.md

## Project Overview
This project is a Windows Input Method Editor for Cantonese Jyutping romanization. It is a native Visual Studio C++ project that implements the Windows Text Services Framework (TSF) as an in-process COM DLL integrated with the Windows text input system. The input engine supports normal Jyutping input and reverse lookup from other input methods back to Cantonese romanizations.

## Build
- Development platform: Windows 11 or later, x64 and ARM64
- Target platform: Windows 10 or later, x64 and ARM64
- Language: C++23 and C17
- Build system: Visual Studio 2026 / MSBuild

Build from the repository root:

```powershell
msbuild Jyutping.sln /p:Configuration=Debug /p:Platform=x64
msbuild Jyutping.sln /p:Configuration=Release /p:Platform=x64
msbuild Jyutping.sln /p:Configuration=Debug /p:Platform=ARM64
msbuild Jyutping.sln /p:Configuration=Release /p:Platform=ARM64
```

The build links against `d2d1.lib`, `dwrite.lib`, `shlwapi.lib`, and `winsqlite3.lib`, and copies `Jyutping\Resources\ime.sqlite3` into the target output after build.

## Project Architecture
The solution is `Jyutping.sln`; the native project is `Jyutping\Jyutping.vcxproj`. Most source files are under `Jyutping\`.

- COM and TSF registration: `DllMain.cpp`, `Server.cpp`, and `Register.cpp` initialize process-wide state, expose the COM class factory, implement `DllRegisterServer` and `DllUnregisterServer`, register the TSF language profile, and declare supported TSF categories.
- Main text service: `CJyutping` in `Jyutping.h` and `Jyutping.cpp` is the central TSF object. It implements activation, deactivation, key handling, thread/document sinks, text edit sinks, composition callbacks, display attributes, language profile notifications, thread focus handling, function provider support, and touch keyboard layout preference.
- TSF sink implementations: `ThreadMgrEventSink.cpp`, `TextEditSink.cpp`, `KeyEventSink.cpp`, `ActiveLanguageProfileNotifySink.cpp`, `ThreadFocusSink.cpp`, and `FunctionProviderSink.cpp` split the `CJyutping` interface implementations by responsibility.
- Composition flow: `KeyEventSink.cpp` decides whether a key should be eaten, then `_InvokeKeyHandler` schedules `CKeyHandlerEditSession`. `KeyHandler.cpp`, `KeyHandlerEditSession.cpp`, `Composition.cpp`, `StartComposition.cpp`, and `EndComposition.cpp` update TSF ranges, composition text, display attributes, and final committed text.
- Input engine: `CompositionProcessorEngine.*` owns keystroke tables, preserved keys, punctuation and full-width conversion, candidate index ranges, language bar state, compartments, the raw input buffer, and reverse-lookup buffer handling. It delegates Jyutping suggestions and reverse lookup to `Ime::InputEngine`.
- IME lookup: `InputEngine.*`, `InputEngineReverseLookup.cpp`, `ImeDatabase.*`, `ImeTypes.*`, `Segmenter.*`, `PinyinSegmenter.*`, and `VirtualInputKey.*` convert keystrokes into input keys, segment Jyutping and Pinyin syllables, query the SQLite lexicon database, and return ranked `Ime::Lexicon` suggestions.
- Reverse lookup: `InputEnginePinyin.cpp`, `InputEngineCangjie.cpp`, `InputEngineStroke.cpp`, and `InputEngineStructure.cpp` implement the ported reverse lookup methods. The first input key selects the method (`r` = Pinyin, `v` = Cangjie, `x` = Stroke, `q` = Structure); `CompositionProcessorEngine.*` keeps that selector in the user-visible buffer but passes only the remaining query keys to `Ime::InputEngine::ReverseLookup`.
- Candidate UI: `CandidateListUIPresenter.*` bridges TSF candidate UI interfaces with the custom Win32 candidate window. `CandidateWindow.*`, `BaseWindow.*`, `ShadowWindow.*`, `ScrollBarWindow.*`, and `ButtonWindow.*` implement the visible UI using Win32, DirectWrite, and Direct2D.
- Candidate positioning: `TfTextLayoutSink.*` and `GetTextExtentEditSession.*` track composition text layout and move the candidate window near the active text range.
- Language bar, compartments, and display attributes: `LanguageBar.*`, `Compartment.*`, `DisplayAttribute*.cpp`, `DisplayAttributeInfo.*`, and `EnumDisplayAttributeInfo.*` manage TSF UI state, mode toggles, and visual composition attributes.
- Search integration and TSF candidate wrappers: `SearchCandidateProvider.*`, `TipCandidateList.*`, `TipCandidateString.*`, and `EnumTfCandidates.*` expose suggestions through TSF function provider and candidate list interfaces.
- Shared utilities and global data: `Globals.*`, `Define.h`, `JyutpingBaseStructure.*`, `JyutpingStructureArray.h`, `RegKey.*`, and `Logger.*` provide GUIDs, global resources, small containers, registry helpers, theme colors, punctuation tables, and logging.

The main runtime path is:

```text
TSF key event
  -> CJyutping::_IsKeyEaten / _InvokeKeyHandler
  -> CKeyHandlerEditSession
  -> CJyutping composition handlers
  -> CCompositionProcessorEngine
  -> Ime::InputEngine / ImeDatabase / Segmenter / PinyinSegmenter
  -> CCandidateListUIPresenter / CCandidateWindow
  -> selected candidate committed back through TSF ranges
```

Reverse lookup follows the same TSF composition and candidate UI path, but `CCompositionProcessorEngine` recognizes the leading method selector and calls `Ime::InputEngine::ReverseLookup` instead of `Suggest`. Reverse lookup candidates display the matched character or word as candidate text and the Cantonese romanization as the comment.

## Debugging
Runtime logging is written to:

```text
%LOCALAPPDATA%\Jyutping\Jyutping.log
```

If that path is unavailable, logging falls back to the temp path.

## Coding
Check the root `.editorconfig` for code style and formatting.

- Match the style of the surrounding file, especially in COM and TSF interface implementations.
- Prefer descriptive names over cryptic abbreviations.
- For new private members, use a leading underscore, for example `_renderTarget`.
- Use camelCase for multi-word names, for example `_pageStartIndices`.
- Avoid adding Hungarian notation that only repeats type information.
- Use descriptive boolean names.
- Add comments where they clarify non-obvious behavior or important state.
- When adding/deleting source files, update both `Jyutping\Jyutping.vcxproj` and `Jyutping\Jyutping.vcxproj.filters`.

## Porting from the Swift/macOS version of code
- The reverse lookup methods from the macOS/Swift code have been ported into the Windows C++ input engine: Pinyin, Cangjie, Stroke, and Structure are implemented and wired into composition/candidate handling.
- The Windows version should remain dedicated to the 26-key QWERTY desktop input; 9-key and other layouts are excluded.
