#include "RpcProbeClient.h"

#include <Rpc.h>
#include <cstdlib>
#include <string_view>

#include "ProbeRpc.h"

#pragma comment(lib, "rpcrt4.lib")

extern "C" void* __RPC_USER midl_user_allocate(size_t size) { return malloc(size); }
extern "C" void __RPC_USER midl_user_free(void* memory) { free(memory); }

unsigned long CallRpc(const wchar_t* endpoint)
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
        unsigned char candidate[256] = {};
        RpcTryExcept
        {
            const long length = Client_Suggest(binding, candidate);
            status = RPC_S_CALL_FAILED;
            if (length > 0 && length <= static_cast<long>(sizeof(candidate)))
            {
                const std::string_view reply(reinterpret_cast<const char*>(candidate), length);
                if (reply == "\xe9\xa6\x99\xe6\xb8\xaf\thoeng1 gong2")
                {
                    status = RPC_S_OK;
                }
            }
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
