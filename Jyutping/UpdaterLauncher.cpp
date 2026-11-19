#include "Private.h"
#include "InstallPath.h"
#include "UpdaterLauncher.h"
#include "Localization.h"
#include "resource.h"
#include "../Shared/UpdateConfiguration.h"

#include <string>

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

    const std::wstring installPath = InstalledJyutpingDirectory();
    if (installPath.empty())
    {
        return ReportError(IDS_UPDATER_UNAVAILABLE, L"The updater is unavailable. Reinstall Jyutping from https://jyutping.app", ERROR_PATH_NOT_FOUND);
    }

    std::wstring updaterDirectory = installPath + L"\\Updater";
    std::wstring helperPath = installPath + L"\\" + UpdateConfiguration::HelperRelativePath;
    if (GetFileAttributesW(helperPath.c_str()) == INVALID_FILE_ATTRIBUTES || GetFileAttributesW((updaterDirectory + L"\\WinSparkle.dll").c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        return ReportError(IDS_UPDATER_UNAVAILABLE, L"The updater is unavailable. Reinstall Jyutping from https://jyutping.app", ERROR_FILE_NOT_FOUND);
    }

    std::wstring command = L"\"" + helperPath + L"\" --check-for-updates";
    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process = {};
    if (!CreateProcessW(helperPath.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, updaterDirectory.c_str(), &startup, &process))
    {
        DWORD error = GetLastError();
        return ReportError(IDS_UPDATER_START_FAILED, L"Could not open the updater. Try again from a desktop application, or download Jyutping from https://jyutping.app", error);
    }
    AllowSetForegroundWindow(process.dwProcessId);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return S_OK;
}

HRESULT OpenSettings(BOOL isSecureMode)
{
    if (isSecureMode)
    {
        return E_ACCESSDENIED;
    }
    const std::wstring directory = InstalledJyutpingDirectory();
    if (directory.empty())
    {
        return HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);
    }
    const std::wstring settingsDirectory = directory + L"\\Settings";
    const std::wstring executable = settingsDirectory + L"\\JyutpingSettings.exe";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), nullptr, nullptr, nullptr, FALSE, 0, nullptr, settingsDirectory.c_str(), &startup, &process))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    AllowSetForegroundWindow(process.dwProcessId);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return S_OK;
}

}
