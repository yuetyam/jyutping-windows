#pragma once

#include <windows.h>

enum class InputMethodMode : DWORD
{
    Cantonese = 1,
    ABC = 2
};

struct ImeSettings
{
    DWORD version = 1;
    InputMethodMode inputMethodMode = InputMethodMode::Cantonese;
};

class SettingsStore
{
public:
    ImeSettings Load() const;
    bool Save(const ImeSettings& settings) const;
    bool SaveInputMethodMode(InputMethodMode mode) const;

private:
    bool ReadDWORD(_In_z_ PCWSTR valueName, _Out_ DWORD& value) const;
    bool WriteDWORD(_In_z_ PCWSTR valueName, DWORD value) const;
};

InputMethodMode InputMethodModeFromKeyboardOpen(BOOL isOpen);
BOOL KeyboardOpenFromInputMethodMode(InputMethodMode mode);
