# Milestone 0 IPC transport probes

The opt-in Debug DLL submits a synthetic `hoenggong` query during TSF activation and after each fresh normal lookup. The backend reads one exact-spell candidate from the bundled `lexicon_core` table with Windows SQLite. The DLL measures its current synchronous lookup, submits at most one IPC request per engine, and retains the latest unsent revision. A message-only window reports replies on the TSF thread and identifies stale revisions. The local engine supplies the candidates, reading text, and selection used by the host.

Both transports use a 256-byte reply buffer. Launch the listener manually from the same logon session as the client. The named pipe uses one request per connection, a user-specific ACL, and a server-side session check. It rejects remote clients. The DLL checks the pipe owner and server session and cancels pending I/O after a 250 ms reply wait. The single-instance pipe listener handles one client at a time, so an intentionally delayed request can keep it busy.

## Build and run

Use an ARM64-targeting Visual Studio Developer PowerShell. Generate the dictionary with the command in `AGENTS.md` if needed.

```powershell
cargo build --offline --manifest-path experiments/ipc-probe/Cargo.toml
msbuild Jyutping.sln /p:Configuration=Debug /p:Platform=ARM64 /p:EnableIpcProbe=true /m
cl /nologo /std:c++latest /EHsc /DUNICODE /D_UNICODE /DJYUTPING_IPC_PROBE /Foexperiments/ipc-probe/target/Smoke.obj /Feexperiments/ipc-probe/target/Smoke.exe experiments/ipc-probe/Smoke.cpp /link user32.lib advapi32.lib
pwsh -NoProfile -File experiments/ipc-probe/Test-Smoke.ps1
```

The smoke test runs the DLL's client code in a console process with an owner-thread message pump. It checks a missing backend, a real reply, cancellation before reply delivery, timeout, and a fresh request after the backend restarts. The harness retries transient `ERROR_FILE_NOT_FOUND` and `ERROR_PIPE_BUSY` during startup. In the DLL, each new normal lookup can submit another request after a connection failure.

For live TSF testing, use the probe DLL and its bundled dictionary on a test installation. Start the backend in a Developer PowerShell and collect `Jyutping IPC probe:` lines from `OutputDebugStringW` or the host's Jyutping log. The optional last argument inserts a delay in milliseconds before querying the dictionary.

```powershell
& experiments/ipc-probe/target/debug/jyutping-ipc-probe.exe Jyutping/Resources/ime.sqlite3 75
```

Record the host, process architecture, package and AppContainer status, integrity level, and TSF activation mode. Run in classic Win32, packaged desktop Notepad, and a supported AppContainer TSF host. For each, try a cold first lookup with the backend stopped, steady typing, fast typing, Escape or focus loss before a reply, Space and number selection, and stopping and restarting the backend mid-composition. Compare `local_lookup_us` and `input_to_lookup_us` with `submit_us`, `queue_us`, `worker_us`, and `dispatch_us`. `current=0` indicates that a reply arrived after the client revision changed. Selection currently exercises the synchronous path. The out-of-process candidate selection and raw-input fallback checks still need a client that uses the replies.

## Initial results

The Debug ARM64 probe DLL built. The Windows ARM64 smoke test passed with a native ARM64 Rust listener and both x86 and ARM64 smoke clients. In one native ARM64 run, successful undelayed requests spent 0.85–2.42 ms on the worker and 0.04–0.06 ms from worker completion to owner-thread dispatch. The deliberately delayed request arrived after invalidation. A 1000 ms server delay produced a client timeout after about 254 ms. Some first attempts returned Win32 error 2 just after the backend printed its startup line. A bounded retry 50 ms later found the listener. These console measurements establish an initial reference for host tests.

## Live tests on Windows 11 ARM64

| Host | Observation |
| --- | --- |
| Native Win32 RichEdit test host | TSF activated the Debug DLL. With the backend stopped, the activation probe reported Win32 error 2 on the owning thread. With the backend running and a 25 ms artificial delay, the candidate arrived on the owning thread. One successful run measured 0.055 ms to connect, 28.3 ms on the worker, and 29.7 ms to dispatch the reply on the TSF thread. A first-key synchronous lookup was measured separately at 3.8 ms. |
| Packaged ARM64 Notepad | The package declares `Windows.FullTrustApplication`. A candidate arrived on the owning TSF thread with the same 25 ms delay. The run measured 0.124 ms to connect, 27.1 ms on the worker, and 34.9 ms to dispatch. |
| Standalone native ARM64 AppContainer | The child verified `TokenIsAppContainer`. The same pipe client reached `CreateFileW` and received Win32 error 5 while a desktop control received a candidate from the same backend. Over `ncalrpc`, both desktop and AppContainer callers received `香港` with `hoeng1 gong2` from the Rust dictionary lookup. |
| Packaged ARM64 Clock | Focusing the unsaved Timer name field activated the Debug DLL in `Time.exe`. The activation reply arrived on that thread after a focus change and was marked stale. Typing `hoenggong` followed by Space committed `香港` through the in-process engine. A parallel RPC candidate took 2.2 ms on the worker and waited 275.3 ms for dispatch while the host processed input. By dispatch, the reply's revision was stale. The synchronous lookups in that run ranged from 1.2 to 27.7 ms. |

Local RPC also delivered candidates during activation in native RichEdit and packaged Notepad. With the listener stopped, RichEdit received RPC error 1722 on its TSF thread and continued running. The standalone comparison uses a temporary AppContainer profile with no capabilities. The local RPC server links the Rust dictionary probe. Its endpoint and interface grant access to the owner and, when configured, one specific AppContainer SID. The Clock test used the SID for `Microsoft.WindowsAlarms_8wekyb3d8bbwe`. Production endpoint authorization, server activation, and selection from a remote reply still need validation.

An emulated x64 standalone client received the candidate from the ARM64 RPC server. The smoke runs also checked a missing listener, stale reply delivery, and a fresh request after restart. The emulated TSF DLL path and x64 Windows still need coverage.

## Build the AppContainer comparison

Run these commands in an ARM64-targeting Visual Studio Developer PowerShell. For the pipe test, start the Rust pipe backend first. `AppContainerPipe.exe` expects an access denial and `AppContainerRpc.exe` expects a candidate. Run the executables with normal privilege from a directory the temporary AppContainer can execute from. The test used a temporary directory under Program Files and removed it afterward.

```powershell
cargo build --offline --manifest-path experiments/ipc-probe/Cargo.toml --features rpc-probe
midl /nologo /env arm64 /prefix client Client_ /prefix server Server_ /out experiments/ipc-probe/target experiments/ipc-probe/appcontainer/ProbeRpc.idl
Push-Location experiments/ipc-probe/target
try {
    cl /nologo /MT /std:c++latest /EHsc /DUNICODE /D_UNICODE /DJYUTPING_IPC_PROBE /FeAppContainerPipe.exe ../appcontainer/AppContainer.cpp /link user32.lib advapi32.lib userenv.lib
    cl /nologo /MD /std:c++latest /EHsc /DUNICODE /D_UNICODE /DJYUTPING_IPC_PROBE /DPROBE_RPC /I. /FeAppContainerRpc.exe ../appcontainer/AppContainer.cpp ../appcontainer/RpcComparison.cpp ../appcontainer/RpcProbeClient.cpp ProbeRpc_c.c ProbeRpc_s.c /link debug/jyutping_ipc_probe.lib user32.lib advapi32.lib userenv.lib rpcrt4.lib ntdll.lib ws2_32.lib dbghelp.lib synchronization.lib winsqlite3.lib
}
finally {
    Pop-Location
}
```

Pass the full path to `ime.sqlite3` as the only argument to `AppContainerRpc.exe`.

For the live local RPC build, enable both MSBuild properties. Pass the dictionary path to the server in `serve` mode. The optional package family name grants access to that one AppContainer.

```powershell
msbuild Jyutping.sln /p:Configuration=Debug /p:Platform=ARM64 /p:EnableIpcProbe=true /p:IpcProbeTransport=Rpc /m
$dictionary = (Resolve-Path Jyutping/Resources/ime.sqlite3).Path
& experiments/ipc-probe/target/AppContainerRpc.exe serve $dictionary Microsoft.WindowsAlarms_8wekyb3d8bbwe
```

## Build and run the x64 RPC smoke client

Build the ARM64 `AppContainerRpc.exe` first. Use an x64-targeting Visual Studio Developer PowerShell for the client commands. The script starts and stops the ARM64 listener during the test.

```powershell
New-Item -ItemType Directory -Force experiments/ipc-probe/target/rpc-x64 | Out-Null
midl /nologo /env x64 /prefix client Client_ /prefix server Server_ /out experiments/ipc-probe/target/rpc-x64 experiments/ipc-probe/appcontainer/ProbeRpc.idl
Push-Location experiments/ipc-probe/target/rpc-x64
try {
    cl /nologo /MT /std:c++latest /EHsc /DUNICODE /D_UNICODE /DJYUTPING_IPC_PROBE /DJYUTPING_RPC_PROBE /I. /FeRpcSmoke.exe ../../Smoke.cpp ../../appcontainer/RpcProbeClient.cpp ProbeRpc_c.c /link user32.lib advapi32.lib
}
finally {
    Pop-Location
}
pwsh -NoProfile -File experiments/ipc-probe/Test-Smoke.ps1 -Rpc
```

The reference revision for later Mac behavior comparisons is `01d20107753929189d758016a5ef0c86aa6a6789` in `apple-jyutping`.
