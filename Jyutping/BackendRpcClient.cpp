#include "BackendRpcClient.h"

#include "JyutpingRpc.h"
#include "../Shared/Protocol/LocalRpcIdentity.h"

#include <Bcrypt.h>

#include <algorithm>
#include <cstdlib>
#include <utility>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "rpcrt4.lib")

extern "C" void* __RPC_USER midl_user_allocate(size_t size) { return malloc(size); }
extern "C" void __RPC_USER midl_user_free(void* memory) { free(memory); }

namespace {

DWORD Hello(RPC_BINDING_HANDLE binding, const JyutpingHelloRequest& request, JyutpingHelloReply& reply)
{
    DWORD status = RPC_S_OK;
    RpcTryExcept
    {
        Client_Hello(binding, &request, &reply);
    }
    RpcExcept(RpcExceptionFilter(RpcExceptionCode()))
    {
        status = RpcExceptionCode();
    }
    RpcEndExcept
    return status;
}

DWORD CallSuggest(RPC_BINDING_HANDLE binding, const JyutpingSuggestRequest& request, JyutpingSuggestReply& reply)
{
    DWORD status = RPC_S_OK;
    RpcTryExcept
    {
        Client_Suggest(binding, &request, &reply);
    }
    RpcExcept(RpcExceptionFilter(RpcExceptionCode()))
    {
        status = RpcExceptionCode();
    }
    RpcEndExcept
    return status;
}

DWORD CallSuggestCandidates(RPC_BINDING_HANDLE binding, const JyutpingEngineRequest& request, JyutpingEngineReply& reply)
{
    DWORD status = RPC_S_OK;
    RpcTryExcept
    {
        Client_SuggestCandidates(binding, &request, &reply);
    }
    RpcExcept(RpcExceptionFilter(RpcExceptionCode()))
    {
        status = RpcExceptionCode();
    }
    RpcEndExcept
    return status;
}

DWORD CallMutateMemory(RPC_BINDING_HANDLE binding, const JyutpingMemoryRequest& request, JyutpingMemoryReply& reply)
{
    DWORD status = RPC_S_OK;
    RpcTryExcept
    {
        Client_MutateMemory(binding, &request, &reply);
    }
    RpcExcept(RpcExceptionFilter(RpcExceptionCode()))
    {
        status = RpcExceptionCode();
    }
    RpcEndExcept
    return status;
}

DWORD CallGetSettings(RPC_BINDING_HANDLE binding, const JyutpingSettingsRequest& request, JyutpingSettingsReply& reply)
{
    DWORD status = RPC_S_OK;
    RpcTryExcept
    {
        Client_GetSettings(binding, &request, &reply);
    }
    RpcExcept(RpcExceptionFilter(RpcExceptionCode()))
    {
        status = RpcExceptionCode();
    }
    RpcEndExcept
    return status;
}

DWORD CallSetSetting(RPC_BINDING_HANDLE binding, const JyutpingSetSettingRequest& request, JyutpingSettingsReply& reply)
{
    DWORD status = RPC_S_OK;
    RpcTryExcept
    {
        Client_SetSetting(binding, &request, &reply);
    }
    RpcExcept(RpcExceptionFilter(RpcExceptionCode()))
    {
        status = RpcExceptionCode();
    }
    RpcEndExcept
    return status;
}

bool Matches(const JyutpingRequestIdentity& left, const JyutpingRequestIdentity& right)
{
    return std::equal(std::begin(left.instance), std::end(left.instance), right.instance) &&
        std::equal(std::begin(left.client), std::end(left.client), right.client) &&
        left.context == right.context && left.generation == right.generation && left.revision == right.revision && left.request == right.request;
}

bool Decode(const unsigned char (&bytes)[JyutpingCandidateCapacity], unsigned long length, std::wstring& result)
{
    if (!length || length > sizeof(bytes) || std::find(bytes, bytes + length, 0) != bytes + length)
    {
        return false;
    }
    wchar_t decoded[JyutpingCandidateCapacity];
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, reinterpret_cast<const char*>(bytes), static_cast<int>(length), decoded, ARRAYSIZE(decoded));
    if (!size)
    {
        return false;
    }
    result.assign(decoded, size);
    return true;
}

unsigned long Encode(std::wstring_view text, unsigned char (&bytes)[JyutpingCandidateCapacity])
{
    if (text.empty() || text.size() > JyutpingCandidateCapacity || text.find(L'\0') != std::wstring_view::npos)
    {
        return 0;
    }
    return static_cast<unsigned long>(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
        reinterpret_cast<char*>(bytes), ARRAYSIZE(bytes), nullptr, nullptr));
}

} // namespace

BackendRpcClient::~BackendRpcClient()
{
    Disconnect();
}

void BackendRpcClient::Disconnect()
{
    if (_binding)
    {
        RpcBindingFree(&_binding);
    }
    _capabilities = 0;
}

DWORD BackendRpcClient::Connect()
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
    if (std::all_of(_client.begin(), _client.end(), [](unsigned char value) { return value == 0; }) &&
        !BCRYPT_SUCCESS(BCryptGenRandom(nullptr, _client.data(), static_cast<ULONG>(_client.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG)))
    {
        return ERROR_GEN_FAILURE;
    }

    using JyutpingRpcLocal::RpcString;
    const auto endpoint = JyutpingRpcLocal::Endpoint(identity.session, logonSid);
    RPC_WSTR bindingString = nullptr;
    status = RpcStringBindingComposeW(nullptr, RpcString(L"ncalrpc"), nullptr, RpcString(endpoint.c_str()), nullptr, &bindingString);
    if (status != RPC_S_OK)
    {
        return status;
    }
    status = RpcBindingFromStringBindingW(bindingString, &_binding);
    RpcStringFreeW(&bindingString);
    if (status != RPC_S_OK)
    {
        return status;
    }
    // ncalrpc can verify the server token's user SID before sending application data.
    RPC_SECURITY_QOS_V3_W qos{};
    qos.Version = RPC_C_SECURITY_QOS_VERSION_3;
    qos.Capabilities = RPC_C_QOS_CAPABILITIES_MUTUAL_AUTH;
    qos.IdentityTracking = RPC_C_QOS_IDENTITY_STATIC;
    qos.ImpersonationType = RPC_C_IMP_LEVEL_IDENTIFY;
    qos.Sid = identity.UserSid();
    status = RpcBindingSetAuthInfoExW(_binding, nullptr, RPC_C_AUTHN_LEVEL_PKT_PRIVACY, RPC_C_AUTHN_WINNT, nullptr,
        RPC_C_AUTHZ_NONE, reinterpret_cast<RPC_SECURITY_QOS*>(&qos));
    if (status == RPC_S_OK)
    {
        status = RpcBindingSetOption(_binding, RPC_C_OPT_CALL_TIMEOUT, 250);
    }

    JyutpingHelloReply reply{};
    if (status == RPC_S_OK)
    {
        status = Hello(_binding, {JyutpingProtocolVersion, JyutpingEngineProtocolVersion}, reply);
    }
    if (status == RPC_S_OK && (reply.status != JyutpingOk || reply.version < JyutpingProtocolVersion || reply.version > JyutpingEngineProtocolVersion ||
        !(reply.capabilities & JyutpingExactSpell) || (reply.version == JyutpingEngineProtocolVersion && !(reply.capabilities & JyutpingEngineCandidates)) ||
        std::all_of(std::begin(reply.instance), std::end(reply.instance), [](unsigned char byte) { return byte == 0; })))
    {
        status = ERROR_REVISION_MISMATCH;
    }
    if (status != RPC_S_OK)
    {
        Disconnect();
        return status;
    }
    std::copy_n(reply.instance, _instance.size(), _instance.begin());
    _capabilities = reply.capabilities;
    return ERROR_SUCCESS;
}

DWORD BackendRpcClient::Suggest(std::wstring_view spelling, uint64_t context, uint64_t generation, uint64_t revision, BackendRpcCandidate& candidate)
{
    candidate = {};
    if (spelling.empty() || spelling.size() > JyutpingSpellingCapacity || spelling.find_first_not_of(L"abcdefghijklmnopqrstuvwxyz") != std::wstring_view::npos)
    {
        return ERROR_INVALID_PARAMETER;
    }

    if (!_binding)
    {
        const DWORD status = Connect();
        if (status != ERROR_SUCCESS)
        {
            return status;
        }
    }

    JyutpingSuggestRequest request{};
    request.version = JyutpingProtocolVersion;
    std::copy(_instance.begin(), _instance.end(), request.identity.instance);
    std::copy(_client.begin(), _client.end(), request.identity.client);
    request.identity.context = context;
    request.identity.generation = generation;
    request.identity.revision = revision;
    request.identity.request = ++_request;
    request.inputCount = static_cast<unsigned long>(spelling.size());
    std::transform(spelling.begin(), spelling.end(), request.spelling, [](wchar_t letter) { return static_cast<unsigned char>(letter); });

    JyutpingSuggestReply reply{};
    const DWORD status = CallSuggest(_binding, request, reply);
    if (status != RPC_S_OK)
    {
        Disconnect();
        return status;
    }
    if (!Matches(request.identity, reply.identity))
    {
        Disconnect();
        return ERROR_INVALID_DATA;
    }

    switch (reply.status)
    {
    case JyutpingOk:
        if (reply.consumedInput == request.inputCount &&
            Decode(reply.text, reply.textLength, candidate.text) && Decode(reply.romanization, reply.romanizationLength, candidate.romanization))
        {
            return ERROR_SUCCESS;
        }
        candidate = {};
        return ERROR_INVALID_DATA;
    case JyutpingEmpty:
        return !reply.consumedInput && !reply.textLength && !reply.romanizationLength ? ERROR_SUCCESS : ERROR_INVALID_DATA;
    case JyutpingInvalidRequest:
        return ERROR_INVALID_PARAMETER;
    case JyutpingStaleInstance:
        Disconnect();
        return ERROR_RETRY;
    case JyutpingUnsupportedVersion:
        Disconnect();
        return ERROR_REVISION_MISMATCH;
    case JyutpingStorageFailure:
        return ERROR_READ_FAULT;
    default:
        return ERROR_INVALID_DATA;
    }
}

DWORD BackendRpcClient::SuggestCandidates(std::wstring_view input, BackendLookupMode mode, uint32_t standard, uint32_t form, uint32_t limit,
    uint64_t context, uint64_t generation, uint64_t revision, BackendEngineReply& result, wchar_t trigger)
{
    static_assert(static_cast<uint32_t>(BackendLookupMode::Normal) == JyutpingNormal && static_cast<uint32_t>(BackendLookupMode::Structure) == JyutpingStructure);
    static_assert(MaxInputEvents == JyutpingEngineInputCapacity && MaxCandidates == JyutpingEngineResultCapacity);
    result = {};
    if (input.empty() || input.size() > JyutpingEngineInputCapacity || input.find_first_not_of(L"abcdefghijklmnopqrstuvwxyz0123456789'") != std::wstring_view::npos ||
        !limit || limit > JyutpingEngineResultCapacity)
    {
        return ERROR_INVALID_PARAMETER;
    }
    if (!_binding)
    {
        const DWORD status = Connect();
        if (status != ERROR_SUCCESS)
        {
            return status;
        }
    }
    if (!(_capabilities & JyutpingEngineCandidates))
    {
        return ERROR_REVISION_MISMATCH;
    }

    JyutpingEngineRequest request{};
    request.version = JyutpingEngineProtocolVersion;
    std::copy(_instance.begin(), _instance.end(), request.identity.instance);
    std::copy(_client.begin(), _client.end(), request.identity.client);
    request.identity.context = context;
    request.identity.generation = generation;
    request.identity.revision = revision;
    request.identity.request = ++_request;
    request.mode = static_cast<unsigned long>(mode);
    request.triggerKey = trigger;
    request.characterStandard = standard;
    request.romanizationForm = form;
    request.resultLimit = limit;
    request.inputCount = static_cast<unsigned long>(input.size());
    std::transform(input.begin(), input.end(), request.input, [](wchar_t key) { return static_cast<unsigned char>(key); });

    JyutpingEngineReply reply{};
    const DWORD status = CallSuggestCandidates(_binding, request, reply);
    if (status != RPC_S_OK)
    {
        Disconnect();
        return status;
    }
    if (!Matches(request.identity, reply.identity))
    {
        Disconnect();
        return ERROR_INVALID_DATA;
    }
    if (reply.status != JyutpingOk)
    {
        if (reply.candidateCount || reply.hasMore)
        {
            Disconnect();
            return ERROR_INVALID_DATA;
        }
        switch (reply.status)
        {
        case JyutpingEmpty:
            result.memoryEnabled = (_capabilities & JyutpingInputMemory) != 0;
            result.instance = _instance;
            return ERROR_SUCCESS;
        case JyutpingInvalidRequest: return ERROR_INVALID_PARAMETER;
        case JyutpingStaleInstance: Disconnect(); return ERROR_RETRY;
        case JyutpingUnsupportedVersion: Disconnect(); return ERROR_REVISION_MISMATCH;
        case JyutpingStorageFailure: return ERROR_READ_FAULT;
        default: Disconnect(); return ERROR_INVALID_DATA;
        }
    }
    if (!reply.candidateCount || reply.candidateCount > limit || reply.hasMore > 1 || (reply.hasMore && reply.candidateCount != limit))
    {
        Disconnect();
        return ERROR_INVALID_DATA;
    }

    BackendEngineReply decoded;
    decoded.candidates.reserve(reply.candidateCount);
    for (unsigned long index = 0; index < reply.candidateCount; ++index)
    {
        const auto& source = reply.candidates[index];
        BackendEngineCandidate candidate;
        if (source.kind > JyutpingSymbolCandidate || source.consumedInput > request.inputCount ||
            (!source.consumedInput && mode != BackendLookupMode::Stroke) ||
            !Decode(source.text, source.textLength, candidate.text) ||
            !Decode(source.originalText, source.originalTextLength, candidate.originalText) ||
            !Decode(source.romanization, source.romanizationLength, candidate.romanization) ||
            (source.markLength && !Decode(source.mark, source.markLength, candidate.mark)) ||
            (source.commentLength && !Decode(source.comment, source.commentLength, candidate.comment)))
        {
            Disconnect();
            return ERROR_INVALID_DATA;
        }
        candidate.kind = source.kind;
        candidate.consumedInput = source.consumedInput;
        decoded.candidates.push_back(std::move(candidate));
    }
    decoded.hasMore = reply.hasMore != 0;
    decoded.memoryEnabled = (_capabilities & JyutpingInputMemory) != 0;
    decoded.memoryGeneration = reply.memoryGeneration;
    decoded.instance = _instance;
    result = std::move(decoded);
    return ERROR_SUCCESS;
}

DWORD BackendRpcClient::MutateMemory(BackendMemoryOperation operation, std::wstring_view text, std::wstring_view romanization,
    uint64_t context, uint64_t generation, uint64_t revision, const std::array<unsigned char, 16>& expectedInstance, uint64_t memoryGeneration)
{
    static_assert(static_cast<uint32_t>(BackendMemoryOperation::Remember) == JyutpingRemember &&
        static_cast<uint32_t>(BackendMemoryOperation::Forget) == JyutpingForget && static_cast<uint32_t>(BackendMemoryOperation::Clear) == JyutpingClearMemory);
    // An absent binding cannot establish whether this operation belongs to the current server instance.
    if (!_binding || !(_capabilities & JyutpingInputMemory))
    {
        return ERROR_NOT_SUPPORTED;
    }
    if (expectedInstance != _instance)
    {
        return ERROR_RETRY;
    }
    JyutpingMemoryRequest request{};
    request.version = JyutpingEngineProtocolVersion;
    request.operation = static_cast<unsigned long>(operation);
    if (operation != BackendMemoryOperation::Clear)
    {
        request.memoryGeneration = memoryGeneration;
        request.originalTextLength = Encode(text, request.originalText);
        request.romanizationLength = Encode(romanization, request.romanization);
        if (!memoryGeneration || !request.originalTextLength || !request.romanizationLength)
        {
            return ERROR_INVALID_PARAMETER;
        }
    }
    std::copy(_instance.begin(), _instance.end(), request.identity.instance);
    std::copy(_client.begin(), _client.end(), request.identity.client);
    request.identity.context = context;
    request.identity.generation = generation;
    request.identity.revision = revision;
    request.identity.request = ++_request;
    JyutpingMemoryReply reply{};
    const DWORD status = CallMutateMemory(_binding, request, reply);
    if (status != RPC_S_OK)
    {
        // The commit may have succeeded before the reply was lost.
        Disconnect();
        return status;
    }
    if (!Matches(request.identity, reply.identity))
    {
        Disconnect();
        return ERROR_INVALID_DATA;
    }
    switch (reply.status)
    {
    case JyutpingOk: return ERROR_SUCCESS;
    case JyutpingInvalidRequest: return ERROR_INVALID_PARAMETER;
    case JyutpingStorageFailure: return ERROR_WRITE_FAULT;
    case JyutpingStaleMemory: return ERROR_REVISION_MISMATCH;
    case JyutpingCapabilityUnavailable: Disconnect(); return ERROR_NOT_SUPPORTED;
    case JyutpingStaleInstance: Disconnect(); return ERROR_RETRY;
    case JyutpingUnsupportedVersion: Disconnect(); return ERROR_REVISION_MISMATCH;
    default: Disconnect(); return ERROR_INVALID_DATA;
    }
}

bool BackendRpcClient::HasSettings() const
{
    return _binding && (_capabilities & JyutpingSettings);
}

bool BackendRpcClient::HasInputMemory() const
{
    return _binding && (_capabilities & JyutpingInputMemory);
}

bool BackendRpcClient::HasMemoryTransfer() const
{
    return _binding && (_capabilities & JyutpingMemoryTransfer);
}

DWORD BackendRpcClient::GetSettings(BackendSettingsSnapshot& snapshot)
{
    if (!_binding)
    {
        const DWORD status = Connect();
        if (status != ERROR_SUCCESS)
        {
            return status;
        }
    }
    if (!HasSettings())
    {
        return ERROR_NOT_SUPPORTED;
    }
    JyutpingSettingsRequest request{};
    request.version = JyutpingEngineProtocolVersion;
    std::copy(_instance.begin(), _instance.end(), request.identity.instance);
    std::copy(_client.begin(), _client.end(), request.identity.client);
    request.identity.context = 1;
    request.identity.generation = 1;
    request.identity.request = ++_request;
    request.knownRevision = snapshot.instance == _instance ? snapshot.revision : 0;
    JyutpingSettingsReply reply{};
    const DWORD status = CallGetSettings(_binding, request, reply);
    return ApplySettingsReply(request.identity, status, reply, &snapshot);
}

DWORD BackendRpcClient::SetSetting(uint32_t field, uint32_t value, const std::array<unsigned char, 16>& expectedInstance)
{
    if (!HasSettings())
    {
        return ERROR_NOT_SUPPORTED;
    }
    if (expectedInstance != _instance)
    {
        return ERROR_RETRY;
    }
    JyutpingSetSettingRequest request{};
    request.version = JyutpingEngineProtocolVersion;
    std::copy(_instance.begin(), _instance.end(), request.identity.instance);
    std::copy(_client.begin(), _client.end(), request.identity.client);
    request.identity.context = 1;
    request.identity.generation = 1;
    request.identity.request = ++_request;
    request.field = field;
    request.value = value;
    JyutpingSettingsReply reply{};
    const DWORD status = CallSetSetting(_binding, request, reply);
    return ApplySettingsReply(request.identity, status, reply, nullptr);
}

DWORD BackendRpcClient::ApplySettingsReply(const JyutpingRequestIdentity& identity, DWORD status, const JyutpingSettingsReply& reply,
    BackendSettingsSnapshot* snapshot)
{
    if (status != RPC_S_OK || !Matches(identity, reply.identity))
    {
        Disconnect();
        return status != RPC_S_OK ? status : ERROR_INVALID_DATA;
    }
    switch (reply.status)
    {
    case JyutpingOk:
        if (!reply.settingsRevision || (snapshot && snapshot->instance == _instance && reply.settingsRevision < snapshot->revision))
        {
            Disconnect();
            return ERROR_INVALID_DATA;
        }
        if (snapshot)
        {
            static_assert(std::tuple_size_v<decltype(BackendSettingsSnapshot::values)> == JyutpingSettingCount);
            snapshot->instance = _instance;
            snapshot->revision = reply.settingsRevision;
            std::copy_n(reply.values, JyutpingSettingCount, snapshot->values.begin());
        }
        return ERROR_SUCCESS;
    case JyutpingNotModified:
        return snapshot && snapshot->instance == _instance && snapshot->revision == reply.settingsRevision ? ERROR_SUCCESS : ERROR_INVALID_DATA;
    case JyutpingInvalidRequest: return ERROR_INVALID_PARAMETER;
    case JyutpingStorageFailure: return ERROR_WRITE_FAULT;
    case JyutpingStaleInstance: Disconnect(); return ERROR_RETRY;
    case JyutpingUnsupportedVersion: Disconnect(); return ERROR_REVISION_MISMATCH;
    case JyutpingCapabilityUnavailable: Disconnect(); return ERROR_NOT_SUPPORTED;
    default: Disconnect(); return ERROR_INVALID_DATA;
    }
}
