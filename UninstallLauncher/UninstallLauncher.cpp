#include <windows.h>
#include <shellapi.h>

#include <array>
#include <cwctype>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr PCWSTR ProductName = L"Jyutping";
constexpr PCWSTR UninstallKeyPath =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{B120D0CC-C4A1-4F0A-BA46-B3F1376BDE4F}_is1";

bool FileExists(const std::wstring& path)
{
    DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring Trim(std::wstring value)
{
    size_t first = 0;
    while (first < value.size() && iswspace(value[first]))
    {
        ++first;
    }

    size_t last = value.size();
    while (last > first && iswspace(value[last - 1]))
    {
        --last;
    }

    return value.substr(first, last - first);
}

std::wstring ExpandRegistryEnvironmentStrings(const std::wstring& value)
{
    DWORD requiredLength = ExpandEnvironmentStringsW(value.c_str(), nullptr, 0);
    if (requiredLength == 0)
    {
        return value;
    }

    std::wstring expanded(requiredLength, L'\0');
    DWORD writtenLength = ExpandEnvironmentStringsW(value.c_str(), expanded.data(), requiredLength);
    if (writtenLength == 0 || writtenLength > requiredLength)
    {
        return value;
    }

    expanded.resize(writtenLength - 1);
    return expanded;
}

std::optional<std::wstring> ReadRegistryString(HKEY rootKey, REGSAM viewFlags, PCWSTR valueName)
{
    HKEY key = nullptr;
    LONG status = RegOpenKeyExW(rootKey, UninstallKeyPath, 0, KEY_QUERY_VALUE | viewFlags, &key);
    if (status != ERROR_SUCCESS)
    {
        return std::nullopt;
    }

    DWORD type = 0;
    DWORD byteCount = 0;
    status = RegQueryValueExW(key, valueName, nullptr, &type, nullptr, &byteCount);
    if (status != ERROR_SUCCESS || byteCount == 0 || (type != REG_SZ && type != REG_EXPAND_SZ))
    {
        RegCloseKey(key);
        return std::nullopt;
    }

    std::wstring value(byteCount / sizeof(WCHAR), L'\0');
    status = RegQueryValueExW(key, valueName, nullptr, &type, reinterpret_cast<BYTE*>(value.data()), &byteCount);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS)
    {
        return std::nullopt;
    }

    while (!value.empty() && value.back() == L'\0')
    {
        value.pop_back();
    }

    value = Trim(type == REG_EXPAND_SZ ? ExpandRegistryEnvironmentStrings(value) : value);
    return value.empty() ? std::nullopt : std::optional<std::wstring>(value);
}

std::optional<std::wstring> ReadInnoUninstallCommand()
{
    const std::array<HKEY, 2> rootKeys = {
        HKEY_LOCAL_MACHINE,
        HKEY_CURRENT_USER,
    };

    const std::array<REGSAM, 3> registryViews = {
        KEY_WOW64_64KEY,
        0,
        KEY_WOW64_32KEY,
    };

    for (HKEY rootKey : rootKeys)
    {
        for (REGSAM registryView : registryViews)
        {
            if (std::optional<std::wstring> command = ReadRegistryString(rootKey, registryView, L"UninstallString"))
            {
                return command;
            }
        }
    }

    return std::nullopt;
}

std::wstring QuoteArgument(std::wstring_view argument)
{
    bool needsQuotes = argument.empty();
    for (WCHAR ch : argument)
    {
        if (iswspace(ch) || ch == L'"')
        {
            needsQuotes = true;
            break;
        }
    }

    if (!needsQuotes)
    {
        return std::wstring(argument);
    }

    std::wstring quoted;
    quoted.push_back(L'"');

    size_t backslashCount = 0;
    for (WCHAR ch : argument)
    {
        if (ch == L'\\')
        {
            ++backslashCount;
            continue;
        }

        if (ch == L'"')
        {
            quoted.append(backslashCount * 2 + 1, L'\\');
            quoted.push_back(ch);
        }
        else
        {
            quoted.append(backslashCount, L'\\');
            quoted.push_back(ch);
        }

        backslashCount = 0;
    }

    quoted.append(backslashCount * 2, L'\\');
    quoted.push_back(L'"');
    return quoted;
}

std::wstring JoinPath(const std::wstring& directory, PCWSTR name)
{
    if (directory.empty())
    {
        return std::wstring(name);
    }

    std::wstring path = directory;
    if (path.back() != L'\\' && path.back() != L'/')
    {
        path.push_back(L'\\');
    }
    path.append(name);
    return path;
}

std::wstring DirectoryName(const std::wstring& path)
{
    size_t separator = path.find_last_of(L"\\/");
    return separator == std::wstring::npos ? std::wstring() : path.substr(0, separator);
}

std::wstring ModuleFilePath()
{
    DWORD bufferLength = MAX_PATH;

    for (;;)
    {
        std::wstring path(bufferLength, L'\0');
        DWORD length = GetModuleFileNameW(nullptr, path.data(), bufferLength);
        if (length == 0)
        {
            return std::wstring();
        }

        if (length < bufferLength)
        {
            path.resize(length);
            return path;
        }

        bufferLength *= 2;
    }
}

std::wstring EnvironmentVariable(PCWSTR name)
{
    DWORD requiredLength = GetEnvironmentVariableW(name, nullptr, 0);
    if (requiredLength == 0)
    {
        return std::wstring();
    }

    std::wstring value(requiredLength, L'\0');
    DWORD writtenLength = GetEnvironmentVariableW(name, value.data(), requiredLength);
    if (writtenLength == 0 || writtenLength >= requiredLength)
    {
        return std::wstring();
    }

    value.resize(writtenLength);
    return value;
}

std::wstring FallbackUninstallCommand()
{
    std::wstring moduleDirectory = DirectoryName(ModuleFilePath());
    std::wstring sameDirectoryUninstaller = JoinPath(moduleDirectory, L"unins000.exe");
    if (FileExists(sameDirectoryUninstaller))
    {
        return QuoteArgument(sameDirectoryUninstaller);
    }

    for (PCWSTR variableName : { L"ProgramW6432", L"ProgramFiles" })
    {
        std::wstring programFiles = EnvironmentVariable(variableName);
        if (programFiles.empty())
        {
            continue;
        }

        std::wstring programFilesUninstaller = JoinPath(JoinPath(programFiles, ProductName), L"unins000.exe");
        if (FileExists(programFilesUninstaller))
        {
            return QuoteArgument(programFilesUninstaller);
        }
    }

    return QuoteArgument(sameDirectoryUninstaller);
}

std::vector<std::wstring> ForwardedArguments()
{
    int argumentCount = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (arguments == nullptr)
    {
        return {};
    }

    std::vector<std::wstring> result;
    for (int index = 1; index < argumentCount; ++index)
    {
        result.emplace_back(arguments[index]);
    }

    LocalFree(arguments);
    return result;
}

std::wstring BuildUninstallCommandLine()
{
    std::wstring commandLine = ReadInnoUninstallCommand().value_or(FallbackUninstallCommand());
    for (const std::wstring& argument : ForwardedArguments())
    {
        commandLine.push_back(L' ');
        commandLine.append(QuoteArgument(argument));
    }
    return commandLine;
}

std::wstring FormatWin32Error(DWORD error)
{
    PWSTR message = nullptr;
    DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error,
        0,
        reinterpret_cast<PWSTR>(&message),
        0,
        nullptr);

    if (length == 0 || message == nullptr)
    {
        return L"Error " + std::to_wstring(error);
    }

    std::wstring result(message, length);
    LocalFree(message);
    return Trim(result);
}

void ShowError(const std::wstring& message)
{
    MessageBoxW(nullptr, message.c_str(), ProductName, MB_OK | MB_ICONERROR);
}

int RunUninstaller(const std::wstring& commandLine)
{
    STARTUPINFOW startupInfo = {};
    startupInfo.cb = sizeof(startupInfo);

    PROCESS_INFORMATION processInfo = {};

    std::wstring mutableCommandLine = commandLine;
    if (!CreateProcessW(
            nullptr,
            mutableCommandLine.data(),
            nullptr,
            nullptr,
            FALSE,
            0,
            nullptr,
            nullptr,
            &startupInfo,
            &processInfo))
    {
        DWORD error = GetLastError();
        ShowError(L"Could not start the Jyutping uninstaller.\n\n" + FormatWin32Error(error));
        return static_cast<int>(HRESULT_FROM_WIN32(error));
    }

    WaitForSingleObject(processInfo.hProcess, INFINITE);

    DWORD exitCode = 1;
    GetExitCodeProcess(processInfo.hProcess, &exitCode);

    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);

    return static_cast<int>(exitCode);
}

} // namespace

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    return RunUninstaller(BuildUninstallCommandLine());
}
