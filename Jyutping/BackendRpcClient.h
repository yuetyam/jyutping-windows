#pragma once

#include <Windows.h>
#include <Rpc.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

struct BackendRpcCandidate
{
    std::wstring text;
    std::wstring romanization;
};

// One worker uses this binding at a time.
class BackendRpcClient
{
public:
    BackendRpcClient() = default;
    ~BackendRpcClient();
    BackendRpcClient(const BackendRpcClient&) = delete;
    BackendRpcClient& operator=(const BackendRpcClient&) = delete;
    DWORD Suggest(std::wstring_view spelling, uint64_t context, uint64_t generation, uint64_t revision, BackendRpcCandidate& candidate);

private:
    DWORD Connect();
    void Disconnect();

    RPC_BINDING_HANDLE _binding = nullptr;
    std::array<unsigned char, 16> _client{};
    std::array<unsigned char, 16> _instance{};
    uint64_t _request = 0;
};
