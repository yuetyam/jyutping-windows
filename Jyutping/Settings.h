#pragma once

#include <windows.h>

enum class CharacterStandard : int;

inline constexpr DWORD MinimumCandidateFontSize = 11;
inline constexpr DWORD MaximumCandidateFontSize = 24;
inline constexpr DWORD DefaultCandidateFontSize = 16;
inline constexpr DWORD DefaultCandidateNumberFontSize = 13;
inline constexpr DWORD DefaultCandidateCommentFontSize = 13;

enum class InputMethodMode : DWORD
{
    Cantonese = 1,
    ABC = 2
};

enum class CharacterForm : DWORD
{
    HalfWidth = 1,
    FullWidth = 2
};

enum class PunctuationForm : DWORD
{
    Cantonese = 1,
    English = 2
};

enum class CharacterVariant : DWORD
{
    Traditional = 1,
    HongKong = 2,
    Taiwan = 3,
    Simplified = 4
};

struct ImeSettings
{
    DWORD version = 1;
    InputMethodMode inputMethodMode = InputMethodMode::Cantonese;
    CharacterForm characterForm = CharacterForm::HalfWidth;
    PunctuationForm punctuationForm = PunctuationForm::Cantonese;
    CharacterVariant characterVariant = CharacterVariant::Traditional;
    DWORD candidatePageSize = 7;
    DWORD candidateFontSize = DefaultCandidateFontSize;
    DWORD candidateNumberFontSize = DefaultCandidateNumberFontSize;
    DWORD candidateCommentFontSize = DefaultCandidateCommentFontSize;
};

class SettingsStore
{
public:
    ImeSettings Load() const;
    bool Save(const ImeSettings& settings) const;
    bool SaveInputMethodMode(InputMethodMode mode) const;
    bool SaveCharacterForm(CharacterForm form) const;
    bool SavePunctuationForm(PunctuationForm form) const;
    bool SaveCharacterVariant(CharacterVariant variant) const;
    bool SaveCandidatePageSize(DWORD pageSize) const;
    bool SaveCandidateFontSize(DWORD fontSize) const;
    bool SaveCandidateNumberFontSize(DWORD fontSize) const;
    bool SaveCandidateCommentFontSize(DWORD fontSize) const;

private:
    bool ReadDWORD(_In_z_ PCWSTR valueName, _Out_ DWORD& value) const;
    bool WriteDWORD(_In_z_ PCWSTR valueName, DWORD value) const;
};

InputMethodMode InputMethodModeFromKeyboardOpen(BOOL isOpen);
BOOL KeyboardOpenFromInputMethodMode(InputMethodMode mode);
CharacterForm CharacterFormFromFullWidth(BOOL isFullWidth);
BOOL FullWidthFromCharacterForm(CharacterForm form);
PunctuationForm PunctuationFormFromCantonesePunctuation(BOOL isCantonesePunctuation);
BOOL CantonesePunctuationFromPunctuationForm(PunctuationForm form);
CharacterVariant CharacterVariantFromRawValue(DWORD value);
CharacterStandard CharacterStandardFromCharacterVariant(CharacterVariant variant);
DWORD CandidatePageSizeFromRawValue(DWORD value);
DWORD CandidateFontSizeFromRawValue(DWORD value);
DWORD CandidateNumberFontSizeFromRawValue(DWORD value);
DWORD CandidateCommentFontSizeFromRawValue(DWORD value);
