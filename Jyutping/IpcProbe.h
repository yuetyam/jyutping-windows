#pragma once

#ifdef JYUTPING_IPC_PROBE

#include <Windows.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#ifdef JYUTPING_RPC_PROBE
struct RpcProbeCandidate;
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
    using ResultCallback = void (*)(void*, uint64_t, const RpcProbeCandidate&);
    void SetResultCallback(ResultCallback callback, void* context);
    void Query(std::wstring spell);
    bool ArmSelectionTimeout();
    bool HasListener() const { return _hasListener; }
    uint64_t Revision() const { return _revision; }
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
    uint64_t _generation = 1;
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
