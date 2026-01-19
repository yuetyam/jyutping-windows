#include "Logger.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <string>
#include <shlobj.h>

namespace Global {

    static std::wstring GetLogFilePath()
    {
        wchar_t* localAppDataPath = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppDataPath)))
        {
            std::wstring logDir = std::wstring(localAppDataPath) + L"\\Jyutping";
            CoTaskMemFree(localAppDataPath);

            // Create directory if it doesn't exist
            CreateDirectoryW(logDir.c_str(), nullptr);

            return logDir + L"\\Jyutping.log";
        }

        // Fallback to temp directory if LocalAppData is not available
        wchar_t tempPath[MAX_PATH];
        GetTempPathW(MAX_PATH, tempPath);
        return std::wstring(tempPath) + L"Jyutping.log";
    }

    void StartLog()
    {
        std::wstring logPath = GetLogFilePath();
        FILE* fp = nullptr;
        _wfopen_s(&fp, logPath.c_str(), L"w");
        if (fp)
        {
            time_t now;
            time(&now);
            char timeStr[26];
            ctime_s(timeStr, sizeof(timeStr), &now);
            fprintf(fp, "=== Jyutping IME Log Started: %s ===\n", timeStr);
            fclose(fp);
        }
    }

    void Log(const wchar_t* format, ...)
    {
        std::wstring logPath = GetLogFilePath();
        FILE* fp = nullptr;
        _wfopen_s(&fp, logPath.c_str(), L"a, ccs=UTF-8"); // Append mode
        if (fp)
        {
            va_list args;
            va_start(args, format);
            vfwprintf(fp, format, args);
            va_end(args);
            fwprintf(fp, L"\n");
            fclose(fp);
        }
    }
}
