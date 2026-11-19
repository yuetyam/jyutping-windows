#pragma once

#ifdef JYUTPING_IPC_PROBE

#include <Windows.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#ifdef JYUTPING_RPC_PROBE
#include "BackendRpcClient.h"
#endif

class IpcProbe
{
public:
    using Clock = std::chrono::steady_clock;

    IpcProbe() = default;
    IpcProbe(const IpcProbe&) = delete;
    IpcProbe& operator=(const IpcProbe&) = delete;
    ~IpcProbe();
    void Initialize();
    void Invalidate(bool replaceComposition = false);
    void Measure(Clock::duration localLookup);
    bool IsEnabled() const { return _window != nullptr; }
#ifdef JYUTPING_RPC_PROBE
    using ResultCallback = void (*)(void*, uint64_t, BackendEngineReply);
    void SetResultCallback(ResultCallback callback, void* context);
    void Query(std::wstring spell, uint32_t characterStandard, BackendLookupMode mode, wchar_t trigger);
    bool ArmSelectionTimeout();
    bool HasListener() const { return _hasListener; }
    uint64_t Revision() const { return _revision; }
#endif
#ifdef JYUTPING_BACKEND_RPC
    using SettingsCallback = void (*)(void*, const BackendSettingsSnapshot&);
    void SetSettingsCallback(SettingsCallback callback);
    bool HasSettings() const { return _settingsSnapshot.revision != 0; }
    bool QueueSetting(UINT field, DWORD value);
    bool HasInputMemory() const { return _memoryEnabled; }
    uint64_t MemoryGeneration() const { return _memoryGeneration; }
    const std::array<unsigned char, 16>& MemoryInstance() const { return _memoryInstance; }
    bool QueueMemoryWrite(BackendMemoryOperation operation, const std::wstring& text, const std::wstring& romanization);
#endif

private:
    struct State;
    struct Work;
    static void CALLBACK Run(PTP_CALLBACK_INSTANCE instance, void* context);
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR subclassId, DWORD_PTR reference);
    void Submit();
    void OnResult();

    std::shared_ptr<State> _state;
    HWND _window = nullptr;
    uint64_t _revision = 0;
#ifdef JYUTPING_BACKEND_RPC
    struct MemoryWrite
    {
        BackendMemoryOperation operation;
        std::wstring text;
        std::wstring romanization;
        uint64_t generation;
        uint64_t revision;
        uint64_t memoryGeneration;
        std::array<unsigned char, 16> instance;
    };
    uint64_t _generation = 1;
    uint32_t _pendingStandard = 1;
    BackendLookupMode _pendingMode{};
    wchar_t _pendingTrigger = 0;
    std::vector<MemoryWrite> _memoryWrites;
    std::array<unsigned char, 16> _memoryInstance{};
    uint64_t _memoryGeneration = 0;
    bool _memoryEnabled = false;
    BackendSettingsSnapshot _settingsSnapshot;
    std::array<std::optional<DWORD>, 8> _settingsWrites{};
    uint64_t _settingsEpoch = 0;
    uint64_t _appliedSettingsEpoch = 0;
    bool _settingsPoll = false;
    UINT_PTR _settingsTimer = 0;
    SettingsCallback _settingsCallback = nullptr;
    Clock::time_point _nextHealthCheck{};
    UINT_PTR _healthTimer = 0;
    unsigned int _healthDelayMs = 500;
#endif
    bool _pending = false;
    Clock::time_point _changedAt;
    Clock::time_point _queuedAt;
#ifdef JYUTPING_RPC_PROBE
    std::wstring _pendingSpell;
    ResultCallback _callback = nullptr;
    void* _callbackContext = nullptr;
    bool _hasListener = false;
    UINT_PTR _selectionTimer = 0;
    UINT_PTR _nextTimer = 0;
    Clock::time_point _selectionDeadline;
    void StopSelectionTimer();
#endif
};

#endif
