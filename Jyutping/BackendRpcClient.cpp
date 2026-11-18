#include "BackendRpcClient.h"

#include "JyutpingRpc.h"
#include "../Shared/Protocol/LocalRpcIdentity.h"

#include <Bcrypt.h>

#include <algorithm>
#include <cstdlib>

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
        status = Hello(_binding, {JyutpingProtocolVersion, JyutpingProtocolVersion}, reply);
    }
    if (status == RPC_S_OK && (reply.status != JyutpingOk || reply.version != JyutpingProtocolVersion ||
        !(reply.capabilities & JyutpingExactSpell) || std::all_of(std::begin(reply.instance), std::end(reply.instance), [](unsigned char byte) { return byte == 0; })))
    {
        status = ERROR_REVISION_MISMATCH;
    }
    if (status != RPC_S_OK)
    {
        Disconnect();
        return status;
    }
    std::copy_n(reply.instance, _instance.size(), _instance.begin());
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
    const auto& identity = reply.identity;
    if (!std::equal(_instance.begin(), _instance.end(), identity.instance) || !std::equal(_client.begin(), _client.end(), identity.client) ||
        identity.context != context || identity.generation != generation || identity.revision != revision || identity.request != request.identity.request)
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
