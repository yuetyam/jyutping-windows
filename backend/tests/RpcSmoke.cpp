#include "../../Jyutping/BackendRpcClient.h"
#include "../../Shared/Protocol/LocalRpcIdentity.h"
#include "JyutpingRpc.h"

#include <algorithm>
#include <cstdio>

namespace {

constexpr uint64_t LargeRevision = 0xf0123456789abcde;

bool CheckRawCalls(RPC_BINDING_HANDLE binding)
{
    JyutpingHelloReply hello{};
    const JyutpingHelloRequest invalid{2, 1};
    Client_Hello(binding, &invalid, &hello);
    if (hello.status != JyutpingInvalidRequest)
    {
        return false;
    }
    const JyutpingHelloRequest incompatible{2, 3};
    Client_Hello(binding, &incompatible, &hello);
    if (hello.status != JyutpingUnsupportedVersion)
    {
        return false;
    }
    const JyutpingHelloRequest supported{1, 1};
    Client_Hello(binding, &supported, &hello);
    if (hello.status != JyutpingOk || hello.version != 1 || hello.capabilities != JyutpingExactSpell)
    {
        return false;
    }

    JyutpingSuggestRequest request{};
    JyutpingSuggestReply reply{};
    request.version = 1;
    request.identity.client[0] = 1;
    request.identity.context = 1;
    request.identity.generation = 2;
    request.identity.revision = LargeRevision;
    request.identity.request = 3;
    request.inputCount = 1;
    request.spelling[0] = 'a';
    Client_Suggest(binding, &request, &reply);
    if (reply.status != JyutpingStaleInstance || reply.identity.revision != LargeRevision)
    {
        return false;
    }
    std::copy_n(hello.instance, sizeof(hello.instance), request.identity.instance);
    request.inputCount = JyutpingSpellingCapacity + 1;
    Client_Suggest(binding, &request, &reply);
    if (reply.status != JyutpingInvalidRequest || reply.textLength != 0)
    {
        return false;
    }
    request.inputCount = 1;
    request.spelling[1] = 'z';
    Client_Suggest(binding, &request, &reply);
    if (reply.status != JyutpingInvalidRequest)
    {
        return false;
    }
    request.spelling[1] = 0;
    request.spelling[0] = 'A';
    Client_Suggest(binding, &request, &reply);
    if (reply.status != JyutpingInvalidRequest)
    {
        return false;
    }
    request.spelling[0] = 'a';
    request.version = 2;
    Client_Suggest(binding, &request, &reply);
    return reply.status == JyutpingUnsupportedVersion;
}

bool CheckRawCallsSafely(RPC_BINDING_HANDLE binding)
{
    bool passed = false;
    RpcTryExcept
    {
        passed = CheckRawCalls(binding);
    }
    RpcExcept(RpcExceptionFilter(RpcExceptionCode()))
    {
        printf("RPC fixture failed (%lu)\n", RpcExceptionCode());
    }
    RpcEndExcept
    return passed;
}

bool CheckProtocol()
{
    JyutpingRpcLocal::Identity identity;
    std::wstring logonSid;
    if (identity.ReadProcess() != ERROR_SUCCESS || JyutpingRpcLocal::SidString(identity.LogonSid(), logonSid) != ERROR_SUCCESS)
    {
        return false;
    }
    using JyutpingRpcLocal::RpcString;
    const auto endpoint = JyutpingRpcLocal::Endpoint(identity.session, logonSid);
    RPC_WSTR address = nullptr;
    if (RpcStringBindingComposeW(nullptr, RpcString(L"ncalrpc"), nullptr, RpcString(endpoint.c_str()), nullptr, &address) != RPC_S_OK)
    {
        return false;
    }
    RPC_BINDING_HANDLE binding = nullptr;
    DWORD status = RpcBindingFromStringBindingW(address, &binding);
    RpcStringFreeW(&address);
    if (status != RPC_S_OK)
    {
        return false;
    }

    RPC_SECURITY_QOS qos{RPC_C_SECURITY_QOS_VERSION, RPC_C_QOS_CAPABILITIES_DEFAULT, RPC_C_QOS_IDENTITY_STATIC, RPC_C_IMP_LEVEL_IDENTIFY};
    status = RpcBindingSetAuthInfoExW(binding, nullptr, RPC_C_AUTHN_LEVEL_PKT_PRIVACY, RPC_C_AUTHN_WINNT, nullptr, RPC_C_AUTHZ_NONE, &qos);
    if (status == RPC_S_OK)
    {
        status = RpcBindingSetOption(binding, RPC_C_OPT_CALL_TIMEOUT, 250);
    }
    const bool passed = status == RPC_S_OK && CheckRawCallsSafely(binding);
    RpcBindingFree(&binding);
    return passed;
}

} // namespace

int main()
{
    BackendRpcClient client;
    char command[16];
    while (fgets(command, sizeof(command), stdin))
    {
        BackendRpcCandidate candidate;
        const bool empty = command[0] == 'e';
        const DWORD status = client.Suggest(empty ? L"zzzzzzzzz" : L"hoenggong", empty ? 2 : 1, 1, LargeRevision, candidate);
        bool passed = false;
        switch (command[0])
        {
        case 'q':
            passed = status == ERROR_SUCCESS && candidate.text == L"\u9999\u6e2f" && candidate.romanization == L"hoeng1 gong2";
            break;
        case 'e':
            passed = status == ERROR_SUCCESS && candidate.text.empty() && candidate.romanization.empty();
            break;
        case 'd':
            passed = status == RPC_S_SERVER_UNAVAILABLE || status == RPC_S_CALL_FAILED || status == RPC_S_CALL_FAILED_DNE;
            break;
        case 'p':
            passed = status == ERROR_SUCCESS && CheckProtocol();
            break;
        }
        printf("%s (%lu)\n", passed ? "PASS" : "FAIL", status);
        fflush(stdout);
        if (!passed)
        {
            return 1;
        }
    }
    return 0;
}
