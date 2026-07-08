#include "Private.h"
#include "Globals.h"
#include "Logger.h"
#include "Localization.h"
#include "resource.h"

static const WCHAR RegInfo_Prefix_CLSID[] = L"CLSID\\";
static const WCHAR RegInfo_Key_InProSvr32[] = L"InProcServer32";
static const WCHAR RegInfo_Key_ThreadModel[] = L"ThreadingModel";

static const WCHAR TextServiceDescriptionFallback[] = L"Jyutping";

static const GUID SupportCategories[] = {
    GUID_TFCAT_TIP_KEYBOARD,
    GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER,
    GUID_TFCAT_TIPCAP_UIELEMENTENABLED,
    GUID_TFCAT_TIPCAP_SECUREMODE,
    GUID_TFCAT_TIPCAP_COMLESS,
    GUID_TFCAT_TIPCAP_INPUTMODECOMPARTMENT,
    GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT,
    GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT,
};

namespace {

BOOL IsRepeatRegistrationSuccess(HRESULT hr)
{
    return SUCCEEDED(hr) || hr == TF_E_ALREADY_EXISTS || hr == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
}

std::wstring GuidToString(REFGUID guid)
{
    WCHAR buffer[64] = { L'\0' };
    if (StringFromGUID2(guid, buffer, ARRAYSIZE(buffer)) == 0)
    {
        return L"<invalid-guid>";
    }
    return buffer;
}

}

//+---------------------------------------------------------------------------
//
//  RegisterProfiles
//
//----------------------------------------------------------------------------

BOOL RegisterProfiles()
{
    HRESULT hr = S_FALSE;

    ITfInputProcessorProfileMgr *pITfInputProcessorProfileMgr = nullptr;
    hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_INPROC_SERVER,
        IID_ITfInputProcessorProfileMgr, (void**)&pITfInputProcessorProfileMgr);
    if (FAILED(hr))
    {
        Global::Log(L"RegisterProfiles: CoCreateInstance failed: 0x%08lX", static_cast<unsigned long>(hr));
        return FALSE;
    }

    WCHAR achIconFile[MAX_PATH] = {'\0'};
    DWORD cchA = 0;
    cchA = GetModuleFileName(Global::dllInstanceHandle, achIconFile, MAX_PATH);
    if (cchA == 0 || cchA >= MAX_PATH)
    {
        Global::Log(L"RegisterProfiles: GetModuleFileName failed or truncated: length=%lu error=%lu", cchA, GetLastError());
        pITfInputProcessorProfileMgr->Release();
        return FALSE;
    }

    std::wstring textServiceDescription = Localization::LoadStringOrFallback(IDS_TEXTSERVICE_DESC, TextServiceDescriptionFallback);
    size_t lenOfDesc = 0;
    hr = StringCchLength(textServiceDescription.c_str(), STRSAFE_MAX_CCH, &lenOfDesc);
    if (hr != S_OK)
    {
        Global::Log(L"RegisterProfiles: StringCchLength failed: 0x%08lX", static_cast<unsigned long>(hr));
        pITfInputProcessorProfileMgr->Release();
        return FALSE;
    }
    Global::UpdateSystemTheme();
    UINT iconIndex = static_cast<UINT>(Global::GetSystemTheme() == Global::DARK_MODE
        ? Global::TextServiceAltIcoIndex
        : Global::TextServiceIcoIndex);

    hr = pITfInputProcessorProfileMgr->RegisterProfile(Global::JyutpingCLSID,
        TEXTSERVICE_LANGID,
        Global::JyutpingGuidProfile,
        textServiceDescription.c_str(),
        static_cast<ULONG>(lenOfDesc),
        achIconFile,
        cchA,
        iconIndex, NULL, 0, TRUE, 0);

    if (!IsRepeatRegistrationSuccess(hr))
    {
        Global::Log(L"RegisterProfiles: RegisterProfile failed: 0x%08lX", static_cast<unsigned long>(hr));
        pITfInputProcessorProfileMgr->Release();
        return FALSE;
    }

    if (hr == TF_E_ALREADY_EXISTS || hr == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS))
    {
        Global::Log(L"RegisterProfiles: profile already exists");
    }

    pITfInputProcessorProfileMgr->Release();
    return TRUE;
}

//+---------------------------------------------------------------------------
//
//  UnregisterProfiles
//
//----------------------------------------------------------------------------

void UnregisterProfiles()
{
    HRESULT hr = S_OK;

    ITfInputProcessorProfileMgr *pITfInputProcessorProfileMgr = nullptr;
    hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_INPROC_SERVER,
        IID_ITfInputProcessorProfileMgr, (void**)&pITfInputProcessorProfileMgr);
    if (FAILED(hr))
    {
        Global::Log(L"UnregisterProfiles: CoCreateInstance failed: 0x%08lX", static_cast<unsigned long>(hr));
        return;
    }

    hr = pITfInputProcessorProfileMgr->UnregisterProfile(Global::JyutpingCLSID, TEXTSERVICE_LANGID, Global::JyutpingGuidProfile, 0);
    if (FAILED(hr))
    {
        Global::Log(L"UnregisterProfiles: UnregisterProfile returned 0x%08lX", static_cast<unsigned long>(hr));
    }
    pITfInputProcessorProfileMgr->Release();
}

//+---------------------------------------------------------------------------
//
//  RegisterCategories
//
//----------------------------------------------------------------------------

BOOL RegisterCategories()
{
    ITfCategoryMgr* pCategoryMgr = nullptr;
    HRESULT hr = S_OK;

    hr = CoCreateInstance(CLSID_TF_CategoryMgr, NULL, CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr, (void**)&pCategoryMgr);
    if (FAILED(hr))
    {
        Global::Log(L"RegisterCategories: CoCreateInstance failed: 0x%08lX", static_cast<unsigned long>(hr));
        return FALSE;
    }

    for (GUID guid : SupportCategories)
    {
        hr = pCategoryMgr->RegisterCategory(Global::JyutpingCLSID, guid, Global::JyutpingCLSID);
        if (!IsRepeatRegistrationSuccess(hr))
        {
            Global::Log(L"RegisterCategories: RegisterCategory failed for %s: 0x%08lX",
                GuidToString(guid).c_str(),
                static_cast<unsigned long>(hr));
            pCategoryMgr->Release();
            return FALSE;
        }
    }

    pCategoryMgr->Release();

    return TRUE;
}

//+---------------------------------------------------------------------------
//
//  UnregisterCategories
//
//----------------------------------------------------------------------------

void UnregisterCategories()
{
    ITfCategoryMgr* pCategoryMgr = nullptr;
    HRESULT hr = S_OK;

    hr = CoCreateInstance(CLSID_TF_CategoryMgr, NULL, CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr, (void**)&pCategoryMgr);
    if (FAILED(hr))
    {
        Global::Log(L"UnregisterCategories: CoCreateInstance failed: 0x%08lX", static_cast<unsigned long>(hr));
        return;
    }

    for (GUID guid : SupportCategories)
    {
        hr = pCategoryMgr->UnregisterCategory(Global::JyutpingCLSID, guid, Global::JyutpingCLSID);
        if (FAILED(hr))
        {
            Global::Log(L"UnregisterCategories: UnregisterCategory returned 0x%08lX for %s",
                static_cast<unsigned long>(hr),
                GuidToString(guid).c_str());
        }
    }

    pCategoryMgr->Release();

    return;
}

//+---------------------------------------------------------------------------
//
// RecurseDeleteKey
//
// RecurseDeleteKey is necessary because on NT RegDeleteKey doesn't work if the
// specified key has subkeys
//----------------------------------------------------------------------------

LONG RecurseDeleteKey(_In_ HKEY hParentKey, _In_ LPCTSTR lpszKey)
{
    HKEY regKeyHandle = nullptr;
    LONG res = 0;
    FILETIME time;
    WCHAR stringBuffer[256] = {'\0'};
    DWORD size = ARRAYSIZE(stringBuffer);

    if (RegOpenKey(hParentKey, lpszKey, &regKeyHandle) != ERROR_SUCCESS)
    {
        return ERROR_SUCCESS;
    }

    res = ERROR_SUCCESS;
    while (RegEnumKeyEx(regKeyHandle, 0, stringBuffer, &size, NULL, NULL, NULL, &time) == ERROR_SUCCESS)
    {
        stringBuffer[ARRAYSIZE(stringBuffer)-1] = '\0';
        res = RecurseDeleteKey(regKeyHandle, stringBuffer);
        if (res != ERROR_SUCCESS)
        {
            break;
        }
        size = ARRAYSIZE(stringBuffer);
    }
    RegCloseKey(regKeyHandle);

    return res == ERROR_SUCCESS ? RegDeleteKey(hParentKey, lpszKey) : res;
}

//+---------------------------------------------------------------------------
//
//  RegisterServer
//
//----------------------------------------------------------------------------

BOOL RegisterServer()
{
    DWORD copiedStringLen = 0;
    HKEY regKeyHandle = nullptr;
    HKEY regSubkeyHandle = nullptr;
    BOOL ret = FALSE;
    WCHAR achIMEKey[ARRAYSIZE(RegInfo_Prefix_CLSID) + CLSID_STRLEN] = {'\0'};
    WCHAR achFileName[MAX_PATH] = {'\0'};

    if (!CLSIDToString(Global::JyutpingCLSID, achIMEKey + ARRAYSIZE(RegInfo_Prefix_CLSID) - 1))
    {
        return FALSE;
    }

    memcpy(achIMEKey, RegInfo_Prefix_CLSID, sizeof(RegInfo_Prefix_CLSID) - sizeof(WCHAR));

    std::wstring textServiceDescription = Localization::LoadStringOrFallback(IDS_TEXTSERVICE_DESC, TextServiceDescriptionFallback);
    size_t textServiceDescriptionLength = 0;
    if (StringCchLength(textServiceDescription.c_str(), STRSAFE_MAX_CCH, &textServiceDescriptionLength) != S_OK)
    {
        return FALSE;
    }

    LONG status = RegCreateKeyEx(HKEY_CLASSES_ROOT, achIMEKey, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &regKeyHandle, &copiedStringLen);
    if (status == ERROR_SUCCESS)
    {
        if (RegSetValueEx(regKeyHandle, NULL, 0, REG_SZ, (const BYTE *)textServiceDescription.c_str(), (static_cast<DWORD>(textServiceDescriptionLength) + 1) * sizeof(WCHAR)) != ERROR_SUCCESS)
        {
            Global::Log(L"RegisterServer: RegSetValueEx description failed for %s: %lu", achIMEKey, GetLastError());
            RegCloseKey(regKeyHandle);
            return FALSE;
        }

        status = RegCreateKeyEx(regKeyHandle, RegInfo_Key_InProSvr32, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &regSubkeyHandle, &copiedStringLen);
        if (status == ERROR_SUCCESS)
        {
            copiedStringLen = GetModuleFileNameW(Global::dllInstanceHandle, achFileName, ARRAYSIZE(achFileName));
            if (copiedStringLen == 0 || copiedStringLen >= ARRAYSIZE(achFileName))
            {
                Global::Log(L"RegisterServer: GetModuleFileName failed or truncated: length=%lu error=%lu", copiedStringLen, GetLastError());
                RegCloseKey(regSubkeyHandle);
                RegCloseKey(regKeyHandle);
                return FALSE;
            }

            copiedStringLen++;
            if (RegSetValueEx(regSubkeyHandle, NULL, 0, REG_SZ, (const BYTE *)achFileName, (copiedStringLen)*sizeof(WCHAR)) != ERROR_SUCCESS)
            {
                Global::Log(L"RegisterServer: RegSetValueEx server path failed for %s: %lu", achIMEKey, GetLastError());
                RegCloseKey(regSubkeyHandle);
                RegCloseKey(regKeyHandle);
                return FALSE;
            }
            if (RegSetValueEx(regSubkeyHandle, RegInfo_Key_ThreadModel, 0, REG_SZ, (const BYTE *)TEXTSERVICE_MODEL, (_countof(TEXTSERVICE_MODEL)) * sizeof(WCHAR)) != ERROR_SUCCESS)
            {
                Global::Log(L"RegisterServer: RegSetValueEx threading model failed for %s: %lu", achIMEKey, GetLastError());
                RegCloseKey(regSubkeyHandle);
                RegCloseKey(regKeyHandle);
                return FALSE;
            }

            ret = TRUE;
            RegCloseKey(regSubkeyHandle);
        }
        else
        {
            Global::Log(L"RegisterServer: RegCreateKeyEx InProcServer32 failed for %s: %ld", achIMEKey, status);
        }
        RegCloseKey(regKeyHandle);
    }
    else
    {
        Global::Log(L"RegisterServer: RegCreateKeyEx failed for %s: %ld", achIMEKey, status);
    }

    return ret;
}

//+---------------------------------------------------------------------------
//
//  UnregisterServer
//
//----------------------------------------------------------------------------

void UnregisterServer()
{
    WCHAR achIMEKey[ARRAYSIZE(RegInfo_Prefix_CLSID) + CLSID_STRLEN] = {'\0'};

    if (!CLSIDToString(Global::JyutpingCLSID, achIMEKey + ARRAYSIZE(RegInfo_Prefix_CLSID) - 1))
    {
        return;
    }

    memcpy(achIMEKey, RegInfo_Prefix_CLSID, sizeof(RegInfo_Prefix_CLSID) - sizeof(WCHAR));

    LONG status = RecurseDeleteKey(HKEY_CLASSES_ROOT, achIMEKey);
    if (status != ERROR_SUCCESS)
    {
        Global::Log(L"UnregisterServer: RecurseDeleteKey returned %ld for %s", status, achIMEKey);
    }
}
