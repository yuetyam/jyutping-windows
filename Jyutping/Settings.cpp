#include "Private.h"
#include "Settings.h"
#include "CharacterStandard.h"
#include "RegKey.h"

namespace {

constexpr PCWSTR SettingsKeyPath = L"Software\\Jyutping\\Settings";
constexpr PCWSTR VersionValueName = L"Version";
constexpr PCWSTR InputMethodModeValueName = L"InputMethodMode";
constexpr PCWSTR CharacterFormValueName = L"CharacterForm";
constexpr PCWSTR PunctuationFormValueName = L"PunctuationForm";
constexpr PCWSTR CharacterVariantValueName = L"CharacterVariant";
constexpr PCWSTR CandidatePageSizeValueName = L"CandidatePageSize";
constexpr DWORD CurrentSettingsVersion = 1;
constexpr DWORD DefaultCandidatePageSize = 7;
constexpr DWORD MinimumCandidatePageSize = 1;
constexpr DWORD MaximumCandidatePageSize = 10;

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

    DWORD characterForm = 0;
    if (ReadDWORD(CharacterFormValueName, characterForm) && IsValidCharacterForm(characterForm))
    {
        settings.characterForm = static_cast<CharacterForm>(characterForm);
    }

    DWORD punctuationForm = 0;
    if (ReadDWORD(PunctuationFormValueName, punctuationForm) && IsValidPunctuationForm(punctuationForm))
    {
        settings.punctuationForm = static_cast<PunctuationForm>(punctuationForm);
    }

    DWORD characterVariant = 0;
    if (ReadDWORD(CharacterVariantValueName, characterVariant) && IsValidCharacterVariant(characterVariant))
    {
        settings.characterVariant = static_cast<CharacterVariant>(characterVariant);
    }

    DWORD candidatePageSize = 0;
    if (ReadDWORD(CandidatePageSizeValueName, candidatePageSize) && IsValidCandidatePageSize(candidatePageSize))
    {
        settings.candidatePageSize = candidatePageSize;
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

    if (key.SetDWORDValue(InputMethodModeValueName, static_cast<DWORD>(settings.inputMethodMode)) != ERROR_SUCCESS)
    {
        return false;
    }

    if (key.SetDWORDValue(CharacterFormValueName, static_cast<DWORD>(settings.characterForm)) != ERROR_SUCCESS)
    {
        return false;
    }

    if (key.SetDWORDValue(PunctuationFormValueName, static_cast<DWORD>(settings.punctuationForm)) != ERROR_SUCCESS)
    {
        return false;
    }

    if (key.SetDWORDValue(CharacterVariantValueName, static_cast<DWORD>(settings.characterVariant)) != ERROR_SUCCESS)
    {
        return false;
    }

    return key.SetDWORDValue(CandidatePageSizeValueName, CandidatePageSizeFromRawValue(settings.candidatePageSize)) == ERROR_SUCCESS;
}

bool SettingsStore::SaveInputMethodMode(InputMethodMode mode) const
{
    ImeSettings settings = Load();
    settings.inputMethodMode = mode;
    return Save(settings);
}

bool SettingsStore::SaveCharacterForm(CharacterForm form) const
{
    ImeSettings settings = Load();
    settings.characterForm = form;
    return Save(settings);
}

bool SettingsStore::SavePunctuationForm(PunctuationForm form) const
{
    ImeSettings settings = Load();
    settings.punctuationForm = form;
    return Save(settings);
}

bool SettingsStore::SaveCharacterVariant(CharacterVariant variant) const
{
    ImeSettings settings = Load();
    settings.characterVariant = variant;
    return Save(settings);
}

bool SettingsStore::SaveCandidatePageSize(DWORD pageSize) const
{
    ImeSettings settings = Load();
    settings.candidatePageSize = CandidatePageSizeFromRawValue(pageSize);
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
