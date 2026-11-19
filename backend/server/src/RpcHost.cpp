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
extern "C" uint32_t JyutpingSuggestCandidates(const unsigned char* input, uint32_t count, uint32_t mode, uint32_t triggerKey, uint32_t standard,
    uint32_t form, JyutpingEngineCandidate* candidates, uint32_t limit, uint32_t* candidateCount, uint32_t* hasMore, uint64_t* memoryGeneration);
extern "C" bool JyutpingMemoryEnabled();
extern "C" bool JyutpingMemoryTransferEnabled();
extern "C" uint32_t JyutpingMutateMemory(uint32_t operation, uint64_t generation, const unsigned char* text, uint32_t textLength,
    const unsigned char* romanization, uint32_t romanizationLength);
extern "C" bool JyutpingSettingsEnabled();
extern "C" uint32_t JyutpingGetSettings(uint64_t knownRevision, uint64_t* revision, uint32_t* values, uint32_t count);
extern "C" uint32_t JyutpingSetSetting(uint32_t field, uint32_t value, uint64_t* revision, uint32_t* values, uint32_t count);

static_assert(JyutpingCandidateCapacity == 256 && sizeof(JyutpingEngineCandidate) == 1308 && alignof(JyutpingEngineCandidate) == 4);

namespace {

constexpr unsigned int MaxCalls = 4;
constexpr unsigned int MaxRpcBytes = 1024;

JyutpingRpcLocal::Identity serverIdentity;
unsigned char instance[16];
HANDLE shutdownEvent = nullptr;

std::wstring StopEventName(const std::wstring& endpoint)
{
    return L"Local\\" + endpoint + L".Stop";
}

unsigned long ValidateIdentity(const JyutpingRequestIdentity& identity)
{
    if (!std::equal(std::begin(instance), std::end(instance), identity.instance))
    {
        return JyutpingStaleInstance;
    }
    if (!identity.context || !identity.generation || !identity.request ||
        std::all_of(std::begin(identity.client), std::end(identity.client), [](unsigned char byte) { return byte == 0; }))
    {
        return JyutpingInvalidRequest;
    }
    return JyutpingOk;
}

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

    shutdownEvent = CreateEventW(nullptr, TRUE, FALSE, StopEventName(endpoint).c_str());
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
    status = RpcServerRegisterIf3(Server_JyutpingRpc_v1_1_s_ifspec, nullptr, nullptr, flags, MaxCalls, MaxRpcBytes, Authorize, descriptor.get());
    if (status != RPC_S_OK)
    {
        return status;
    }
    status = RpcServerListen(1, MaxCalls, TRUE);
    if (status != RPC_S_OK)
    {
        RpcServerUnregisterIf(Server_JyutpingRpc_v1_1_s_ifspec, nullptr, TRUE);
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
    const auto unregistered = RpcServerUnregisterIf(Server_JyutpingRpc_v1_1_s_ifspec, nullptr, TRUE);
    SetConsoleCtrlHandler(OnConsoleControl, FALSE);
    CloseHandle(shutdownEvent);
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
    if (request->minimumVersion > JyutpingEngineProtocolVersion)
    {
        reply->status = JyutpingUnsupportedVersion;
        return;
    }
    reply->version = (std::min)(request->maximumVersion, static_cast<unsigned long>(JyutpingEngineProtocolVersion));
    reply->capabilities = reply->version == JyutpingEngineProtocolVersion ? JyutpingExactSpell | JyutpingEngineCandidates : JyutpingExactSpell;
    if (reply->version == JyutpingEngineProtocolVersion && JyutpingMemoryEnabled())
    {
        reply->capabilities |= JyutpingInputMemory;
        if (JyutpingMemoryTransferEnabled())
        {
            reply->capabilities |= JyutpingMemoryTransfer;
        }
    }
    if (reply->version == JyutpingEngineProtocolVersion && JyutpingSettingsEnabled())
    {
        reply->capabilities |= JyutpingSettings;
    }
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
    reply->status = ValidateIdentity(request->identity);
    if (reply->status != JyutpingOk)
    {
        return;
    }
    if (!request->inputCount || request->inputCount > JyutpingSpellingCapacity ||
        std::any_of(request->spelling + request->inputCount, std::end(request->spelling), [](unsigned char byte) { return byte != 0; }))
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

extern "C" void Server_SuggestCandidates(handle_t, const JyutpingEngineRequest* request, JyutpingEngineReply* reply)
{
    *reply = {};
    reply->identity = request->identity;
    if (request->version != JyutpingEngineProtocolVersion)
    {
        reply->status = JyutpingUnsupportedVersion;
        return;
    }
    reply->status = ValidateIdentity(request->identity);
    if (reply->status != JyutpingOk)
    {
        return;
    }
    if (!request->inputCount || request->inputCount > JyutpingEngineInputCapacity ||
        !request->resultLimit || request->resultLimit > JyutpingEngineResultCapacity ||
        std::any_of(request->input + request->inputCount, std::end(request->input), [](unsigned char byte) { return byte != 0; }))
    {
        reply->status = JyutpingInvalidRequest;
        return;
    }

    uint32_t candidateCount = 0;
    uint32_t hasMore = 0;
    uint64_t memoryGeneration = 0;
    reply->status = JyutpingSuggestCandidates(request->input, request->inputCount, request->mode, request->triggerKey, request->characterStandard,
        request->romanizationForm, reply->candidates, request->resultLimit, &candidateCount, &hasMore, &memoryGeneration);
    if (reply->status == JyutpingOk)
    {
        reply->candidateCount = candidateCount;
        reply->hasMore = hasMore;
        reply->memoryGeneration = memoryGeneration;
    }
    else
    {
        ZeroMemory(reply->candidates, sizeof(reply->candidates));
    }
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

extern "C" uint32_t StopJyutpingRpcServer()
{
    try
    {
        JyutpingRpcLocal::Identity identity;
        DWORD status = identity.ReadProcess();
        if (status != ERROR_SUCCESS)
        {
            return status;
        }
        std::wstring logonSid;
        status = JyutpingRpcLocal::SidString(identity.LogonSid(), logonSid);
        if (status != ERROR_SUCCESS)
        {
            return status;
        }
        const auto eventName = StopEventName(JyutpingRpcLocal::Endpoint(identity.session, logonSid));
        std::unique_ptr<void, decltype(&CloseHandle)> event(OpenEventW(EVENT_MODIFY_STATE, FALSE, eventName.c_str()), CloseHandle);
        if (!event)
        {
            status = GetLastError();
            return status == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : status;
        }
        return SetEvent(event.get()) ? ERROR_SUCCESS : GetLastError();
    }
    catch (const std::bad_alloc&)
    {
        return ERROR_NOT_ENOUGH_MEMORY;
    }
}

extern "C" void Server_MutateMemory(handle_t, const JyutpingMemoryRequest* request, JyutpingMemoryReply* reply)
{
    *reply = {};
    reply->identity = request->identity;
    if (request->version != JyutpingEngineProtocolVersion)
    {
        reply->status = JyutpingUnsupportedVersion;
        return;
    }
    reply->status = ValidateIdentity(request->identity);
    if (reply->status != JyutpingOk)
    {
        return;
    }
    if (!JyutpingMemoryEnabled())
    {
        reply->status = JyutpingCapabilityUnavailable;
        return;
    }
    if (request->operation == JyutpingClearMemory)
    {
        if (request->memoryGeneration || request->originalTextLength || request->romanizationLength)
        {
            reply->status = JyutpingInvalidRequest;
            return;
        }
    }
    else if ((request->operation != JyutpingRemember && request->operation != JyutpingForget) || !request->memoryGeneration ||
        !request->originalTextLength || request->originalTextLength > JyutpingCandidateCapacity ||
        !request->romanizationLength || request->romanizationLength > JyutpingCandidateCapacity)
    {
        reply->status = JyutpingInvalidRequest;
        return;
    }
    reply->status = JyutpingMutateMemory(request->operation, request->memoryGeneration, request->originalText, request->originalTextLength,
        request->romanization, request->romanizationLength);
}

extern "C" void Server_GetSettings(handle_t, const JyutpingSettingsRequest* request, JyutpingSettingsReply* reply)
{
    *reply = {};
    reply->identity = request->identity;
    reply->status = request->version == JyutpingEngineProtocolVersion ? ValidateIdentity(request->identity) : JyutpingUnsupportedVersion;
    if (reply->status == JyutpingOk)
    {
        uint32_t values[JyutpingSettingCount]{};
        uint64_t revision = 0;
        reply->status = JyutpingGetSettings(request->knownRevision, &revision, values, JyutpingSettingCount);
        reply->settingsRevision = revision;
        std::copy_n(values, JyutpingSettingCount, reply->values);
    }
}

extern "C" void Server_SetSetting(handle_t, const JyutpingSetSettingRequest* request, JyutpingSettingsReply* reply)
{
    *reply = {};
    reply->identity = request->identity;
    reply->status = request->version == JyutpingEngineProtocolVersion ? ValidateIdentity(request->identity) : JyutpingUnsupportedVersion;
    if (reply->status == JyutpingOk)
    {
        uint32_t values[JyutpingSettingCount]{};
        uint64_t revision = 0;
        reply->status = JyutpingSetSetting(request->field, request->value, &revision, values, JyutpingSettingCount);
        reply->settingsRevision = revision;
        std::copy_n(values, JyutpingSettingCount, reply->values);
    }
}
