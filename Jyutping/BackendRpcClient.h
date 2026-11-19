#pragma once

#include <Windows.h>
#include <Rpc.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

struct BackendRpcCandidate
{
    std::wstring text;
    std::wstring romanization;
};

enum class BackendLookupMode : uint32_t
{
    Normal,
    Pinyin,
    Cangjie,
    Stroke,
    Structure
};

enum class BackendMemoryOperation : uint32_t
{
    Remember = 1,
    Forget = 2,
    Clear = 3
};

struct BackendEngineCandidate
{
    uint32_t kind = 0;
    uint32_t consumedInput = 0;
    std::wstring text;
    std::wstring comment;
    std::wstring mark;
    std::wstring originalText;
    std::wstring romanization;
};

struct BackendEngineReply
{
    std::vector<BackendEngineCandidate> candidates;
    bool hasMore = false;
    bool memoryEnabled = false;
    uint64_t memoryGeneration = 0;
    std::array<unsigned char, 16> instance{};
};

struct BackendSettingsSnapshot
{
    uint64_t revision = 0;
    std::array<uint32_t, 8> values{};
    std::array<unsigned char, 16> instance{};
    bool operator==(const BackendSettingsSnapshot&) const = default;
};

// One worker uses this binding at a time.
class BackendRpcClient
{
public:
    static constexpr size_t MaxInputEvents = 12;
    static constexpr uint32_t MaxCandidates = 16;

    BackendRpcClient() = default;
    ~BackendRpcClient();
    BackendRpcClient(const BackendRpcClient&) = delete;
    BackendRpcClient& operator=(const BackendRpcClient&) = delete;
    DWORD Suggest(std::wstring_view spelling, uint64_t context, uint64_t generation, uint64_t revision, BackendRpcCandidate& candidate);
    DWORD SuggestCandidates(std::wstring_view input, BackendLookupMode mode, uint32_t standard, uint32_t form, uint32_t limit,
        uint64_t context, uint64_t generation, uint64_t revision, BackendEngineReply& result, wchar_t trigger = 0);
    DWORD MutateMemory(BackendMemoryOperation operation, std::wstring_view text, std::wstring_view romanization,
        uint64_t context, uint64_t generation, uint64_t revision, const std::array<unsigned char, 16>& expectedInstance, uint64_t memoryGeneration);
    bool HasSettings() const;
    bool HasInputMemory() const;
    bool HasMemoryTransfer() const;
    DWORD GetSettings(BackendSettingsSnapshot& snapshot);
    DWORD SetSetting(uint32_t field, uint32_t value, const std::array<unsigned char, 16>& expectedInstance);

private:
    DWORD Connect();
    void Disconnect();
    DWORD ApplySettingsReply(const struct JyutpingRequestIdentity& identity, DWORD status, const struct JyutpingSettingsReply& reply,
        BackendSettingsSnapshot* snapshot);

    RPC_BINDING_HANDLE _binding = nullptr;
    std::array<unsigned char, 16> _client{};
    std::array<unsigned char, 16> _instance{};
    uint64_t _request = 0;
    uint32_t _capabilities = 0;
};
