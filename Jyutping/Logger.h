#pragma once

#include <windows.h>

#include <string>

namespace Global {
    std::wstring UserDataDirectory();
    std::wstring LogFilePath();
    void StartLog();
    void Log(const wchar_t* format, ...);
}
