#pragma once

#include "sal.h"
#include "KeyHandlerEditSession.h"
#include "JyutpingBaseStructure.h"
#include "Compartment.h"
#include "define.h"
#include "InputEngine.h"
#include "Settings.h"
#include "VirtualInputKey.h"

#include <optional>
#include <string>
#include <vector>

class CCompositionProcessorEngine
{
public:
    CCompositionProcessorEngine(void);
    ~CCompositionProcessorEngine(void);

    BOOL SetupLanguageProfile(LANGID langid, REFGUID guidLanguageProfile, _In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId, BOOL isSecureMode, BOOL isComLessMode);

    // Get language profile.
    GUID GetLanguageProfile(LANGID *plangid)
    {
        *plangid = _langid;
        return _guidProfile;
    }
    // Get locale
    LCID GetLocale()
    {
        return MAKELCID(_langid, SORT_DEFAULT);
    }

    BOOL IsVirtualKeyNeed(UINT uCode, _In_reads_(1) WCHAR *pwch, BOOL fComposing, CANDIDATE_MODE candidateMode, _Out_opt_ _KEYSTROKE_STATE *pKeyState);

    BOOL AddVirtualKey(WCHAR wch);
    void RemoveVirtualKey(DWORD_PTR dwIndex);
    void PurgeVirtualKey();

    DWORD_PTR GetVirtualKeyLength() { return _keystrokeBuffer.GetLength(); }
    WCHAR GetVirtualKey(DWORD_PTR dwIndex);
    std::wstring GetRawInputText() const;
    std::wstring GetCandidateTailInputText(DWORD_PTR inputCount) const;
    BOOL SetRawInputText(const std::wstring& inputText);

    void GetReadingStrings(_Inout_ CJyutpingArray<CStringRange> *pReadingStrings);
    void GetCandidateList(_Inout_ CJyutpingArray<CCandidateListItem> *pCandidateList);
    void AppendSelectedCandidateForMemory(UINT candidateIndex);
    void CommitSelectedCandidateForMemory(UINT candidateIndex);
    BOOL ForgetCandidateFromMemory(UINT candidateIndex);
    void ClearSelectedCandidateMemory();

    // Preserved key handler
    void OnPreservedKey(REFGUID rguid, _Out_ BOOL *pIsEaten, _In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId);
    BOOL IsCharacterVariantPreservedKey(REFGUID rguid) const;

    // Punctuation
    BOOL IsPunctuation(WCHAR wch);
    WCHAR GetPunctuation(WCHAR wch);

    BOOL IsCharacterFormConvertible(WCHAR wch);
    BOOL IsReverseLookupBuffer() const;

    // Language bar control
    void SetLanguageBarStatus(DWORD status, BOOL isSet);

    void ConversionModeCompartmentUpdated(_In_ ITfThreadMgr *pThreadMgr);
    void ApplyPersistedSettings(_In_ ITfThreadMgr *pThreadMgr);

    void ShowAllLanguageBarIcons();
    void HideAllLanguageBarIcons();

    inline CCandidateRange *GetCandidateListIndexRange() { return &_candidateListIndexRange; }
    inline UINT GetCandidateListPhraseModifier() { return _candidateListPhraseModifier; }

private:
    void InitKeyStrokeTable();
    BOOL InitLanguageBar(_In_ CLangBarItemButton *pLanguageBar, _In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId, REFGUID guidCompartment);

    struct _KEYSTROKE;
    BOOL IsVirtualKeyKeystrokeComposition(UINT uCode, _Out_opt_ _KEYSTROKE_STATE *pKeyState, KEYSTROKE_FUNCTION function);
    BOOL IsVirtualKeyKeystrokeCandidate(UINT uCode, _In_ _KEYSTROKE_STATE *pKeyState, CANDIDATE_MODE candidateMode, _Out_ BOOL *pfRetCode, _In_ CJyutpingArray<_KEYSTROKE> *pKeystrokeMetric);
    BOOL IsKeystrokeRange(UINT uCode, _Out_ _KEYSTROKE_STATE *pKeyState, CANDIDATE_MODE candidateMode);

    void SetupKeystroke();
    void SetupPreserved(_In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId);
    void SetupConfiguration();
    void SetupLanguageBar(_In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId, BOOL isSecureMode);
    void SetKeystrokeTable(_Inout_ CJyutpingArray<_KEYSTROKE> *pKeystroke);
    void SetupPunctuationPair();
    void CreateLanguageBarButton(DWORD dwEnable, GUID guidLangBar, _In_z_ LPCWSTR pwszDescriptionValue, _In_z_ LPCWSTR pwszTooltipValue, DWORD dwOnIconIndex, DWORD dwOffIconIndex, _Outptr_result_maybenull_ CLangBarItemButton **ppLangBarItemButton, BOOL isSecureMode);
    void SetInitialCandidateListRange();
	void InitializeJyutpingCompartment(_In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId);

    class XPreservedKey;
    void SetPreservedKey(const CLSID clsid, TF_PRESERVEDKEY & tfPreservedKey, _In_z_ LPCWSTR pwszDescription, _Out_ XPreservedKey *pXPreservedKey);
    BOOL InitPreservedKey(_In_ XPreservedKey *pXPreservedKey, _In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId);
    BOOL CheckShiftKeyOnly(_In_ CJyutpingArray<TF_PRESERVEDKEY> *pTSFPreservedKeyTable);

    static HRESULT CompartmentCallback(_In_ void *pv, REFGUID guidCompartment);
    void PrivateCompartmentsUpdated(_In_ ITfThreadMgr *pThreadMgr);
    void KeyboardOpenCompartmentUpdated(_In_ ITfThreadMgr *pThreadMgr);
    void PersistInputMethodModeFromCompartment(_In_ ITfThreadMgr *pThreadMgr);
    void PersistCharacterFormFromCompartment(_In_ ITfThreadMgr *pThreadMgr);
    void PersistPunctuationFormFromCompartment(_In_ ITfThreadMgr *pThreadMgr);
    void ApplySettingsToCompartments(_In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId);

    BOOL SetupInputEngine();
    std::wstring CurrentInputText() const;
    std::vector<VirtualInputKey> CurrentInputKeys() const;
    Ime::ReverseLookupMethod CurrentReverseLookupMethod() const;
    std::vector<VirtualInputKey> ReverseLookupQueryKeys() const;
    std::wstring ReverseLookupReadingText(const std::vector<Ime::Lexicon>& suggestions) const;
    BOOL IsReverseLookupInputKey(UINT uCode) const;
    const std::vector<Ime::Lexicon>& GetInputSuggestions();
    std::optional<Ime::Lexicon> CandidateAt(UINT candidateIndex) const;
    CharacterStandard CurrentCharacterStandard() const;
    std::wstring DisplayTextForCandidate(const Ime::Lexicon& suggestion) const;
    void AppendInputEngineCandidates(_Inout_ CJyutpingArray<CCandidateListItem> *pCandidateList);

private:
    struct _KEYSTROKE
    {
        UINT VirtualKey;
        UINT Modifiers;
        KEYSTROKE_FUNCTION Function;

        _KEYSTROKE()
        {
            VirtualKey = 0;
            Modifiers = 0;
            Function = FUNCTION_NONE;
        }
    };
    _KEYSTROKE _keystrokeTable[VirtualInputKey::alphabetSetCount];

    CStringRange _keystrokeBuffer;

    LANGID _langid;
    GUID _guidProfile;
    TfClientId  _tfClientId;

    CJyutpingArray<_KEYSTROKE> _KeystrokeComposition;
    CJyutpingArray<_KEYSTROKE> _KeystrokeCandidate;
    CJyutpingArray<_KEYSTROKE> _KeystrokeCandidateSymbol;
    CJyutpingArray<_KEYSTROKE> _KeystrokeSymbol;

    // Preserved key data
    class XPreservedKey
    {
    public:
        XPreservedKey();
        ~XPreservedKey();
        BOOL UninitPreservedKey(_In_ ITfThreadMgr *pThreadMgr);

    public:
        CJyutpingArray<TF_PRESERVEDKEY> TSFPreservedKeyTable;
        GUID Guid;
        LPCWSTR Description;
    };

    XPreservedKey _PreservedKey_InputMethodMode;
    XPreservedKey _PreservedKey_CharacterForm;
    XPreservedKey _PreservedKey_PunctuationForm;
    XPreservedKey _PreservedKey_CharacterVariantTraditional;
    XPreservedKey _PreservedKey_CharacterVariantHongKong;
    XPreservedKey _PreservedKey_CharacterVariantTaiwan;
    XPreservedKey _PreservedKey_CharacterVariantSimplified;

    // Punctuation data
    CJyutpingArray<CPunctuationPair> _PunctuationPair;
    CJyutpingArray<CPunctuationNestPair> _PunctuationNestPair;

    // Language bar data
    CLangBarItemButton* _pLanguageBar_InputMethodMode;

    // Compartment
    CCompartment* _pCompartmentConversion;
    CCompartmentEventSink* _pCompartmentConversionEventSink;
    CCompartmentEventSink* _pCompartmentKeyboardOpenEventSink;
    CCompartmentEventSink* _pCompartmentCharacterFormEventSink;
    CCompartmentEventSink* _pCompartmentPunctuationFormEventSink;

    ImeSettings _settings;
    SettingsStore _settingsStore;
    BOOL _isApplyingSettings;
    BOOL _isMirroringConversionMode;

    // Configuration data
    BOOL _isComLessMode : 1;
    CCandidateRange _candidateListIndexRange;
    UINT _candidateListPhraseModifier;

    Ime::InputEngine _inputEngine;
    BOOL _isInputEngineReady;
    std::wstring _cachedInputText;
    Ime::ReverseLookupMethod _cachedReverseLookupMethod;
    std::vector<Ime::Lexicon> _cachedSuggestions;
    std::vector<Ime::Lexicon> _selectedMemorySequence;
    std::vector<std::wstring> _candidateItemTextStorage;
    std::vector<std::wstring> _candidateItemCommentStorage;
    std::wstring _readingStringStorage;

    static const int OUT_OF_FILE_INDEX = -1;
};
