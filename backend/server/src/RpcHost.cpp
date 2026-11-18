#include "LocalRpcIdentity.h"
#include "JyutpingRpc.h"

#include <Bcrypt.h>
#include <Userenv.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iterator>

extern "C" int32_t JyutpingSuggestExact(const unsigned char* spelling, uint32_t inputCount, unsigned char* text,
    unsigned char* romanization, uint32_t capacity, uint32_t* textLength, uint32_t* romanizationLength);

namespace {

constexpr unsigned int MaxCalls = 4;
constexpr unsigned int MaxRpcBytes = 1024;

JyutpingRpcLocal::Identity serverIdentity;
unsigned char instance[16];
HANDLE shutdownEvent = nullptr;

BOOL WINAPI OnConsoleControl(DWORD event)
{
    return (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT) && SetEvent(shutdownEvent);
}

int RPC_ENTRY DenyRemoteManagement(RPC_BINDING_HANDLE, unsigned long, RPC_STATUS* status)
{
    *status = RPC_S_ACCESS_DENIED;
    return FALSE;
}

RPC_STATUS RPC_ENTRY Authorize(RPC_IF_HANDLE, void* context)
{
    const auto status = RpcImpersonateClient(context);
    if (status != RPC_S_OK)
    {
        return RPC_S_ACCESS_DENIED;
    }

    HANDLE rawToken = nullptr;
    const BOOL haveToken = OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &rawToken);
    const auto reverted = RpcRevertToSelfEx(context);
    std::unique_ptr<void, decltype(&CloseHandle)> token(rawToken, CloseHandle);
    JyutpingRpcLocal::Identity caller;
    if (!haveToken || reverted != RPC_S_OK || caller.Read(token.get()) != ERROR_SUCCESS ||
        caller.session != serverIdentity.session || !EqualSid(caller.UserSid(), serverIdentity.UserSid()) ||
        !EqualSid(caller.LogonSid(), serverIdentity.LogonSid()))
    {
        return RPC_S_ACCESS_DENIED;
    }
    return RPC_S_OK;
}

DWORD Serve(const wchar_t* appContainer)
{
    DWORD status = serverIdentity.ReadProcess();
    if (status != ERROR_SUCCESS)
    {
        return status;
    }
    std::wstring logonSid;
    status = JyutpingRpcLocal::SidString(serverIdentity.LogonSid(), logonSid);
    if (status != ERROR_SUCCESS)
    {
        return status;
    }

    std::wstring security = L"D:P(A;;GA;;;" + logonSid + L")";
    if (appContainer)
    {
        PSID rawSid = nullptr;
        const HRESULT derived = DeriveAppContainerSidFromAppContainerName(appContainer, &rawSid);
        if (FAILED(derived))
        {
            return derived;
        }
        std::unique_ptr<void, decltype(&FreeSid)> sid(rawSid, FreeSid);
        std::wstring packageSid;
        status = JyutpingRpcLocal::SidString(sid.get(), packageSid);
        if (status != ERROR_SUCCESS)
        {
            return status;
        }
        security += L"(A;;GA;;;" + packageSid + L")S:(ML;;NW;;;LW)";
    }

    PSECURITY_DESCRIPTOR rawDescriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(security.c_str(), SDDL_REVISION_1, &rawDescriptor, nullptr))
    {
        return GetLastError();
    }
    std::unique_ptr<void, decltype(&LocalFree)> descriptor(rawDescriptor, LocalFree);
    const auto endpoint = JyutpingRpcLocal::Endpoint(serverIdentity.session, logonSid);
    using JyutpingRpcLocal::RpcString;
    status = RpcServerUseProtseqEpW(RpcString(L"ncalrpc"), RPC_C_PROTSEQ_MAX_REQS_DEFAULT, RpcString(endpoint.c_str()), descriptor.get());
    if (status == RPC_S_DUPLICATE_ENDPOINT)
    {
        puts("JyutpingServer is already running for this logon.");
        return ERROR_SUCCESS;
    }
    if (status != RPC_S_OK)
    {
        return status;
    }
    if (!BCRYPT_SUCCESS(BCryptGenRandom(nullptr, instance, sizeof(instance), BCRYPT_USE_SYSTEM_PREFERRED_RNG)))
    {
        return ERROR_GEN_FAILURE;
    }

    shutdownEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!shutdownEvent || !SetConsoleCtrlHandler(OnConsoleControl, TRUE))
    {
        return GetLastError();
    }
    status = RpcMgmtSetAuthorizationFn(DenyRemoteManagement);
    if (status != RPC_S_OK)
    {
        return status;
    }
    status = RpcServerRegisterAuthInfoW(nullptr, RPC_C_AUTHN_WINNT, nullptr, nullptr);
    if (status != RPC_S_OK)
    {
        return status;
    }
    constexpr unsigned int flags = RPC_IF_ALLOW_LOCAL_ONLY | RPC_IF_ALLOW_SECURE_ONLY | RPC_IF_SEC_NO_CACHE;
    status = RpcServerRegisterIf3(Server_JyutpingRpc_v1_0_s_ifspec, nullptr, nullptr, flags, MaxCalls, MaxRpcBytes, Authorize, descriptor.get());
    if (status != RPC_S_OK)
    {
        return status;
    }
    status = RpcServerListen(1, MaxCalls, TRUE);
    if (status != RPC_S_OK)
    {
        RpcServerUnregisterIf(Server_JyutpingRpc_v1_0_s_ifspec, nullptr, TRUE);
        return status;
    }

    puts("Listening on local RPC");
    fflush(stdout);
    if (WaitForSingleObject(shutdownEvent, INFINITE) != WAIT_OBJECT_0)
    {
        return GetLastError();
    }
    const auto stopped = RpcMgmtStopServerListening(nullptr);
    const auto drained = stopped == RPC_S_OK ? RpcMgmtWaitServerListen() : stopped;
    const auto unregistered = RpcServerUnregisterIf(Server_JyutpingRpc_v1_0_s_ifspec, nullptr, TRUE);
    SetConsoleCtrlHandler(OnConsoleControl, FALSE);
    return drained != RPC_S_OK ? drained : unregistered;
}

} // namespace

extern "C" void* __RPC_USER midl_user_allocate(size_t size) { return malloc(size); }
extern "C" void __RPC_USER midl_user_free(void* memory) { free(memory); }

extern "C" void Server_Hello(handle_t, const JyutpingHelloRequest* request, JyutpingHelloReply* reply)
{
    *reply = {};
    if (!request->minimumVersion || request->minimumVersion > request->maximumVersion)
    {
        reply->status = JyutpingInvalidRequest;
        return;
    }
    if (request->minimumVersion > JyutpingProtocolVersion || request->maximumVersion < JyutpingProtocolVersion)
    {
        reply->status = JyutpingUnsupportedVersion;
        return;
    }
    reply->version = JyutpingProtocolVersion;
    reply->capabilities = JyutpingExactSpell;
    std::copy_n(instance, sizeof(instance), reply->instance);
}

extern "C" void Server_Suggest(handle_t, const JyutpingSuggestRequest* request, JyutpingSuggestReply* reply)
{
    *reply = {};
    reply->identity = request->identity;
    if (request->version != JyutpingProtocolVersion)
    {
        reply->status = JyutpingUnsupportedVersion;
        return;
    }
    if (!std::equal(std::begin(instance), std::end(instance), request->identity.instance))
    {
        reply->status = JyutpingStaleInstance;
        return;
    }
    if (!request->inputCount || request->inputCount > JyutpingSpellingCapacity ||
        !request->identity.context || !request->identity.generation || !request->identity.request ||
        std::any_of(request->spelling + request->inputCount, std::end(request->spelling), [](unsigned char byte) { return byte != 0; }) ||
        std::all_of(std::begin(request->identity.client), std::end(request->identity.client), [](unsigned char byte) { return byte == 0; }))
    {
        reply->status = JyutpingInvalidRequest;
        return;
    }

    uint32_t textLength = 0;
    uint32_t romanizationLength = 0;
    const int32_t result = JyutpingSuggestExact(request->spelling, request->inputCount, reply->text, reply->romanization,
        JyutpingCandidateCapacity, &textLength, &romanizationLength);
    if (result != 1)
    {
        ZeroMemory(reply->text, sizeof(reply->text));
        ZeroMemory(reply->romanization, sizeof(reply->romanization));
        reply->status = result == 0 ? JyutpingEmpty : result == -2 ? JyutpingInvalidRequest : JyutpingStorageFailure;
        return;
    }
    reply->consumedInput = request->inputCount;
    reply->textLength = textLength;
    reply->romanizationLength = romanizationLength;
}

extern "C" uint32_t RunJyutpingRpcServer(const wchar_t* appContainer)
{
    try
    {
        return Serve(appContainer);
    }
    catch (const std::bad_alloc&)
    {
        return ERROR_NOT_ENOUGH_MEMORY;
    }
}
