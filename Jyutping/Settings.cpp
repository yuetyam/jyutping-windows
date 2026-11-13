#include "Private.h"
#include "Settings.h"
#include "CharacterStandard.h"
#include "Logger.h"
#include "RegKey.h"

#include <aclapi.h>
#include <appmodel.h>
#include <sddl.h>

namespace {

// Settings live under a dedicated key that no released build has ever written from inside a packaged app.
// Packaged apps that ran older builds hold a stale private copy of the legacy key in their virtualized registry,
// which would otherwise shadow the real values forever.
constexpr PCWSTR PreferencesKeyPath = L"Software\\Jyutping\\Preferences";
constexpr PCWSTR LegacySettingsKeyPath = L"Software\\Jyutping\\Settings";
constexpr PCWSTR VersionValueName = L"Version";
constexpr DWORD CurrentSettingsVersion = 1;
constexpr DWORD DefaultCandidatePageSize = 7;
constexpr DWORD MinimumCandidatePageSize = 1;
constexpr DWORD MaximumCandidatePageSize = 10;

// Indexed by SettingKind.
constexpr PCWSTR SettingValueNames[SettingKindCount] = {
    L"InputMethodMode",
    L"CharacterForm",
    L"PunctuationForm",
    L"CharacterVariant",
    L"CandidatePageSize",
    L"CandidateFontSize",
    L"LabelFontSize",
    L"CommentFontSize",
};

// App containers that may host the IME and need to read the persisted settings.
constexpr PCWSTR AppContainerReaderSids[] = {
    L"S-1-15-2-1", // ALL APPLICATION PACKAGES
    L"S-1-15-2-2", // ALL RESTRICTED APPLICATION PACKAGES
};

bool IsValidInputMethodMode(DWORD value)
{
    return value == static_cast<DWORD>(InputMethodMode::Cantonese) ||
        value == static_cast<DWORD>(InputMethodMode::ABC);
}

bool IsValidCharacterForm(DWORD value)
{
    return value == static_cast<DWORD>(CharacterForm::HalfWidth) ||
        value == static_cast<DWORD>(CharacterForm::FullWidth);
}

bool IsValidPunctuationForm(DWORD value)
{
    return value == static_cast<DWORD>(PunctuationForm::Cantonese) ||
        value == static_cast<DWORD>(PunctuationForm::English);
}

bool IsValidCharacterVariant(DWORD value)
{
    return value == static_cast<DWORD>(CharacterVariant::Traditional) ||
        value == static_cast<DWORD>(CharacterVariant::HongKong) ||
        value == static_cast<DWORD>(CharacterVariant::Taiwan) ||
        value == static_cast<DWORD>(CharacterVariant::Simplified);
}

bool IsValidCandidatePageSize(DWORD value)
{
    return value >= MinimumCandidatePageSize && value <= MaximumCandidatePageSize;
}

bool IsValidCandidateFontSize(DWORD value)
{
    return value >= MinimumCandidateFontSize && value <= MaximumCandidateFontSize;
}

PCWSTR SettingValueName(SettingKind kind)
{
    UINT index = static_cast<UINT>(kind);
    return index < SettingKindCount ? SettingValueNames[index] : nullptr;
}

bool IsRunningInAppContainer()
{
    DWORD isAppContainer = 0;
    DWORD returnLength = 0;
    if (!GetTokenInformation(GetCurrentProcessToken(), TokenIsAppContainer, &isAppContainer, sizeof(isAppContainer), &returnLength))
    {
        return false;
    }
    return isAppContainer != 0;
}

bool HasPackageIdentity()
{
    UINT32 length = 0;
    return GetCurrentPackageFullName(&length, nullptr) != APPMODEL_ERROR_NO_PACKAGE;
}

// Registry writes reach the real per-user hive only from processes without package identity and outside app containers.
bool CanCurrentProcessPersistSettings()
{
    return !IsRunningInAppContainer() && !HasPackageIdentity();
}

void ReadSettings(_In_ HKEY key, _Inout_ ImeSettings& settings)
{
    DWORD version = 0;
    DWORD size = sizeof(version);
    if (RegQueryValueExW(key, VersionValueName, nullptr, nullptr, reinterpret_cast<BYTE*>(&version), &size) == ERROR_SUCCESS)
    {
        settings.version = version;
    }

    for (UINT index = 0; index < SettingKindCount; ++index)
    {
        DWORD value = 0;
        size = sizeof(value);
        DWORD type = REG_NONE;
        if (RegQueryValueExW(key, SettingValueNames[index], nullptr, &type, reinterpret_cast<BYTE*>(&value), &size) == ERROR_SUCCESS &&
            type == REG_DWORD)
        {
            SetSettingValue(settings, static_cast<SettingKind>(index), value);
        }
    }
}

bool ReadSettingsFromKey(_In_z_ PCWSTR keyPath, _Out_ ImeSettings& settings)
{
    settings = ImeSettings();
    CRegKey key;
    if (key.Open(HKEY_CURRENT_USER, keyPath, KEY_READ) != ERROR_SUCCESS)
    {
        return false;
    }
    ReadSettings(key.GetHKEY(), settings);
    return true;
}

bool DaclGrantsRead(_In_opt_ PACL dacl, _In_ PSID sid)
{
    if (dacl == nullptr)
    {
        // A NULL DACL grants everyone full access.
        return true;
    }

    for (DWORD index = 0; index < dacl->AceCount; ++index)
    {
        void* ace = nullptr;
        if (!GetAce(dacl, index, &ace))
        {
            continue;
        }
        const ACE_HEADER* header = static_cast<const ACE_HEADER*>(ace);
        if (header->AceType != ACCESS_ALLOWED_ACE_TYPE)
        {
            continue;
        }
        const ACCESS_ALLOWED_ACE* allowedAce = static_cast<const ACCESS_ALLOWED_ACE*>(ace);
        if ((allowedAce->Mask & KEY_READ) == KEY_READ && EqualSid(const_cast<DWORD*>(&allowedAce->SidStart), sid))
        {
            return true;
        }
    }
    return false;
}

// Grants app containers read access to the settings key, so that the IME hosted by Store apps reads the same settings.
void EnsureAppContainerReadAccess(_In_ HKEY key)
{
    PACL currentDacl = nullptr;
    PSECURITY_DESCRIPTOR securityDescriptor = nullptr;
    if (GetSecurityInfo(key, SE_REGISTRY_KEY, DACL_SECURITY_INFORMATION, nullptr, nullptr, &currentDacl, nullptr, &securityDescriptor) != ERROR_SUCCESS)
    {
        return;
    }

    PSID sids[ARRAYSIZE(AppContainerReaderSids)] = {};
    EXPLICIT_ACCESS_W entries[ARRAYSIZE(AppContainerReaderSids)] = {};
    ULONG entryCount = 0;
    for (size_t index = 0; index < ARRAYSIZE(AppContainerReaderSids); ++index)
    {
        if (!ConvertStringSidToSidW(AppContainerReaderSids[index], &sids[index]))
        {
            continue;
        }
        if (DaclGrantsRead(currentDacl, sids[index]))
        {
            continue;
        }

        EXPLICIT_ACCESS_W& entry = entries[entryCount++];
        entry.grfAccessPermissions = KEY_READ;
        entry.grfAccessMode = GRANT_ACCESS;
        entry.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
        entry.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        entry.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
        entry.Trustee.ptstrName = static_cast<LPWSTR>(sids[index]);
    }

    if (entryCount > 0)
    {
        PACL updatedDacl = nullptr;
        DWORD result = SetEntriesInAclW(entryCount, entries, currentDacl, &updatedDacl);
        if (result == ERROR_SUCCESS)
        {
            result = SetSecurityInfo(key, SE_REGISTRY_KEY, DACL_SECURITY_INFORMATION, nullptr, nullptr, updatedDacl, nullptr);
            LocalFree(updatedDacl);
        }
        if (result != ERROR_SUCCESS)
        {
            Global::Log(L"Settings: unable to grant app containers read access: error=%lu", result);
        }
    }

    for (PSID sid : sids)
    {
        if (sid != nullptr)
        {
            LocalFree(sid);
        }
    }
    LocalFree(securityDescriptor);
}

// Opens the settings key for writing, creating it on first use. Only call this from a process that can persist.
bool CreatePreferencesKey(_Inout_ CRegKey& key)
{
    if (key.Create(HKEY_CURRENT_USER, PreferencesKeyPath, REG_NONE, REG_OPTION_NON_VOLATILE, KEY_READ | KEY_WRITE | WRITE_DAC) == ERROR_SUCCESS)
    {
        // Repair the access once per process, which also covers keys created before the grant existed.
        static volatile LONG hasCheckedAccess = 0;
        if (InterlockedExchange(&hasCheckedAccess, 1) == 0)
        {
            EnsureAppContainerReadAccess(key.GetHKEY());
        }
        return true;
    }

    // Still save the settings if changing the access is not allowed.
    return key.Create(HKEY_CURRENT_USER, PreferencesKeyPath, REG_NONE, REG_OPTION_NON_VOLATILE, KEY_READ | KEY_WRITE) == ERROR_SUCCESS;
}

bool WriteChangedValue(_In_ HKEY key, _In_z_ PCWSTR valueName, DWORD value)
{
    DWORD storedValue = 0;
    DWORD size = sizeof(storedValue);
    DWORD type = REG_NONE;
    if (RegQueryValueExW(key, valueName, nullptr, &type, reinterpret_cast<BYTE*>(&storedValue), &size) == ERROR_SUCCESS &&
        type == REG_DWORD && storedValue == value)
    {
        return true;
    }
    return RegSetValueExW(key, valueName, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value)) == ERROR_SUCCESS;
}

} // namespace

DWORD SettingValue(const ImeSettings& settings, SettingKind kind)
{
    switch (kind)
    {
    case SettingKind::InputMethodMode:
        return static_cast<DWORD>(settings.inputMethodMode);
    case SettingKind::CharacterForm:
        return static_cast<DWORD>(settings.characterForm);
    case SettingKind::PunctuationForm:
        return static_cast<DWORD>(settings.punctuationForm);
    case SettingKind::CharacterVariant:
        return static_cast<DWORD>(settings.characterVariant);
    case SettingKind::CandidatePageSize:
        return settings.candidatePageSize;
    case SettingKind::CandidateFontSize:
        return settings.candidateFontSize;
    case SettingKind::CandidateNumberFontSize:
        return settings.candidateNumberFontSize;
    case SettingKind::CandidateCommentFontSize:
        return settings.candidateCommentFontSize;
    default:
        return 0;
    }
}

bool SetSettingValue(ImeSettings& settings, SettingKind kind, DWORD rawValue)
{
    switch (kind)
    {
    case SettingKind::InputMethodMode:
        if (!IsValidInputMethodMode(rawValue))
        {
            return false;
        }
        settings.inputMethodMode = static_cast<InputMethodMode>(rawValue);
        return true;
    case SettingKind::CharacterForm:
        if (!IsValidCharacterForm(rawValue))
        {
            return false;
        }
        settings.characterForm = static_cast<CharacterForm>(rawValue);
        return true;
    case SettingKind::PunctuationForm:
        if (!IsValidPunctuationForm(rawValue))
        {
            return false;
        }
        settings.punctuationForm = static_cast<PunctuationForm>(rawValue);
        return true;
    case SettingKind::CharacterVariant:
        if (!IsValidCharacterVariant(rawValue))
        {
            return false;
        }
        settings.characterVariant = static_cast<CharacterVariant>(rawValue);
        return true;
    case SettingKind::CandidatePageSize:
        if (!IsValidCandidatePageSize(rawValue))
        {
            return false;
        }
        settings.candidatePageSize = rawValue;
        return true;
    case SettingKind::CandidateFontSize:
        if (!IsValidCandidateFontSize(rawValue))
        {
            return false;
        }
        settings.candidateFontSize = rawValue;
        return true;
    case SettingKind::CandidateNumberFontSize:
        if (!IsValidCandidateFontSize(rawValue))
        {
            return false;
        }
        settings.candidateNumberFontSize = rawValue;
        return true;
    case SettingKind::CandidateCommentFontSize:
        if (!IsValidCandidateFontSize(rawValue))
        {
            return false;
        }
        settings.candidateCommentFontSize = rawValue;
        return true;
    default:
        return false;
    }
}

bool AreSettingsEqual(const ImeSettings& left, const ImeSettings& right)
{
    for (UINT index = 0; index < SettingKindCount; ++index)
    {
        SettingKind kind = static_cast<SettingKind>(index);
        if (SettingValue(left, kind) != SettingValue(right, kind))
        {
            return false;
        }
    }
    return true;
}

SettingsStore::SettingsStore() :
    _canPersist(CanCurrentProcessPersistSettings())
{
}

SettingsLoadResult SettingsStore::Load() const
{
    SettingsLoadResult result;

    if (ReadSettingsFromKey(PreferencesKeyPath, result.settings))
    {
        // Sandboxed processes never write this key, so even they see the real values here.
        result.isAuthoritative = true;
        return result;
    }

    // The settings key does not exist yet, or this app container may not read it.
    ImeSettings legacySettings;
    bool hasLegacySettings = ReadSettingsFromKey(LegacySettingsKeyPath, legacySettings);
    if (hasLegacySettings)
    {
        result.settings = legacySettings;
    }

    if (_canPersist)
    {
        // This process sees the real registry: either migrate the legacy settings, or the defaults are genuine.
        if (hasLegacySettings)
        {
            Save(legacySettings);
        }
        result.isAuthoritative = true;
    }
    return result;
}

bool SettingsStore::Save(const ImeSettings& settings) const
{
    if (!_canPersist)
    {
        return false;
    }

    CRegKey key;
    if (!CreatePreferencesKey(key))
    {
        return false;
    }

    bool isSaved = WriteChangedValue(key.GetHKEY(), VersionValueName, CurrentSettingsVersion);
    for (UINT index = 0; index < SettingKindCount; ++index)
    {
        isSaved = WriteChangedValue(key.GetHKEY(), SettingValueNames[index], SettingValue(settings, static_cast<SettingKind>(index))) && isSaved;
    }
    return isSaved;
}

bool SettingsStore::SaveSetting(SettingKind kind, DWORD value) const
{
    PCWSTR valueName = SettingValueName(kind);
    if (!_canPersist || valueName == nullptr)
    {
        return false;
    }

    CRegKey key;
    if (!CreatePreferencesKey(key))
    {
        return false;
    }

    WriteChangedValue(key.GetHKEY(), VersionValueName, CurrentSettingsVersion);
    return WriteChangedValue(key.GetHKEY(), valueName, value);
}

InputMethodMode InputMethodModeFromKeyboardOpen(BOOL isOpen)
{
    return isOpen ? InputMethodMode::Cantonese : InputMethodMode::ABC;
}

BOOL KeyboardOpenFromInputMethodMode(InputMethodMode mode)
{
    return mode == InputMethodMode::Cantonese ? TRUE : FALSE;
}

CharacterForm CharacterFormFromFullWidth(BOOL isFullWidth)
{
    return isFullWidth ? CharacterForm::FullWidth : CharacterForm::HalfWidth;
}

BOOL FullWidthFromCharacterForm(CharacterForm form)
{
    return form == CharacterForm::FullWidth ? TRUE : FALSE;
}

PunctuationForm PunctuationFormFromCantonesePunctuation(BOOL isCantonesePunctuation)
{
    return isCantonesePunctuation ? PunctuationForm::Cantonese : PunctuationForm::English;
}

BOOL CantonesePunctuationFromPunctuationForm(PunctuationForm form)
{
    return form == PunctuationForm::Cantonese ? TRUE : FALSE;
}

CharacterVariant CharacterVariantFromRawValue(DWORD value)
{
    return IsValidCharacterVariant(value) ? static_cast<CharacterVariant>(value) : CharacterVariant::Traditional;
}

CharacterStandard CharacterStandardFromCharacterVariant(CharacterVariant variant)
{
    switch (variant)
    {
    case CharacterVariant::HongKong:
        return CharacterStandard::HongKong;
    case CharacterVariant::Taiwan:
        return CharacterStandard::Taiwan;
    case CharacterVariant::Simplified:
        return CharacterStandard::Mutilated;
    case CharacterVariant::Traditional:
    default:
        return CharacterStandard::Preset;
    }
}

DWORD CandidatePageSizeFromRawValue(DWORD value)
{
    return IsValidCandidatePageSize(value) ? value : DefaultCandidatePageSize;
}

DWORD CandidateFontSizeFromRawValue(DWORD value)
{
    return IsValidCandidateFontSize(value) ? value : DefaultCandidateFontSize;
}

DWORD CandidateNumberFontSizeFromRawValue(DWORD value)
{
    return IsValidCandidateFontSize(value) ? value : DefaultCandidateNumberFontSize;
}

DWORD CandidateCommentFontSizeFromRawValue(DWORD value)
{
    return IsValidCandidateFontSize(value) ? value : DefaultCandidateCommentFontSize;
}
