#pragma once

#include <windows.h>

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

struct ImeSettings
{
    DWORD version = 1;
    InputMethodMode inputMethodMode = InputMethodMode::Cantonese;
    CharacterForm characterForm = CharacterForm::HalfWidth;
    PunctuationForm punctuationForm = PunctuationForm::Cantonese;
};

class SettingsStore
{
public:
    ImeSettings Load() const;
    bool Save(const ImeSettings& settings) const;
    bool SaveInputMethodMode(InputMethodMode mode) const;
    bool SaveCharacterForm(CharacterForm form) const;
    bool SavePunctuationForm(PunctuationForm form) const;

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
