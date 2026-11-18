#include "RpcProbeClient.h"

#include <Rpc.h>
#include <cstdlib>

#include "ProbeRpc.h"

#pragma comment(lib, "rpcrt4.lib")

extern "C" void* __RPC_USER midl_user_allocate(size_t size) { return malloc(size); }
extern "C" void __RPC_USER midl_user_free(void* memory) { free(memory); }

static unsigned long QueryBytes(const wchar_t* endpoint, unsigned char spell[ProbeSpellCapacity], unsigned char candidate[ProbeReplyCapacity], long& length)
{
    RPC_WSTR bindingString = nullptr;
    wchar_t protocol[] = L"ncalrpc";
    auto status = RpcStringBindingComposeW(nullptr, reinterpret_cast<RPC_WSTR>(protocol), nullptr,
        reinterpret_cast<RPC_WSTR>(const_cast<wchar_t*>(endpoint)), nullptr, &bindingString);
    if (status != RPC_S_OK)
    {
        return status;
    }

    RPC_BINDING_HANDLE binding = nullptr;
    status = RpcBindingFromStringBindingW(bindingString, &binding);
    RpcStringFreeW(&bindingString);
    if (status != RPC_S_OK)
    {
        return status;
    }

    status = RpcBindingSetOption(binding, RPC_C_OPT_CALL_TIMEOUT, 250);
    if (status == RPC_S_OK)
    {
        RpcTryExcept
        {
            length = Client_Suggest(binding, spell, candidate);
        }
        RpcExcept(RpcExceptionFilter(RpcExceptionCode()))
        {
            status = RpcExceptionCode();
        }
        RpcEndExcept
    }
    RpcBindingFree(&binding);
    return status;
}

unsigned long QueryRpcProbe(const wchar_t* endpoint, std::wstring_view spell, RpcProbeCandidate& candidate)
{
    if (spell.empty() || spell.size() > ProbeSpellCapacity || spell.find_first_not_of(L"abcdefghijklmnopqrstuvwxyz") != std::wstring_view::npos)
    {
        return ERROR_INVALID_PARAMETER;
    }
    unsigned char input[ProbeSpellCapacity] = {};
    for (size_t index = 0; index < spell.size(); ++index)
    {
        input[index] = static_cast<unsigned char>(spell[index]);
    }
    unsigned char bytes[ProbeReplyCapacity] = {};
    long length = 0;
    const auto status = QueryBytes(endpoint, input, bytes, length);
    candidate = {};
    if (status != RPC_S_OK || length == 0)
    {
        return status;
    }
    if (length < 0)
    {
        return length == -2 ? ERROR_INVALID_PARAMETER : ERROR_READ_FAULT;
    }
    if (length > ProbeReplyCapacity)
    {
        return ERROR_INVALID_DATA;
    }

    const auto* utf8 = reinterpret_cast<const char*>(bytes);
    if (std::string_view(utf8, length).find('\0') != std::string_view::npos)
    {
        return ERROR_INVALID_DATA;
    }
    wchar_t decoded[ProbeReplyCapacity];
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, length, decoded, ARRAYSIZE(decoded));
    const std::wstring_view reply(decoded, count);
    const auto separator = reply.find(L'\t');
    if (separator == 0 || separator == std::wstring_view::npos || separator + 1 == reply.size() ||
        reply.find(L'\t', separator + 1) != std::wstring_view::npos)
    {
        return ERROR_INVALID_DATA;
    }
    candidate = {std::wstring(reply.substr(0, separator)), std::wstring(reply.substr(separator + 1))};
    return RPC_S_OK;
}

unsigned long CallRpc(const wchar_t* endpoint)
{
    RpcProbeCandidate candidate;
    const auto status = QueryRpcProbe(endpoint, L"hoenggong", candidate);
    if (status != RPC_S_OK)
    {
        return status;
    }
    return candidate.text == L"\u9999\u6e2f" && candidate.romanization == L"hoeng1 gong2" ? RPC_S_OK : RPC_S_CALL_FAILED;
}
