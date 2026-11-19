# Jyutping backend

`core` holds the platform-independent engine code. `server` builds `JyutpingServer.exe`, with Rust lookup and Windows SQLite access behind a C++ adapter for Windows RPC. The server takes an optional dictionary path and defaults to `ime.sqlite3` beside the executable. It serves normal and reverse candidates from the bundled dictionary over protocol version 2. Version 1 serves the original exact-spell request. [Protocol](../Shared/Protocol/README.md)

## ARM64 development build

Run these commands from the repository root using an ARM64-targeting Visual Studio Developer PowerShell. Generate the dictionary using `AGENTS.md` if needed.

```powershell
cargo test --locked --offline --manifest-path backend/Cargo.toml --workspace
cargo build --locked --offline --manifest-path backend/Cargo.toml
msbuild Jyutping.sln /p:Configuration=Debug /p:Platform=ARM64 /p:EnableIpcProbe=true /p:IpcProbeTransport=BackendRpc /p:EnableIpcTypingProbe=true /m
$dictionary = (Resolve-Path Jyutping/Resources/ime.sqlite3).Path
& backend/target/debug/JyutpingServer.exe $dictionary
```

Run the listener in the client's logon session. Pass a package family name after the dictionary path to include an AppContainer in the test ACL. Ctrl+C or Ctrl+Break requests shutdown when running in a console.

The opt-in DLL attempts recovery from a missing listener on its worker, subject to a five-second launch backoff per client. A normal-privilege unpackaged host can start `JyutpingServer.exe --installed` from the native installer path in the 64-bit view of `HKLM\Software\Jyutping\InstallPath`. Installed mode loads the dictionary beside the server and opens the user's preferences and input memory. The development smoke test used a staged server and a process-local registry mapping.

The opt-in Debug ARM64 DLL uses version 2 to fetch at most 16 normal or reverse candidates during composition. It sends up to 12 query keys and the active character standard. It uses the in-process engine for inputs outside that range and while the backend is unavailable at the start of a revision. During a pending request, Space or a candidate number can defer one selection until the reply or the existing 300 ms deadline. Stroke digits continue to enter strokes. Once candidates are visible, the TSF list handles navigation and selection. Selection uses a raw commit when it completes without a candidate.

Pass `--memory <path>` to enable the input-memory capability with that database. The DLL queues learning and forgetting for server candidates when the capability is advertised. The worker uses the original lexicon, preserves the queue order within a client, and sends each queued write once. Learning is optional for a typing session and the worker logs failed writes by status code.

Pass `--settings-key Software\JyutpingTest\Preferences` for a separate HKCU key when testing the settings app. Use `--settings` to use the normal Windows preference location.

## Engine parity and input memory

Run the comparison in the same ARM64 Developer PowerShell after generating the dictionary.

```powershell
pwsh -NoProfile -File backend/tests/Test-WindowsEngineParity.ps1
```

The test builds a standalone reference from the current Windows C++ engine and compares ordered lexicons, consumed key counts, reading marks, display conversion, and original lexicon text. It covers Jyutping, Pinyin, Cangjie and Quick, stroke, structure, plain text, emoji, symbols, and learned suggestions. The Swift desktop reference was `apple-jyutping@01d20107753929189d758016a5ef0c86aa6a6789`. Windows-specific ordering, fallbacks, and variant mappings follow the C++ source.

The Rust `InputMemory` API uses the existing `memory2608` schema and `TRUNCATE` journal, migrates legacy `core_memory` entries in one transaction, and uses a 250 ms SQLite busy timeout. Learning writes the original Cantonese lexicon and serializes frequency updates in a write transaction. The tests use isolated databases for migration, rollback, concurrent connections, learning, forgetting, clearing, and C++ interoperability.

### Portable input memory backups

Windows and macOS use `memory-yyyyMMdd-HHmmss.sqlite3` backups with SQLite `user_version = 1`. The `input_memory` table contains `word`, `romanization`, `frequency`, and `latest` (Unix milliseconds), with a unique constraint on `(word, romanization)` and indexes named `ix_input_memory_frequency` and `ix_input_memory_latest`. Each export is a standalone SQLite database in DELETE journal mode.

Import validates the version, table, columns, and SQLite integrity before merging valid rows in one transaction. It keeps the higher frequency and later timestamp for each word and romanization. It aborts if a concurrent import or clear advanced the destination generation while it opened and checked the backup. The Settings app delegates file transfers to a separate backend process and displays their status.

## Protocol smoke test

Build the ARM64 server first. In an ARM64-targeting Visual Studio Developer PowerShell, build the client from the repository root.

```powershell
New-Item -ItemType Directory -Force backend/target/smoke-arm64 | Out-Null
Push-Location backend/target/smoke-arm64
try {
    midl /nologo /robust /env arm64 /prefix client Client_ /prefix server Server_ ../../../Shared/Protocol/JyutpingRpc.idl
    cl /nologo /W4 /MT /std:c++20 /EHsc /I. /FeRpcSmoke.exe ../../tests/RpcSmoke.cpp ../../../Jyutping/BackendRpcClient.cpp JyutpingRpc_c.c
}
finally {
    Pop-Location
}
pwsh -NoProfile -File backend/tests/Test-Rpc.ps1 -ServerPath backend/target/debug/JyutpingServer.exe -ClientPath backend/target/smoke-arm64/RpcSmoke.exe
```

Repeat with `/env x64` and a separate output directory in an x64-targeting Visual Studio Developer PowerShell to verify the emulated client. The test starts and restarts its own listener and checks both protocol versions, normal and reverse candidate delivery, character conversion, independent concurrent clients, instance validation, 64-bit identities, learning, persistence, forgetting, and reconnect.

The ARM64 server passed the native and emulated x64 smoke tests on Windows 11 ARM64. The previous version 1 client binaries also received the exact-spell candidate. A native RichEdit host exercised early Space and number selection, partial consumption, all four reverse-lookup modes, learning, and raw fallback after listener loss. A temporary standalone AppContainer received version 2 candidates with its SID granted access and got access denied with the default ACL.
