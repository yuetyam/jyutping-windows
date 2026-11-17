#include "Private.h"
#include "UpdaterLauncher.h"
#include "Localization.h"
#include "resource.h"
#include "../Shared/UpdateConfiguration.h"

#include <string>
#include <vector>

namespace UpdaterLauncher {

namespace {

HRESULT ReportError(UINT resourceId, PCWSTR fallback, DWORD error)
{
    std::wstring message = Localization::LoadStringOrFallback(resourceId, fallback);
    std::wstring title = Localization::LoadStringOrFallback(IDS_UPDATER_TITLE, L"Jyutping Update");
    MessageBoxW(nullptr, message.c_str(), title.c_str(), MB_OK | MB_ICONERROR);
    return HRESULT_FROM_WIN32(error);
}

}

HRESULT CheckForUpdates(BOOL isSecureMode)
{
    if (isSecureMode)
    {
        return E_ACCESSDENIED;
    }

    DWORD pathBytes = 0;
    constexpr DWORD registryFlags = RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY;
    LSTATUS status = RegGetValueW(HKEY_LOCAL_MACHINE, UpdateConfiguration::InstallRegistryKey, UpdateConfiguration::InstallPathValue, registryFlags, nullptr, nullptr, &pathBytes);
    std::vector<WCHAR> buffer(pathBytes / sizeof(WCHAR) + 1);
    if (status == ERROR_SUCCESS && pathBytes > sizeof(WCHAR))
    {
        status = RegGetValueW(HKEY_LOCAL_MACHINE, UpdateConfiguration::InstallRegistryKey, UpdateConfiguration::InstallPathValue, registryFlags, nullptr, buffer.data(), &pathBytes);
    }
    else
    {
        status = ERROR_PATH_NOT_FOUND;
    }

    std::wstring installPath = status == ERROR_SUCCESS ? buffer.data() : L"";
    // Only the installer-owned absolute native path is authoritative, including in x86 hosts.
    if (installPath.length() < 3 || installPath[1] != L':' || installPath[2] != L'\\')
    {
        return ReportError(IDS_UPDATER_UNAVAILABLE, L"The updater is unavailable. Reinstall Jyutping from https://jyutping.app/windows.", ERROR_PATH_NOT_FOUND);
    }

    std::wstring updaterDirectory = installPath + L"\\Updater";
    std::wstring helperPath = installPath + L"\\" + UpdateConfiguration::HelperRelativePath;
    if (GetFileAttributesW(helperPath.c_str()) == INVALID_FILE_ATTRIBUTES || GetFileAttributesW((updaterDirectory + L"\\WinSparkle.dll").c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        return ReportError(IDS_UPDATER_UNAVAILABLE, L"The updater is unavailable. Reinstall Jyutping from https://jyutping.app/windows.", ERROR_FILE_NOT_FOUND);
    }

    std::wstring command = L"\"" + helperPath + L"\" --check-for-updates";
    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process = {};
    if (!CreateProcessW(helperPath.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, updaterDirectory.c_str(), &startup, &process))
    {
        DWORD error = GetLastError();
        return ReportError(IDS_UPDATER_START_FAILED, L"Could not open the updater. Try again from a desktop application, or download Jyutping from https://jyutping.app/windows.", error);
    }
    AllowSetForegroundWindow(process.dwProcessId);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return S_OK;
}

}
