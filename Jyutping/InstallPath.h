#pragma once

#include <Windows.h>
#include <string>
#include <vector>

#include "../Shared/UpdateConfiguration.h"

inline std::wstring InstalledJyutpingDirectory()
{
    DWORD pathBytes = 0;
    constexpr DWORD registryFlags = RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY;
    LSTATUS status = RegGetValueW(HKEY_LOCAL_MACHINE, UpdateConfiguration::InstallRegistryKey, UpdateConfiguration::InstallPathValue,
        registryFlags, nullptr, nullptr, &pathBytes);
    if (status != ERROR_SUCCESS || pathBytes <= sizeof(WCHAR))
    {
        return {};
    }
    std::vector<WCHAR> buffer(pathBytes / sizeof(WCHAR) + 1);
    status = RegGetValueW(HKEY_LOCAL_MACHINE, UpdateConfiguration::InstallRegistryKey, UpdateConfiguration::InstallPathValue,
        registryFlags, nullptr, buffer.data(), &pathBytes);
    std::wstring path = status == ERROR_SUCCESS ? buffer.data() : L"";
    // Use the installer-owned native path, including when running in an x86 host.
    return path.size() >= 3 && path[1] == L':' && path[2] == L'\\' ? path : std::wstring();
}
