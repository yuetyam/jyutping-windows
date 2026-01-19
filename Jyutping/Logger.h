#pragma once

#include <windows.h>

namespace Global {
    void StartLog();
    void Log(const wchar_t* format, ...);
}
