#pragma once

#include <Windows.h>
#include <Sddl.h>

#include <memory>
#include <string>

namespace JyutpingRpcLocal {

struct Identity
{
    alignas(TOKEN_USER) BYTE user[(sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE + 7) & ~7u];
    alignas(TOKEN_GROUPS) BYTE logon[(sizeof(TOKEN_GROUPS) + SECURITY_MAX_SID_SIZE + 7) & ~7u];
    DWORD session = 0;

    PSID UserSid() const { return reinterpret_cast<const TOKEN_USER*>(user)->User.Sid; }
    PSID LogonSid() const { return reinterpret_cast<const TOKEN_GROUPS*>(logon)->Groups[0].Sid; }

    DWORD Read(HANDLE token)
    {
        DWORD returned = 0;
        if (!GetTokenInformation(token, TokenSessionId, &session, sizeof(session), &returned) ||
            !GetTokenInformation(token, TokenUser, user, sizeof(user), &returned) ||
            !GetTokenInformation(token, TokenLogonSid, logon, sizeof(logon), &returned))
        {
            return GetLastError();
        }
        return reinterpret_cast<const TOKEN_GROUPS*>(logon)->GroupCount == 1 ? ERROR_SUCCESS : ERROR_NO_SUCH_LOGON_SESSION;
    }

    DWORD ReadProcess()
    {
        HANDLE handle = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &handle))
        {
            return GetLastError();
        }
        std::unique_ptr<void, decltype(&CloseHandle)> token(handle, CloseHandle);
        return Read(token.get());
    }
};

inline DWORD SidString(PSID sid, std::wstring& result)
{
    wchar_t* raw = nullptr;
    if (!ConvertSidToStringSidW(sid, &raw))
    {
        return GetLastError();
    }
    std::unique_ptr<wchar_t, decltype(&LocalFree)> text(raw, LocalFree);
    result.assign(text.get());
    return ERROR_SUCCESS;
}

inline std::wstring Endpoint(DWORD session, const std::wstring& logonSid)
{
    return L"Jyutping.Rpc.v1." + std::to_wstring(session) + L"." + logonSid;
}

inline unsigned short* RpcString(const wchar_t* text)
{
    return reinterpret_cast<unsigned short*>(const_cast<wchar_t*>(text));
}

} // namespace JyutpingRpcLocal
