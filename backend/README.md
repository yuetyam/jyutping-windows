# Jyutping backend

`core` holds the platform-independent engine code. `server` builds `JyutpingServer.exe`, with Rust lookup and Windows SQLite access behind a C++ adapter for Windows RPC. The server takes an optional dictionary path and defaults to `ime.sqlite3` beside the executable. It currently supports a read-only exact-spell lookup. [Protocol](../Shared/Protocol/README.md)

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

Repeat with `/env x64` and a separate output directory in an x64-targeting Visual Studio Developer PowerShell to verify the emulated client. The test starts and restarts its own listener and checks candidate delivery, independent concurrent clients, version and instance validation, 64-bit identity fields, and reconnect from a live client.

The ARM64 server passed the native and emulated x64 smoke tests on Windows 11 ARM64. A native RichEdit host exercised early Space selection and empty-result fallback through the opt-in DLL. A temporary standalone AppContainer received a candidate with its SID granted access and got access denied with the default ACL. Ctrl+Break stopped the console listener and returned exit code zero.
