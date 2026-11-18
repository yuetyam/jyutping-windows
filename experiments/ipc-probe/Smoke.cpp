// Exercise the DLL's client and its owner-thread message delivery without registering a test IME.
#include <Windows.h>

#include <cstdio>
#include <cwchar>

namespace {

const wchar_t* expected = nullptr;
DWORD ownerThread = 0;
bool receivedExpectedResult = false;
bool backendUnavailable = false;

void RecordProbeOutput(const wchar_t* line)
{
    fputws(line, stdout);
    fputwc(L'\n', stdout);
    if (GetCurrentThreadId() == ownerThread && wcsstr(line, expected))
    {
        receivedExpectedResult = true;
    }
    backendUnavailable = wcsstr(line, L"stage=connect error=2 ") || wcsstr(line, L"stage=connect error=231 ") || wcsstr(line, L"stage=local RPC error=1722 ");
}

}

#define OutputDebugStringW RecordProbeOutput
#define JYUTPING_IPC_PROBE_SMOKE
#include "../../Jyutping/IpcProbe.cpp"
#undef OutputDebugStringW

HINSTANCE Global::dllInstanceHandle = GetModuleHandleW(nullptr);

int wmain(int argc, wchar_t** argv)
{
    if (argc < 2 || argc > 3)
    {
        return 2;
    }
    expected = argv[1];
    ownerThread = GetCurrentThreadId();
    IpcProbe probe;
    probe.Initialize();
    if (!probe.IsEnabled())
    {
        return 3;
    }

    const bool invalidate = argc == 3;
    auto submit = [&]()
    {
        probe.Invalidate();
        probe.Measure(IpcProbe::Clock::duration::zero());
        if (invalidate)
        {
            // Simulate a new revision or cancellation before dispatching the queued reply.
            probe.Invalidate();
        }
    };
    submit();

    ULONGLONG deadline = GetTickCount64() + 4000;
    while (!receivedExpectedResult && GetTickCount64() < deadline)
    {
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 25, QS_ALLINPUT);
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            DispatchMessageW(&message);
        }
        if (backendUnavailable && !receivedExpectedResult)
        {
            Sleep(50);
            submit();
        }
    }
    return receivedExpectedResult ? 0 : 1;
}
