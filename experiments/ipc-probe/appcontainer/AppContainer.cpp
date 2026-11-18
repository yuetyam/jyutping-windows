#include <Windows.h>
#include <UserEnv.h>
#include <Rpc.h>
#include <cstdio>
#include <vector>

#define JYUTPING_IPC_PROBE_SMOKE
#include "../../../Jyutping/IpcProbe.cpp"

HINSTANCE Global::dllInstanceHandle = GetModuleHandleW(nullptr);

#ifdef PROBE_RPC
#include "RpcProbeClient.h"
unsigned long StartRpc(const wchar_t* endpoint, PSID containerSid, PSECURITY_DESCRIPTOR* descriptor, const wchar_t* path);

int Serve(const wchar_t* dictionaryPath, const wchar_t* packageFamily)
{
    ProbeIdentity identity;
    Reply reply = ReadProbeIdentity(identity);
    if (reply.error != ERROR_SUCCESS)
    {
        return 1;
    }
    const std::wstring endpoint = L"Jyutping.RpcProbe.v0." + identity.suffix;
    PSID rawPackageSid = nullptr;
    if (packageFamily && FAILED(DeriveAppContainerSidFromAppContainerName(packageFamily, &rawPackageSid)))
    {
        return 1;
    }
    std::unique_ptr<void, decltype(&FreeSid)> packageSid(rawPackageSid, FreeSid);
    PSECURITY_DESCRIPTOR rawDescriptor = nullptr;
    const auto status = StartRpc(endpoint.c_str(), packageSid.get(), &rawDescriptor, dictionaryPath);
    std::unique_ptr<void, decltype(&LocalFree)> descriptor(rawDescriptor, LocalFree);
    if (status != RPC_S_OK)
    {
        printf("RPC start error=%lu\n", status);
        return 1;
    }
    puts("Listening on local RPC");
    fflush(stdout);
    return RpcMgmtWaitServerListen() == RPC_S_OK ? 0 : 1;
}
#endif

int TryContainer(const wchar_t* dictionaryPath)
{
    std::wstring name = L"Jyutping.IpcProbe." + std::to_wstring(GetCurrentProcessId());
    PSID sid = nullptr;
    HRESULT status = CreateAppContainerProfile(name.c_str(), name.c_str(), L"Jyutping IPC transport experiment", nullptr, 0, &sid);
    if (FAILED(status)) { printf("AppContainer profile=0x%08lx\n", status); return 1; }
#ifdef PROBE_RPC
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    auto rpcStatus = StartRpc(name.c_str(), sid, &descriptor, dictionaryPath);
    printf("RPC listen=%lu\n", rpcStatus);
    const auto desktopStatus = rpcStatus == RPC_S_OK ? CallRpc(name.c_str()) : rpcStatus;
    printf("RPC desktop=%lu\n", desktopStatus);
#endif

    SIZE_T attributeBytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);
    std::vector<std::byte> attributeStorage(attributeBytes);
    auto attributes = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(attributeStorage.data());
    int result = 1;
    if (InitializeProcThreadAttributeList(attributes, 1, 0, &attributeBytes))
    {
        SECURITY_CAPABILITIES capabilities{sid, nullptr, 0, 0};
        if (UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES, &capabilities, sizeof(capabilities), nullptr, nullptr))
        {
            wchar_t image[MAX_PATH]{};
            GetModuleFileNameW(nullptr, image, ARRAYSIZE(image));
            std::wstring command = L"\"" + std::wstring(image) + L"\" child";
#ifdef PROBE_RPC
            command += L" " + name;
#endif
            STARTUPINFOEXW startup{};
            startup.StartupInfo.cb = sizeof(startup);
            startup.lpAttributeList = attributes;
            PROCESS_INFORMATION child{};
            if (CreateProcessW(image, command.data(), nullptr, nullptr, FALSE, EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW, nullptr, L"C:\\Windows", &startup.StartupInfo, &child))
            {
                DWORD code = 0;
                if (WaitForSingleObject(child.hProcess, 5000) == WAIT_OBJECT_0 && GetExitCodeProcess(child.hProcess, &code))
                {
                    printf("AppContainer child exit=%lu\n", code);
#ifdef PROBE_RPC
                    result = desktopStatus != RPC_S_OK || code != RPC_S_OK;
#else
                    result = code != 1000 + ERROR_ACCESS_DENIED;
#endif
                }
                else
                {
                    TerminateProcess(child.hProcess, 1);
                    WaitForSingleObject(child.hProcess, 1000);
                }
                CloseHandle(child.hThread);
                CloseHandle(child.hProcess);
            }
            else { printf("AppContainer launch error=%lu\n", GetLastError()); }
        }
        DeleteProcThreadAttributeList(attributes);
    }
    FreeSid(sid);
#ifdef PROBE_RPC
    if (rpcStatus == 0)
    {
        RpcMgmtStopServerListening(nullptr);
        RpcMgmtWaitServerListen();
    }
    LocalFree(descriptor);
#endif
    HRESULT removed = DeleteAppContainerProfile(name.c_str());
    if (FAILED(removed)) { printf("AppContainer profile cleanup=0x%08lx\n", removed); }
    return result;
}

int wmain(int argc, wchar_t** argv)
{
#ifdef PROBE_RPC
    if ((argc == 3 || argc == 4) && wcscmp(argv[1], L"serve") == 0)
    {
        return Serve(argv[2], argc == 4 ? argv[3] : nullptr);
    }
#endif
    if (argc < 2 || wcscmp(argv[1], L"child") != 0)
    {
#ifdef PROBE_RPC
        if (argc != 2)
        {
            fputs("usage: AppContainer <ime.sqlite3>\n", stderr);
            return 2;
        }
        return TryContainer(argv[1]);
#else
        return TryContainer(nullptr);
#endif
    }
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) { return 9001; }
    DWORD isContainer = 0;
    DWORD bytes = 0;
    BOOL verified = GetTokenInformation(token, TokenIsAppContainer, &isContainer, sizeof(isContainer), &bytes);
    CloseHandle(token);
    if (!verified || !isContainer) { return 9002; }
#ifdef PROBE_RPC
    if (argc != 3) { return 9003; }
    return static_cast<int>(CallRpc(argv[2]));
#else
    Reply reply = RequestCandidate();
    if (reply.candidate) { return 0; }
    return (wcscmp(reply.stage, L"connect") == 0 ? 1000 : 2000) + reply.error;
#endif
}
