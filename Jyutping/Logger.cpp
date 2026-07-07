#include "Logger.h"

#include <shlobj.h>
#include <stdarg.h>
#include <stdio.h>
#include <string>
#include <vector>

namespace {

constexpr PCWSTR AppDirectoryName = L"Jyutping";
constexpr PCWSTR LogsDirectoryName = L"Logs";
constexpr PCWSTR LogFileName = L"Jyutping.log";

SRWLOCK LogLock = SRWLOCK_INIT;

bool IsSlash(WCHAR ch)
{
    return ch == L'\\' || ch == L'/';
}

std::wstring JoinPath(const std::wstring& directory, _In_z_ PCWSTR name)
{
    if (directory.empty())
    {
        return std::wstring(name);
    }

    std::wstring path = directory;
    if (!IsSlash(path.back()))
    {
        path.push_back(L'\\');
    }
    path.append(name);
    return path;
}

bool EnsureDirectory(const std::wstring& directory)
{
    if (directory.empty())
    {
        return false;
    }

    if (CreateDirectoryW(directory.c_str(), nullptr))
    {
        return true;
    }
    if (GetLastError() != ERROR_ALREADY_EXISTS)
    {
        return false;
    }

    DWORD attributes = GetFileAttributesW(directory.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring KnownFolderPath(REFKNOWNFOLDERID folderId)
{
    PWSTR path = nullptr;
    if (FAILED(SHGetKnownFolderPath(folderId, 0, nullptr, &path)))
    {
        return std::wstring();
    }

    std::wstring result(path);
    CoTaskMemFree(path);
    return result;
}

std::wstring TempDirectoryPath()
{
    WCHAR path[MAX_PATH] = { L'\0' };
    DWORD length = GetTempPathW(ARRAYSIZE(path), path);
    if (length == 0 || length >= ARRAYSIZE(path))
    {
        return std::wstring();
    }
    return std::wstring(path, length);
}

std::wstring CreateAppDirectory(const std::wstring& root)
{
    if (root.empty())
    {
        return std::wstring();
    }

    std::wstring directory = JoinPath(root, AppDirectoryName);
    return EnsureDirectory(directory) ? directory : std::wstring();
}

std::wstring ResolveUserDataDirectory()
{
    std::wstring directory = CreateAppDirectory(KnownFolderPath(FOLDERID_LocalAppData));
    if (!directory.empty())
    {
        return directory;
    }

    return CreateAppDirectory(TempDirectoryPath());
}

std::wstring ResolveLogFilePath()
{
    std::wstring logRootDirectory = CreateAppDirectory(TempDirectoryPath());
    if (logRootDirectory.empty())
    {
        return std::wstring();
    }

    std::wstring logDirectory = JoinPath(logRootDirectory, LogsDirectoryName);
    if (!EnsureDirectory(logDirectory))
    {
        logDirectory = logRootDirectory;
    }
    return JoinPath(logDirectory, LogFileName);
}

std::wstring Timestamp()
{
    SYSTEMTIME time = {};
    GetLocalTime(&time);

    WCHAR buffer[32] = { L'\0' };
    swprintf_s(
        buffer,
        L"%04u-%02u-%02u %02u:%02u:%02u.%03u",
        time.wYear,
        time.wMonth,
        time.wDay,
        time.wHour,
        time.wMinute,
        time.wSecond,
        time.wMilliseconds);
    return buffer;
}

std::wstring FormatLogMessage(_In_z_ PCWSTR format, va_list args)
{
    va_list countArgs;
    va_copy(countArgs, args);
    int length = _vscwprintf(format, countArgs);
    va_end(countArgs);
    if (length <= 0)
    {
        return std::wstring();
    }

    std::vector<WCHAR> buffer(static_cast<size_t>(length) + 1);
    va_list writeArgs;
    va_copy(writeArgs, args);
    int written = vswprintf_s(buffer.data(), buffer.size(), format, writeArgs);
    va_end(writeArgs);
    if (written <= 0)
    {
        return std::wstring();
    }
    return std::wstring(buffer.data(), static_cast<size_t>(written));
}

void WriteLogLine(const std::wstring& message)
{
    std::wstring logPath = Global::LogFilePath();
    if (logPath.empty())
    {
        return;
    }

    WCHAR prefix[96] = { L'\0' };
    swprintf_s(
        prefix,
        L"%s [%lu:%lu] ",
        Timestamp().c_str(),
        GetCurrentProcessId(),
        GetCurrentThreadId());

    std::wstring line = prefix;
    line.append(message);
    line.append(L"\r\n");

    OutputDebugStringW(line.c_str());

    AcquireSRWLockExclusive(&LogLock);

    FILE* file = nullptr;
    _wfopen_s(&file, logPath.c_str(), L"a, ccs=UTF-8");
    if (file != nullptr)
    {
        fputws(line.c_str(), file);
        fclose(file);
    }

    ReleaseSRWLockExclusive(&LogLock);
}

} // namespace

namespace Global {

    std::wstring UserDataDirectory()
    {
        static const std::wstring directory = ResolveUserDataDirectory();
        return directory;
    }

    std::wstring LogFilePath()
    {
        static const std::wstring path = ResolveLogFilePath();
        return path;
    }

    void StartLog()
    {
        std::wstring logPath = LogFilePath();
        WriteLogLine(L"=== Jyutping IME log session started ===");
        WriteLogLine(L"Log file: " + logPath);
    }

    void Log(const wchar_t* format, ...)
    {
        if (format == nullptr)
        {
            return;
        }

        va_list args;
        va_start(args, format);
        std::wstring message = FormatLogMessage(format, args);
        va_end(args);

        WriteLogLine(message.empty() ? format : message);
    }
}
