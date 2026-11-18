#pragma once

#include <Windows.h>

#include <string>
#include <string_view>

struct RpcProbeCandidate
{
    std::wstring text;
    std::wstring romanization;
};

unsigned long QueryRpcProbe(const wchar_t* endpoint, std::wstring_view spell, RpcProbeCandidate& candidate);
unsigned long CallRpc(const wchar_t* endpoint);
