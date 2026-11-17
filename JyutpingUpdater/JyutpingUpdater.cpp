#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <sddl.h>
#include <winsparkle.h>

#include <string>
#include <vector>

#include "JyutpingVersion.h"
#include "../Shared/UpdateConfiguration.h"
#include "../Jyutping/resource.h"

#pragma comment(linker, "\"/manifestdependency:type='Win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {

constexpr wchar_t ControllerClass[] = L"Jyutping.UpdateController";
HANDLE completionEvent = nullptr;

class Handle
{
public:
    explicit Handle(HANDLE handle = nullptr) : _handle(handle) {}
    ~Handle() { if (_handle != nullptr) CloseHandle(_handle); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE Get() const { return _handle; }
private:
    HANDLE _handle;
};

void ReportError(UINT resourceId)
{
    wchar_t message[512] = {};
    wchar_t title[128] = {};
    HINSTANCE instance = GetModuleHandleW(nullptr);
    if (!LoadStringW(instance, resourceId, message, ARRAYSIZE(message)))
    {
        wcscpy_s(message, L"Could not open the updater. Reinstall Jyutping from https://jyutping.app/windows.");
    }
    if (!LoadStringW(instance, IDS_UPDATER_TITLE, title, ARRAYSIZE(title)))
    {
        wcscpy_s(title, L"Jyutping Update");
    }
    MessageBoxW(nullptr, message, title, MB_OK | MB_ICONERROR);
}

void __cdecl FinishInteraction()
{
    // Callbacks can run on WinSparkle threads. Cleanup belongs to our main thread.
    SetEvent(completionEvent);
}

int __cdecl CanCloseHelper()
{
    return 1;
}

BOOL CALLBACK ActivateDialog(HWND window, LPARAM)
{
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (processId == GetCurrentProcessId() && IsWindowVisible(window) && GetWindow(window, GW_OWNER) == nullptr)
    {
        if (IsIconic(window)) ShowWindow(window, SW_RESTORE);
        SetForegroundWindow(window);
        BringWindowToTop(window);
        return FALSE;
    }
    return TRUE;
}

LRESULT CALLBACK ControllerProcedure(HWND window, UINT message, WPARAM parameter, LPARAM data)
{
    if (message == WM_CLOSE || (message == WM_ENDSESSION && parameter))
    {
        FinishInteraction();
        return 0;
    }
    return DefWindowProcW(window, message, parameter, data);
}

std::wstring UserIdentity()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return {};
    Handle tokenHandle(token);
    DWORD bytes = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
    std::vector<BYTE> user(bytes);
    if (!GetTokenInformation(token, TokenUser, user.data(), bytes, &bytes)) return {};
    LPWSTR sid = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid, &sid)) return {};
    std::wstring identity(sid);
    LocalFree(sid);
    return identity;
}

bool HasCheckArgument()
{
    int count = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments) return false;
    bool valid = count == 2 && wcscmp(arguments[1], L"--check-for-updates") == 0;
    LocalFree(arguments);
    return valid;
}

}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    if (!HasCheckArgument()) return ERROR_INVALID_PARAMETER;
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);

    std::wstring identity = UserIdentity();
    if (identity.empty())
    {
        ReportError(IDS_UPDATER_START_FAILED);
        return ERROR_ACCESS_DENIED;
    }

    // Local kernel objects are scoped to this logon session; the SID separates users.
    std::wstring mutexName = L"Local\\Jyutping.Updater.Mutex." + identity;
    Handle mutex(CreateMutexW(nullptr, TRUE, mutexName.c_str()));
    DWORD mutexError = GetLastError();
    if (!mutex.Get())
    {
        ReportError(IDS_UPDATER_START_FAILED);
        return static_cast<int>(mutexError);
    }
    std::wstring activationName = L"Local\\Jyutping.Updater.Activate." + identity;
    Handle activation(CreateEventW(nullptr, FALSE, FALSE, activationName.c_str()));
    if (!activation.Get())
    {
        DWORD error = GetLastError();
        ReportError(IDS_UPDATER_START_FAILED);
        return static_cast<int>(error);
    }
    // An abandoned mutex means the old helper exited before an activation request completed.
    if (mutexError == ERROR_ALREADY_EXISTS && WaitForSingleObject(mutex.Get(), 0) == WAIT_TIMEOUT)
    {
        HWND controller = FindWindowW(ControllerClass, identity.c_str());
        DWORD processId = 0;
        if (controller && GetWindowThreadProcessId(controller, &processId)) AllowSetForegroundWindow(processId);
        SetEvent(activation.Get());
        return 0;
    }

    Handle completed(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!completed.Get()) return static_cast<int>(GetLastError());
    completionEvent = completed.Get();
    WNDCLASSW windowClass = {};
    windowClass.lpfnWndProc = ControllerProcedure;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = ControllerClass;
    if (!RegisterClassW(&windowClass)) return static_cast<int>(GetLastError());
    HWND controller = CreateWindowExW(WS_EX_TOOLWINDOW, ControllerClass, identity.c_str(), 0, 0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
    if (!controller) return static_cast<int>(GetLastError());
    // Consume a caller's initial SW_HIDE so it cannot hide WinSparkle's first dialog.
    ShowWindow(controller, SW_HIDE);

    win_sparkle_set_app_details(L"Jyutping.org", L"Jyutping", JYUTPING_VERSION_WIDE);
    win_sparkle_set_registry_path("Software\\Jyutping\\WinSparkle");
    win_sparkle_set_appcast_url(UpdateConfiguration::FeedUrl);
    if (win_sparkle_set_eddsa_public_key(UpdateConfiguration::PublicKey) != 1)
    {
        ReportError(IDS_UPDATER_CONFIG_FAILED);
        DestroyWindow(controller);
        return ERROR_INVALID_DATA;
    }
    win_sparkle_set_automatic_check_for_updates(0);
    win_sparkle_set_can_shutdown_callback(CanCloseHelper);
    win_sparkle_set_shutdown_request_callback(FinishInteraction);
    win_sparkle_set_update_dismissed_callback(FinishInteraction);
    win_sparkle_init();
    win_sparkle_check_update_with_ui();

    HANDLE events[] = {completed.Get(), activation.Get()};
    bool running = true;
    while (running)
    {
        DWORD result = MsgWaitForMultipleObjects(ARRAYSIZE(events), events, FALSE, INFINITE, QS_ALLINPUT);
        if (result == WAIT_OBJECT_0 || result == WAIT_FAILED) break;
        if (result == WAIT_OBJECT_0 + 1)
        {
            // Another API check would reset the dialog and start a parallel network request.
            EnumWindows(ActivateDialog, 0);
        }
        MSG message = {};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            if (message.message == WM_QUIT) { running = false; break; }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    win_sparkle_cleanup();
    win_sparkle_set_update_dismissed_callback(nullptr);
    win_sparkle_set_shutdown_request_callback(nullptr);
    DestroyWindow(controller);
    completionEvent = nullptr;
    return 0;
}
