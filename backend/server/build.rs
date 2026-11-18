use std::env;
use std::path::PathBuf;
use std::process::Command;

fn run(command: &mut Command) {
        let status = command.status().unwrap_or_else(|error| panic!("could not run {command:?}: {error}"));
        assert!(status.success(), "{command:?} failed with {status}");
}

fn main() {
        let target = env::var("TARGET").expect("Cargo target");
        let (midl_arch, tool_arch) = match target.as_str() {
                "aarch64-pc-windows-msvc" => ("arm64", "arm64"),
                "x86_64-pc-windows-msvc" => ("x64", "x64"),
                _ => panic!("JyutpingServer supports ARM64 and x64 Windows MSVC targets"),
        };
        assert_eq!(env::var("VSCMD_ARG_TGT_ARCH").as_deref(), Ok(tool_arch), "Use an MSVC Developer PowerShell targeting {tool_arch}");

        let out = PathBuf::from(env::var_os("OUT_DIR").expect("Cargo output directory"));
        let protocol = PathBuf::from("../../Shared/Protocol");
        let idl = protocol.join("JyutpingRpc.idl");
        let identity = protocol.join("LocalRpcIdentity.h");
        let host = PathBuf::from("src/RpcHost.cpp");

        for input in [&idl, &identity, &host] {
                println!("cargo:rerun-if-changed={}", input.display());
        }
        println!("cargo:rerun-if-env-changed=VSCMD_ARG_TGT_ARCH");

        run(Command::new("midl.exe").args(["/nologo", "/robust", "/env", midl_arch, "/prefix", "client", "Client_", "/prefix", "server", "Server_", "/out"]).arg(&out).arg(&idl));
        let runtime = if env::var("CARGO_CFG_TARGET_FEATURE").unwrap_or_default().split(',').any(|feature| feature == "crt-static") {
                "/MT"
        } else {
                "/MD"
        };
        run(Command::new("cl.exe").args(["/nologo", "/c", "/O2", runtime, "/W4", "/EHsc", "/std:c++20"]).arg("/I").arg(&out).arg("/I").arg(&protocol)
                .arg(format!("/Fo{}", out.join("RpcHost.obj").display())).arg(&host));
        run(Command::new("cl.exe").args(["/nologo", "/c", "/O2", runtime])
                .arg(format!("/Fo{}", out.join("JyutpingRpc_s.obj").display())).arg(out.join("JyutpingRpc_s.c")));
        run(Command::new("lib.exe").arg("/nologo").arg(format!("/OUT:{}", out.join("jyutping_rpc.lib").display()))
                .arg(out.join("RpcHost.obj")).arg(out.join("JyutpingRpc_s.obj")));

        println!("cargo:rustc-link-search=native={}", out.display());
        for library in ["static=jyutping_rpc", "rpcrt4", "advapi32", "userenv", "bcrypt"] {
                println!("cargo:rustc-link-lib={library}");
        }
}
