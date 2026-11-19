#include "../../Jyutping/BackendRpcClient.h"
#include "../../Shared/Protocol/LocalRpcIdentity.h"
#include "JyutpingRpc.h"

#include <algorithm>
#include <cstdio>

namespace {

constexpr uint64_t LargeRevision = 0xf0123456789abcde;
constexpr std::string_view LearnedWord = "\xe8\xa8\x98\xe6\x86\xb6\xe8\xa9\x9e";
constexpr std::string_view LearnedReading = "hoeng1 gong2";

std::string_view Field(const unsigned char* bytes, unsigned long length)
{
    return length <= JyutpingCandidateCapacity ? std::string_view(reinterpret_cast<const char*>(bytes), length) : std::string_view();
}

bool CheckEngineCalls(RPC_BINDING_HANDLE binding, const JyutpingRequestIdentity& identity)
{
    JyutpingHelloReply hello{};
    const JyutpingHelloRequest versions{1, JyutpingEngineProtocolVersion};
    Client_Hello(binding, &versions, &hello);
    if (hello.status != JyutpingOk || hello.version != JyutpingEngineProtocolVersion ||
        (hello.capabilities & (JyutpingExactSpell | JyutpingEngineCandidates)) != (JyutpingExactSpell | JyutpingEngineCandidates))
    {
        return false;
    }

    JyutpingEngineRequest request{};
    request.version = hello.version;
    request.identity = identity;
    request.mode = JyutpingNormal;
    request.characterStandard = 51;
    request.romanizationForm = 2;
    request.resultLimit = 1;
    constexpr std::string_view input = "gongdungwaa";
    request.inputCount = static_cast<unsigned long>(input.size());
    std::copy(input.begin(), input.end(), request.input);

    JyutpingEngineReply reply{};
    Client_SuggestCandidates(binding, &request, &reply);
    const auto& candidate = reply.candidates[0];
    if (reply.status != JyutpingOk || reply.candidateCount != 1 || reply.hasMore != 1 || reply.identity.revision != LargeRevision ||
        candidate.kind != JyutpingCantoneseCandidate || candidate.consumedInput != input.size() ||
        Field(candidate.text, candidate.textLength) != "\xe8\xae\xb2\xe5\x8a\xa8\xe7\x94\xbb" ||
        Field(candidate.originalText, candidate.originalTextLength) != "\xe8\xac\x9b\xe5\x8b\x95\xe7\x95\xab" ||
        Field(candidate.comment, candidate.commentLength) != "gong dung waa" ||
        Field(candidate.mark, candidate.markLength) != "gong dung waa")
    {
        return false;
    }

    request.mode = JyutpingStructure;
    request.characterStandard = 1;
    request.romanizationForm = 1;
    request.resultLimit = JyutpingEngineResultCapacity;
    ZeroMemory(request.input, sizeof(request.input));
    constexpr std::string_view components = "muk'muk";
    request.inputCount = static_cast<unsigned long>(components.size());
    std::copy(components.begin(), components.end(), request.input);
    Client_SuggestCandidates(binding, &request, &reply);
    if (reply.status != JyutpingOk || reply.candidateCount == 0 || reply.candidateCount > JyutpingEngineResultCapacity ||
        std::none_of(reply.candidates, reply.candidates + reply.candidateCount, [&](const auto& item) {
            return item.consumedInput == components.size() && Field(item.text, item.textLength) == "\xe6\x9e\x97" &&
                Field(item.romanization, item.romanizationLength) == "lam4";
        }))
    {
        return false;
    }

    request.input[0] = 'Z';
    Client_SuggestCandidates(binding, &request, &reply);
    if (reply.status != JyutpingInvalidRequest || reply.candidateCount != 0)
    {
        return false;
    }
    request.input[0] = 'm';
    request.identity.instance[0] ^= 1;
    Client_SuggestCandidates(binding, &request, &reply);
    if (reply.status != JyutpingStaleInstance)
    {
        return false;
    }
    request.identity.instance[0] ^= 1;
    ZeroMemory(request.input, sizeof(request.input));
    request.inputCount = 1;
    request.input[0] = 'z';
    Client_SuggestCandidates(binding, &request, &reply);
    return reply.status == JyutpingEmpty && reply.candidateCount == 0 && reply.hasMore == 0;
}

bool CheckRawCalls(RPC_BINDING_HANDLE binding)
{
    JyutpingHelloReply hello{};
    const JyutpingHelloRequest invalid{2, 1};
    Client_Hello(binding, &invalid, &hello);
    if (hello.status != JyutpingInvalidRequest)
    {
        return false;
    }
    const JyutpingHelloRequest incompatible{3, 4};
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
    return reply.status == JyutpingUnsupportedVersion && CheckEngineCalls(binding, request.identity);
}

bool CheckMemory(RPC_BINDING_HANDLE binding, char command)
{
    const JyutpingHelloRequest versions{JyutpingEngineProtocolVersion, JyutpingEngineProtocolVersion};
    JyutpingHelloReply hello{};
    Client_Hello(binding, &versions, &hello);
    if (hello.status != JyutpingOk || !(hello.capabilities & JyutpingInputMemory))
    {
        return false;
    }
    JyutpingRequestIdentity identity{};
    std::copy_n(hello.instance, sizeof(hello.instance), identity.instance);
    identity.client[0] = 1;
    identity.context = 1;
    identity.generation = 2;
    identity.revision = LargeRevision;
    identity.request = 3;
    JyutpingEngineRequest request{};
    request.version = hello.version;
    request.identity = identity;
    request.characterStandard = 1;
    request.romanizationForm = 1;
    request.resultLimit = 1;
    constexpr std::string_view input = "hoenggong";
    request.inputCount = static_cast<unsigned long>(input.size());
    std::copy(input.begin(), input.end(), request.input);
    JyutpingEngineReply reply{};
    Client_SuggestCandidates(binding, &request, &reply);
    return reply.status == JyutpingOk && reply.candidateCount == 1 &&
        (Field(reply.candidates[0].originalText, reply.candidates[0].originalTextLength) == LearnedWord) == (command != 'f');
}

bool CheckSettings(RPC_BINDING_HANDLE binding, char command)
{
    JyutpingHelloReply hello{};
    const JyutpingHelloRequest versions{JyutpingEngineProtocolVersion, JyutpingEngineProtocolVersion};
    Client_Hello(binding, &versions, &hello);
    if (hello.status != JyutpingOk || !(hello.capabilities & JyutpingSettings))
    {
        return false;
    }
    JyutpingSettingsRequest request{};
    request.version = hello.version;
    std::copy_n(hello.instance, sizeof(hello.instance), request.identity.instance);
    request.identity.client[0] = 1;
    request.identity.context = 1;
    request.identity.generation = 1;
    request.identity.revision = LargeRevision;
    request.identity.request = 1;
    JyutpingSettingsReply reply{};
    Client_GetSettings(binding, &request, &reply);
    if (reply.status != JyutpingOk || !reply.settingsRevision || reply.identity.revision != LargeRevision)
    {
        return false;
    }
    if (command == 'v')
    {
        return reply.values[4] == 9 && reply.values[7] == 19;
    }
    if (reply.values[4] != 7 || reply.values[7] != 13)
    {
        return false;
    }
    JyutpingSetSettingRequest change{};
    change.version = request.version;
    change.identity = request.identity;
    change.field = 4;
    change.value = 11;
    ++change.identity.request;
    Client_SetSetting(binding, &change, &reply);
    if (reply.status != JyutpingInvalidRequest)
    {
        return false;
    }
    change.value = 9;
    ++change.identity.request;
    Client_SetSetting(binding, &change, &reply);
    if (reply.status != JyutpingOk || reply.settingsRevision <= 1 || reply.values[4] != 9 || reply.values[7] != 13)
    {
        return false;
    }
    request.identity.request = change.identity.request + 1;
    request.knownRevision = reply.settingsRevision;
    Client_GetSettings(binding, &request, &reply);
    return reply.status == JyutpingNotModified && reply.settingsRevision == request.knownRevision;
}

bool CheckRawCallsSafely(RPC_BINDING_HANDLE binding, char command)
{
    bool passed = false;
    RpcTryExcept
    {
        if (command == 'p')
        {
            passed = CheckRawCalls(binding);
        }
        else if (command == 's' || command == 'v')
        {
            passed = CheckSettings(binding, command);
        }
        else
        {
            passed = CheckMemory(binding, command);
        }
    }
    RpcExcept(RpcExceptionFilter(RpcExceptionCode()))
    {
        printf("RPC fixture failed (%lu)\n", RpcExceptionCode());
    }
    RpcEndExcept
    return passed;
}

bool CheckProtocol(char command)
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
    const bool passed = status == RPC_S_OK && CheckRawCallsSafely(binding, command);
    RpcBindingFree(&binding);
    return passed;
}

} // namespace

int main()
{
    BackendRpcClient client;
    char command[32];
    while (fgets(command, sizeof(command), stdin))
    {
        BackendRpcCandidate candidate;
        const bool empty = command[0] == 'e';
        const DWORD status = client.Suggest(empty ? L"zzzzzzzzz" : L"hoenggong", empty ? 2 : 1, 1, LargeRevision, candidate);
        bool passed = false;
        switch (command[0])
        {
        case 'q':
        {
            BackendEngineReply suggestions;
            const DWORD engineStatus = client.SuggestCandidates(L"hoenggong", BackendLookupMode::Normal, 1, 1, 5, 1, 1, LargeRevision, suggestions);
            passed = status == ERROR_SUCCESS && candidate.text == L"\u9999\u6e2f" && candidate.romanization == L"hoeng1 gong2" &&
                engineStatus == ERROR_SUCCESS && !suggestions.candidates.empty() &&
                suggestions.candidates.front().text == L"\u9999\u6e2f" &&
                suggestions.candidates.front().mark == L"hoeng gong" && suggestions.candidates.front().consumedInput == 9;
            break;
        }
        case 'e':
            passed = status == ERROR_SUCCESS && candidate.text.empty() && candidate.romanization.empty();
            break;
        case 'd':
            passed = status == RPC_S_SERVER_UNAVAILABLE || status == RPC_S_CALL_FAILED || status == RPC_S_CALL_FAILED_DNE;
            break;
        case 'p':
            passed = status == ERROR_SUCCESS && CheckProtocol(command[0]);
            break;
        case 'l':
        case 'f':
        case 'c':
        {
            BackendEngineReply suggestions;
            constexpr std::wstring_view word = L"\u8a18\u61b6\u8a5e";
            constexpr std::wstring_view reading = L"hoeng1 gong2";
            passed = client.SuggestCandidates(L"hoenggong", BackendLookupMode::Normal, 1, 1, 1, 1, 1, LargeRevision, suggestions) == ERROR_SUCCESS && suggestions.memoryEnabled;
            if (passed && command[0] == 'c')
            {
                passed = client.MutateMemory(BackendMemoryOperation::Clear, {}, {}, 1, 1, LargeRevision, suggestions.instance, 0) == ERROR_SUCCESS &&
                    client.MutateMemory(BackendMemoryOperation::Remember, word, reading, 1, 1, LargeRevision, suggestions.instance, suggestions.memoryGeneration) == ERROR_REVISION_MISMATCH &&
                    CheckProtocol('f');
            }
            else if (passed)
            {
                const auto operation = command[0] == 'l' ? BackendMemoryOperation::Remember : BackendMemoryOperation::Forget;
                passed = client.MutateMemory(operation, word, reading, 1, 1, LargeRevision, suggestions.instance, suggestions.memoryGeneration) == ERROR_SUCCESS &&
                    CheckProtocol(command[0]);
            }
            break;
        }
        case 'm':
        case 's':
        case 'v':
            passed = CheckProtocol(command[0]);
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
