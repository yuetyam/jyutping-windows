#include "Private.h"
#include "Settings.h"
#include "RegKey.h"

namespace {

constexpr PCWSTR SettingsKeyPath = L"Software\\Jyutping\\Settings";
constexpr PCWSTR VersionValueName = L"Version";
constexpr PCWSTR InputMethodModeValueName = L"InputMethodMode";
constexpr DWORD CurrentSettingsVersion = 1;

bool IsValidInputMethodMode(DWORD value)
{
    return value == static_cast<DWORD>(InputMethodMode::Cantonese) ||
        value == static_cast<DWORD>(InputMethodMode::ABC);
}

} // namespace

ImeSettings SettingsStore::Load() const
{
    ImeSettings settings;

    DWORD version = 0;
    if (ReadDWORD(VersionValueName, version))
    {
        settings.version = version;
    }

    DWORD inputMethodMode = 0;
    if (ReadDWORD(InputMethodModeValueName, inputMethodMode) && IsValidInputMethodMode(inputMethodMode))
    {
        settings.inputMethodMode = static_cast<InputMethodMode>(inputMethodMode);
    }

    return settings;
}

bool SettingsStore::Save(const ImeSettings& settings) const
{
    CRegKey key;
    if (key.Create(HKEY_CURRENT_USER, SettingsKeyPath, REG_NONE, REG_OPTION_NON_VOLATILE, KEY_READ | KEY_WRITE) != ERROR_SUCCESS)
    {
        return false;
    }

    if (key.SetDWORDValue(VersionValueName, CurrentSettingsVersion) != ERROR_SUCCESS)
    {
        return false;
    }

    return key.SetDWORDValue(InputMethodModeValueName, static_cast<DWORD>(settings.inputMethodMode)) == ERROR_SUCCESS;
}

bool SettingsStore::SaveInputMethodMode(InputMethodMode mode) const
{
    ImeSettings settings = Load();
    settings.inputMethodMode = mode;
    return Save(settings);
}

bool SettingsStore::ReadDWORD(_In_z_ PCWSTR valueName, _Out_ DWORD& value) const
{
    CRegKey key;
    if (key.Open(HKEY_CURRENT_USER, SettingsKeyPath, KEY_READ) != ERROR_SUCCESS)
    {
        return false;
    }

    return key.QueryDWORDValue(valueName, value) == ERROR_SUCCESS;
}

bool SettingsStore::WriteDWORD(_In_z_ PCWSTR valueName, DWORD value) const
{
    CRegKey key;
    if (key.Create(HKEY_CURRENT_USER, SettingsKeyPath, REG_NONE, REG_OPTION_NON_VOLATILE, KEY_READ | KEY_WRITE) != ERROR_SUCCESS)
    {
        return false;
    }

    return key.SetDWORDValue(valueName, value) == ERROR_SUCCESS;
}

InputMethodMode InputMethodModeFromKeyboardOpen(BOOL isOpen)
{
    return isOpen ? InputMethodMode::Cantonese : InputMethodMode::ABC;
}

BOOL KeyboardOpenFromInputMethodMode(InputMethodMode mode)
{
    return mode == InputMethodMode::Cantonese ? TRUE : FALSE;
}
