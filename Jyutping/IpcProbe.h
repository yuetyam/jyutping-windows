#pragma once

#ifdef JYUTPING_IPC_PROBE

#include <Windows.h>

#include <chrono>
#include <cstdint>
#include <memory>

class IpcProbe
{
public:
    using Clock = std::chrono::steady_clock;

    IpcProbe() = default;
    IpcProbe(const IpcProbe&) = delete;
    IpcProbe& operator=(const IpcProbe&) = delete;
    ~IpcProbe();
    void Initialize();
    void Invalidate();
    void Measure(Clock::duration localLookup);
    bool IsEnabled() const { return _window != nullptr; }

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
    bool _pending = false;
    Clock::time_point _changedAt;
    Clock::time_point _queuedAt;
};

#endif
