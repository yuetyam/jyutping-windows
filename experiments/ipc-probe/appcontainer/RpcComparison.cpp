#include <Windows.h>
#include <Rpc.h>
#include <Sddl.h>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

#include "ProbeRpc.h"

namespace {
std::wstring_view dictionaryPath;
}
extern "C" int JyutpingProbeSuggest(const wchar_t* path, uint32_t pathLength, unsigned char* output);

extern "C" long Server_Suggest(handle_t binding, unsigned char candidate[256])
{
    ZeroMemory(candidate, 256);
    const int length = JyutpingProbeSuggest(dictionaryPath.data(), static_cast<uint32_t>(dictionaryPath.size()), candidate);
    RPC_CALL_ATTRIBUTES_V2_W attributes{};
    attributes.Version = 2;
    attributes.Flags = RPC_QUERY_CLIENT_PID;
    if (RpcServerInqCallAttributesW(binding, &attributes) == RPC_S_OK)
    {
        printf("RPC lookup client_pid=%lu result=%d\n", HandleToULong(attributes.ClientPID), length);
        fflush(stdout);
    }
    return length;
}

static RPC_WSTR RpcString(const wchar_t* text) { return reinterpret_cast<RPC_WSTR>(const_cast<wchar_t*>(text)); }

unsigned long StartRpc(const wchar_t* endpoint, PSID containerSid, PSECURITY_DESCRIPTOR* descriptor, const wchar_t* path)
{
    dictionaryPath = path;
    std::wstring security = L"D:P(A;;GA;;;OW)";
    if (containerSid)
    {
        LPWSTR text = nullptr;
        if (!ConvertSidToStringSidW(containerSid, &text)) { return GetLastError(); }
        security += L"(A;;GA;;;" + std::wstring(text) + L")S:(ML;;NW;;;LW)";
        LocalFree(text);
    }
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(security.c_str(), SDDL_REVISION_1, descriptor, nullptr)) { return GetLastError(); }
    auto status = RpcServerUseProtseqEpW(RpcString(L"ncalrpc"), RPC_C_PROTSEQ_MAX_REQS_DEFAULT, RpcString(endpoint), *descriptor);
    if (status != RPC_S_OK) { return status; }
    status = RpcServerRegisterIf3(Server_ProbeRpc_v1_0_s_ifspec, nullptr, nullptr, RPC_IF_ALLOW_LOCAL_ONLY, 4, 256, nullptr, *descriptor);
    if (status != RPC_S_OK) { return status; }
    return RpcServerListen(1, 4, TRUE);
}
