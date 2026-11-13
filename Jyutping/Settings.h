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

// Identifies one user setting, so that storage and cross-process sync can treat all settings uniformly.
enum class SettingKind : UINT
{
    InputMethodMode = 0,
    CharacterForm,
    PunctuationForm,
    CharacterVariant,
    CandidatePageSize,
    CandidateFontSize,
    CandidateNumberFontSize,
    CandidateCommentFontSize
};

inline constexpr UINT SettingKindCount = 8;

DWORD SettingValue(const ImeSettings& settings, SettingKind kind);
// Validates rawValue and stores it into settings. Returns false, leaving settings untouched, when the value is invalid.
bool SetSettingValue(ImeSettings& settings, SettingKind kind, DWORD rawValue);
bool AreSettingsEqual(const ImeSettings& left, const ImeSettings& right);

struct SettingsLoadResult
{
    ImeSettings settings;
    // True when the values come from the real per-user registry (or are the genuine defaults of a fresh install).
    // False when this process only sees a sandboxed or virtualized view, so the values must not be shared with other processes.
    bool isAuthoritative = false;
};

// Durable settings storage under HKCU\Software\Jyutping\Preferences.
//
// The IME runs inside every client process, and not every process sees the real registry:
// - Packaged desktop apps (MSIX, such as Notepad) get copy-on-write HKCU virtualization. Their writes land in a private
//   per-app hive, and once a key is copied there they never see later changes to the real key again.
// - AppContainer apps cannot write HKCU, and can read only keys that grant them access.
// So this store writes only from processes that see the real registry, and grants app containers read access to the key.
// Live values are shared between processes through the TSF global compartments (see GlobalSettings.h).
class SettingsStore
{
public:
    SettingsStore();

    // Loads the persisted settings. In a process that can persist, this also migrates the legacy settings key.
    SettingsLoadResult Load() const;
    // Writes every setting that differs from the stored value.
    bool Save(const ImeSettings& settings) const;
    // Writes one setting if it differs from the stored value.
    bool SaveSetting(SettingKind kind, DWORD value) const;
    // False in packaged and AppContainer processes, whose registry writes never reach the real per-user registry.
    bool CanPersist() const { return _canPersist; }

private:
    bool _canPersist;
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
