#include "Private.h"
#include "Jyutping.h"
#include "CompositionProcessorEngine.h"
#include "TfInputProcessorProfile.h"
#include "Globals.h"
#include "Logger.h"
#include "Compartment.h"
#include "LanguageBar.h"
#include "Localization.h"
#include "RegKey.h"
#include "resource.h"
#include "Settings.h"

#include <algorithm>

namespace {

Ime::ReverseLookupMethod ReverseLookupMethodFromKey(const VirtualInputKey& key)
{
    if (key == VirtualInputKey::letterR)
    {
        return Ime::ReverseLookupMethod::Pinyin;
    }
    if (key == VirtualInputKey::letterV)
    {
        return Ime::ReverseLookupMethod::Cangjie;
    }
    if (key == VirtualInputKey::letterX)
    {
        return Ime::ReverseLookupMethod::Stroke;
    }
    if (key == VirtualInputKey::letterQ)
    {
        return Ime::ReverseLookupMethod::Structure;
    }
    return Ime::ReverseLookupMethod::None;
}

Ime::ReverseLookupMethod ReverseLookupMethodFromKeys(const std::vector<VirtualInputKey>& keys)
{
    return keys.empty() ? Ime::ReverseLookupMethod::None : ReverseLookupMethodFromKey(keys.front());
}

BOOL IsPinyinOrStructureMethod(Ime::ReverseLookupMethod method)
{
    return method == Ime::ReverseLookupMethod::Pinyin || method == Ime::ReverseLookupMethod::Structure;
}

BOOL IsStrokeMethod(Ime::ReverseLookupMethod method)
{
    return method == Ime::ReverseLookupMethod::Stroke;
}

BOOL ContainsToneInputKey(const std::vector<VirtualInputKey>& keys)
{
    return std::find_if(keys.begin(), keys.end(), [](const VirtualInputKey& key)
    {
        return key.IsToneInputKey();
    }) != keys.end();
}







std::vector<Ime::Lexicon> DistinctLexicons(const std::vector<Ime::Lexicon>& items)
{
    std::vector<Ime::Lexicon> result;
    result.reserve(items.size());
    for (const Ime::Lexicon& item : items)
    {
        if (std::find(result.begin(), result.end(), item) == result.end())
        {
            result.push_back(item);
        }
    }
    return result;
}

} // namespace

//+---------------------------------------------------------------------------
//
// _AddTextProcessorEngine
//
//----------------------------------------------------------------------------

BOOL CJyutping::_AddTextProcessorEngine()
{
    LANGID langid = 0;
    CLSID clsid = GUID_NULL;
    GUID guidProfile = GUID_NULL;

    // Get default profile.
    CTfInputProcessorProfile profile;

    HRESULT hr = profile.CreateInstance();
    if (FAILED(hr))
    {
        Global::Log(L"_AddTextProcessorEngine failed: profile.CreateInstance hr=0x%08X", static_cast<unsigned int>(hr));
        return FALSE;
    }

    hr = profile.GetCurrentLanguage(&langid);
    if (FAILED(hr))
    {
        Global::Log(L"_AddTextProcessorEngine failed: GetCurrentLanguage hr=0x%08X", static_cast<unsigned int>(hr));
        return FALSE;
    }

    hr = profile.GetDefaultLanguageProfile(langid, GUID_TFCAT_TIP_KEYBOARD, &clsid, &guidProfile);
    if (FAILED(hr))
    {
        Global::Log(
            L"_AddTextProcessorEngine failed: GetDefaultLanguageProfile langid=0x%04X hr=0x%08X",
            langid,
            static_cast<unsigned int>(hr));
        return FALSE;
    }

    Global::Log(
        L"_AddTextProcessorEngine start: langid=0x%04X clientId=%lu secure=%d comLess=%d",
        langid,
        _GetClientId(),
        _IsSecureMode(),
        _IsComLess());

    // Is this already added?
    if (_pCompositionProcessorEngine != nullptr)
    {
        LANGID langidProfile = 0;
        GUID guidLanguageProfile = GUID_NULL;

        guidLanguageProfile = _pCompositionProcessorEngine->GetLanguageProfile(&langidProfile);
        if ((langid == langidProfile) && IsEqualGUID(guidProfile, guidLanguageProfile))
        {
            Global::Log(L"_AddTextProcessorEngine: existing engine matches current language profile");
            _pCompositionProcessorEngine->SetTextService(this);
            return TRUE;
        }
    }

    // Create composition processor engine
    if (_pCompositionProcessorEngine == nullptr)
    {
        _pCompositionProcessorEngine = new (std::nothrow) CCompositionProcessorEngine();
    }
    if (!_pCompositionProcessorEngine)
    {
        Global::Log(L"_AddTextProcessorEngine failed: unable to allocate composition processor engine");
        return FALSE;
    }
    _pCompositionProcessorEngine->SetTextService(this);

    // setup composition processor engine
    if (FALSE == _pCompositionProcessorEngine->SetupLanguageProfile(langid, guidProfile, _GetThreadMgr(), _GetClientId(), _IsSecureMode(), _IsComLess()))
    {
        Global::Log(L"_AddTextProcessorEngine failed: SetupLanguageProfile returned false");
        return FALSE;
    }

    Global::Log(L"_AddTextProcessorEngine success");
    return TRUE;
}

//////////////////////////////////////////////////////////////////////
//
// CompositionProcessorEngine implementation.
//
//////////////////////////////////////////////////////////////////////

//+---------------------------------------------------------------------------
//
// ctor
//
//----------------------------------------------------------------------------

CCompositionProcessorEngine::CCompositionProcessorEngine()
{
    _langid = 0xffff;
    _guidProfile = GUID_NULL;
    _tfClientId = TF_CLIENTID_NULL;
    _pTextService = nullptr;

    _pLanguageBar_InputMethodMode = nullptr;

    _pCompartmentConversion = nullptr;
    _pCompartmentKeyboardOpenEventSink = nullptr;
    _pCompartmentConversionEventSink = nullptr;
    _pCompartmentCharacterFormEventSink = nullptr;
    _pCompartmentPunctuationFormEventSink = nullptr;

    _isApplyingSettings = FALSE;
    _isMirroringConversionMode = FALSE;

    _isInputEngineReady = FALSE;
    _cachedReverseLookupMethod = Ime::ReverseLookupMethod::None;

    _candidateListPhraseModifier = 0;

    InitKeyStrokeTable();
}

void CCompositionProcessorEngine::SetTextService(_In_opt_ CJyutping *pTextService)
{
    _pTextService = pTextService;
}

//+---------------------------------------------------------------------------
//
// dtor
//
//----------------------------------------------------------------------------

CCompositionProcessorEngine::~CCompositionProcessorEngine()
{
    if (_pLanguageBar_InputMethodMode)
    {
        _pLanguageBar_InputMethodMode->CleanUp();
        _pLanguageBar_InputMethodMode->Release();
        _pLanguageBar_InputMethodMode = nullptr;
    }

    if (_pCompartmentConversion)
    {
        delete _pCompartmentConversion;
        _pCompartmentConversion = nullptr;
    }
    if (_pCompartmentKeyboardOpenEventSink)
    {
        _pCompartmentKeyboardOpenEventSink->_Unadvise();
        delete _pCompartmentKeyboardOpenEventSink;
        _pCompartmentKeyboardOpenEventSink = nullptr;
    }
    if (_pCompartmentConversionEventSink)
    {
        _pCompartmentConversionEventSink->_Unadvise();
        delete _pCompartmentConversionEventSink;
        _pCompartmentConversionEventSink = nullptr;
    }
    if (_pCompartmentCharacterFormEventSink)
    {
        _pCompartmentCharacterFormEventSink->_Unadvise();
        delete _pCompartmentCharacterFormEventSink;
        _pCompartmentCharacterFormEventSink = nullptr;
    }
    if (_pCompartmentPunctuationFormEventSink)
    {
        _pCompartmentPunctuationFormEventSink->_Unadvise();
        delete _pCompartmentPunctuationFormEventSink;
        _pCompartmentPunctuationFormEventSink = nullptr;
    }

}

//+---------------------------------------------------------------------------
//
// SetupLanguageProfile
//
// Setup language profile for Composition Processor Engine.
// param
//     [in] LANGID langid = Specify language ID
//     [in] GUID guidLanguageProfile - Specify GUID language profile which GUID is as same as Text Service Framework language profile.
//     [in] ITfThreadMgr - pointer ITfThreadMgr.
//     [in] tfClientId - TfClientId value.
//     [in] isSecureMode - secure mode
// returns
//     If setup succeeded, returns true. Otherwise returns false.
// N.B. For reverse conversion, ITfThreadMgr is NULL, TfClientId is 0 and isSecureMode is ignored.
//+---------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::SetupLanguageProfile(LANGID langid, REFGUID guidLanguageProfile, _In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId, BOOL isSecureMode, BOOL isComLessMode)
{
    Global::Log(
        L"CompositionProcessorEngine setup start: langid=0x%04X clientId=%lu secure=%d comLess=%d",
        langid,
        tfClientId,
        isSecureMode,
        isComLessMode);

    if ((tfClientId == 0) && (pThreadMgr == nullptr))
    {
        Global::Log(L"CompositionProcessorEngine setup failed: client id and thread manager are both empty");
        return FALSE;
    }

    _isComLessMode = isComLessMode;
    _langid = langid;
    _guidProfile = guidLanguageProfile;
    _tfClientId = tfClientId;

    SetupPreserved(pThreadMgr, tfClientId);
    _settings = _settingsStore.Load();
    _isApplyingSettings = TRUE;
    InitializeJyutpingCompartment(pThreadMgr, tfClientId);
    SetupPunctuationPair();
    SetupLanguageBar(pThreadMgr, tfClientId, isSecureMode);
    PrivateCompartmentsUpdated(pThreadMgr);
    KeyboardOpenCompartmentUpdated(pThreadMgr);
    _isApplyingSettings = FALSE;
    SetupKeystroke();
    SetupConfiguration();
    BOOL isInputEngineReady = SetupInputEngine();

    Global::Log(L"CompositionProcessorEngine setup success: inputEngineReady=%d", isInputEngineReady);

    return TRUE;
}

void CCompositionProcessorEngine::ApplyPersistedSettings(_In_ ITfThreadMgr *pThreadMgr)
{
    if (pThreadMgr == nullptr)
    {
        return;
    }

    _settings = _settingsStore.Load();
    SetCandidateListRange(_settings.candidatePageSize);
    _isApplyingSettings = TRUE;
    ApplySettingsToCompartments(pThreadMgr, _tfClientId);
    PrivateCompartmentsUpdated(pThreadMgr);
    KeyboardOpenCompartmentUpdated(pThreadMgr);
    _isApplyingSettings = FALSE;
}

//+---------------------------------------------------------------------------
//
// AddVirtualKey
// Add virtual key code to Composition Processor Engine for used to parse keystroke data.
// param
//     [in] uCode - Specify virtual key code.
// returns
//     State of Text Processor Engine.
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::AddVirtualKey(WCHAR wch)
{
    if (!wch)
    {
        return FALSE;
    }

    VirtualInputKey inputKey;
    if (!VirtualInputKey::MatchInputKeyForCharacter(wch, &inputKey))
    {
        return FALSE;
    }

    //
    // append one keystroke in buffer.
    //
    DWORD_PTR srgKeystrokeBufLen = _keystrokeBuffer.GetLength();
    PWCHAR pwch = new (std::nothrow) WCHAR[ srgKeystrokeBufLen + 1 ];
    if (!pwch)
    {
        return FALSE;
    }

    memcpy(pwch, _keystrokeBuffer.Get(), srgKeystrokeBufLen * sizeof(WCHAR));
    pwch[ srgKeystrokeBufLen ] = inputKey.character;

    if (_keystrokeBuffer.Get())
    {
        delete [] _keystrokeBuffer.Get();
    }

    _keystrokeBuffer.Set(pwch, srgKeystrokeBufLen + 1);

    return TRUE;
}

//+---------------------------------------------------------------------------
//
// RemoveVirtualKey
// Remove stored virtual key code.
// param
//     [in] dwIndex   - Specified index.
// returns
//     none.
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::RemoveVirtualKey(DWORD_PTR dwIndex)
{
    DWORD_PTR srgKeystrokeBufLen = _keystrokeBuffer.GetLength();

    if (dwIndex + 1 < srgKeystrokeBufLen)
    {
        // shift following eles left
        memmove((BYTE*)_keystrokeBuffer.Get() + (dwIndex * sizeof(WCHAR)),
            (BYTE*)_keystrokeBuffer.Get() + ((dwIndex + 1) * sizeof(WCHAR)),
            (srgKeystrokeBufLen - dwIndex - 1) * sizeof(WCHAR));
    }

    _keystrokeBuffer.Set(_keystrokeBuffer.Get(), srgKeystrokeBufLen - 1);
}

//+---------------------------------------------------------------------------
//
// PurgeVirtualKey
// Purge stored virtual key code.
// param
//     none.
// returns
//     none.
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::PurgeVirtualKey()
{
    if (_keystrokeBuffer.Get())
    {
        delete [] _keystrokeBuffer.Get();
        _keystrokeBuffer.Set(NULL, 0);
    }
    ClearSelectedCandidateMemory();
}

WCHAR CCompositionProcessorEngine::GetVirtualKey(DWORD_PTR dwIndex)
{
    if (dwIndex < _keystrokeBuffer.GetLength())
    {
        return *(_keystrokeBuffer.Get() + dwIndex);
    }
    return 0;
}

std::wstring CCompositionProcessorEngine::GetRawInputText() const
{
    return CurrentInputText();
}

std::wstring CCompositionProcessorEngine::GetCandidateTailInputText(DWORD_PTR inputCount) const
{
    std::vector<VirtualInputKey> inputKeys = CurrentInputKeys();
    size_t offset = (std::min)(static_cast<size_t>(inputCount), inputKeys.size());
    BOOL isReverseLookupBuffer = ReverseLookupMethodFromKeys(inputKeys) != Ime::ReverseLookupMethod::None;
    std::vector<VirtualInputKey> tail(inputKeys.begin() + offset, inputKeys.end());
    while (!tail.empty() && tail.front().IsApostrophe())
    {
        tail.erase(tail.begin());
    }
    if (isReverseLookupBuffer && offset > 0 && !tail.empty())
    {
        tail.insert(tail.begin(), inputKeys.front());
    }
    return Ime::TextFromKeys(tail);
}

BOOL CCompositionProcessorEngine::SetRawInputText(const std::wstring& inputText)
{
    if (inputText.empty())
    {
        PurgeVirtualKey();
        _cachedInputText.clear();
        _cachedReverseLookupMethod = Ime::ReverseLookupMethod::None;
        _cachedSuggestions.clear();
        return TRUE;
    }

    std::vector<VirtualInputKey> inputKeys = Ime::InputKeysFromText(inputText);
    if (inputKeys.empty())
    {
        return FALSE;
    }

    std::wstring normalizedInputText = Ime::TextFromKeys(inputKeys);
    PWCHAR pwch = new (std::nothrow) WCHAR[normalizedInputText.length()];
    if (!pwch)
    {
        return FALSE;
    }

    memcpy(pwch, normalizedInputText.c_str(), normalizedInputText.length() * sizeof(WCHAR));

    if (_keystrokeBuffer.Get())
    {
        delete [] _keystrokeBuffer.Get();
    }

    _keystrokeBuffer.Set(pwch, normalizedInputText.length());
    _cachedInputText.clear();
    _cachedReverseLookupMethod = Ime::ReverseLookupMethod::None;
    _cachedSuggestions.clear();
    return TRUE;
}

//+---------------------------------------------------------------------------
//
// GetReadingStrings
// Retrieves string from Composition Processor Engine.
// param
//     [out] pReadingStrings - Specified returns pointer of CUnicodeString.
// returns
//     none
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::GetReadingStrings(_Inout_ CJyutpingArray<CStringRange> *pReadingStrings)
{
    if (pReadingStrings->Count() == 0 && _keystrokeBuffer.GetLength())
    {
        std::wstring currentInputText = CurrentInputText();
        const std::vector<Ime::Lexicon>& suggestions = GetInputSuggestions();
        if (!suggestions.empty())
        {
            BOOL isReverseLookupBuffer = IsReverseLookupBuffer();
            size_t inputLength = currentInputText.length();
            size_t matchedInputCount = suggestions.front().inputCount;
            if (isReverseLookupBuffer && inputLength > 0)
            {
                matchedInputCount = (std::min)(matchedInputCount, inputLength - 1) + 1;
            }

            std::vector<VirtualInputKey> inputKeys = CurrentInputKeys();
            if (!isReverseLookupBuffer && ContainsToneInputKey(inputKeys))
            {
                std::vector<Ime::BasicInputEvent> inputEvents;
                inputEvents.reserve(inputKeys.size());
                for (const VirtualInputKey& inputKey : inputKeys)
                {
                    inputEvents.push_back(Ime::BasicInputEvent(inputKey, Ime::KeyboardCase::Lowercased));
                }
                _readingStringStorage = Ime::PreviewMarkNormalized(inputEvents);
            }
            else if (matchedInputCount < inputLength)
            {
                _readingStringStorage = currentInputText;
            }
            else
            {
                _readingStringStorage = isReverseLookupBuffer ? ReverseLookupReadingText(suggestions) : suggestions.front().mark;
                if (_readingStringStorage.empty())
                {
                    _readingStringStorage = currentInputText;
                }
            }
        }
        else
        {
            _readingStringStorage = currentInputText;
        }

        CStringRange* pNewString = nullptr;

        pNewString = pReadingStrings->Append();
        if (pNewString)
        {
            pNewString->Set(_readingStringStorage.c_str(), _readingStringStorage.length());
        }
    }
}

//+---------------------------------------------------------------------------
//
// GetCandidateList
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::GetCandidateList(_Inout_ CJyutpingArray<CCandidateListItem> *pCandidateList)
{
    if (pCandidateList == nullptr)
    {
        return;
    }

    AppendInputEngineCandidates(pCandidateList);
}

void CCompositionProcessorEngine::AppendSelectedCandidateForMemory(UINT candidateIndex)
{
    std::optional<Ime::Lexicon> lexicon = CandidateAt(candidateIndex);
    if (!lexicon || lexicon->IsNotCantonese())
    {
        _selectedMemorySequence.clear();
        return;
    }

    _selectedMemorySequence.push_back(*lexicon);
}

void CCompositionProcessorEngine::CommitSelectedCandidateForMemory(UINT candidateIndex)
{
    std::optional<Ime::Lexicon> lexicon = CandidateAt(candidateIndex);
    if (!lexicon || lexicon->IsNotCantonese())
    {
        _selectedMemorySequence.clear();
        return;
    }

    _selectedMemorySequence.push_back(*lexicon);
    std::optional<Ime::Lexicon> joined = Ime::JoinLexicons(_selectedMemorySequence);
    if (joined)
    {
        _inputEngine.Remember(*joined);
    }
    _selectedMemorySequence.clear();
}

BOOL CCompositionProcessorEngine::ForgetCandidateFromMemory(UINT candidateIndex)
{
    std::optional<Ime::Lexicon> lexicon = CandidateAt(candidateIndex);
    if (!lexicon || lexicon->IsNotCantonese())
    {
        return FALSE;
    }

    BOOL result = _inputEngine.Forget(*lexicon) ? TRUE : FALSE;
    _cachedInputText.clear();
    _cachedReverseLookupMethod = Ime::ReverseLookupMethod::None;
    _cachedSuggestions.clear();
    return result;
}

void CCompositionProcessorEngine::ClearSelectedCandidateMemory()
{
    _selectedMemorySequence.clear();
}

//+---------------------------------------------------------------------------
//
// IsPunctuation
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::IsPunctuation(WCHAR wch)
{
    for (int i = 0; i < ARRAYSIZE(Global::PunctuationTable); i++)
    {
        if (Global::PunctuationTable[i]._Code == wch)
        {
            return TRUE;
        }
    }

    for (UINT j = 0; j < _PunctuationPair.Count(); j++)
    {
        CPunctuationPair* pPuncPair = _PunctuationPair.GetAt(j);

        if (pPuncPair->_punctuation._Code == wch)
        {
            return TRUE;
        }
    }

    for (UINT k = 0; k < _PunctuationNestPair.Count(); k++)
    {
        CPunctuationNestPair* pPuncNestPair = _PunctuationNestPair.GetAt(k);

        if (pPuncNestPair->_punctuation_begin._Code == wch)
        {
            return TRUE;
        }
        if (pPuncNestPair->_punctuation_end._Code == wch)
        {
            return TRUE;
        }
    }
    return FALSE;
}

//+---------------------------------------------------------------------------
//
// GetPunctuationPair
//
//----------------------------------------------------------------------------

WCHAR CCompositionProcessorEngine::GetPunctuation(WCHAR wch)
{
    for (int i = 0; i < ARRAYSIZE(Global::PunctuationTable); i++)
    {
        if (Global::PunctuationTable[i]._Code == wch)
        {
            return Global::PunctuationTable[i]._Punctuation;
        }
    }

    for (UINT j = 0; j < _PunctuationPair.Count(); j++)
    {
        CPunctuationPair* pPuncPair = _PunctuationPair.GetAt(j);

        if (pPuncPair->_punctuation._Code == wch)
        {
            if (! pPuncPair->_isPairToggle)
            {
                pPuncPair->_isPairToggle = TRUE;
                return pPuncPair->_punctuation._Punctuation;
            }
            else
            {
                pPuncPair->_isPairToggle = FALSE;
                return pPuncPair->_pairPunctuation;
            }
        }
    }

    for (UINT k = 0; k < _PunctuationNestPair.Count(); k++)
    {
        CPunctuationNestPair* pPuncNestPair = _PunctuationNestPair.GetAt(k);

        if (pPuncNestPair->_punctuation_begin._Code == wch)
        {
            if (pPuncNestPair->_nestCount++ == 0)
            {
                return pPuncNestPair->_punctuation_begin._Punctuation;
            }
            else
            {
                return pPuncNestPair->_pairPunctuation_begin;
            }
        }
        if (pPuncNestPair->_punctuation_end._Code == wch)
        {
            if (--pPuncNestPair->_nestCount == 0)
            {
                return pPuncNestPair->_punctuation_end._Punctuation;
            }
            else
            {
                return pPuncNestPair->_pairPunctuation_end;
            }
        }
    }
    return 0;
}

//+---------------------------------------------------------------------------
//
// IsCharacterFormConvertible
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::IsCharacterFormConvertible(WCHAR wch)
{
    if (L' ' <= wch && wch <= L'~')
    {
        return TRUE;
    }
    return FALSE;
}

//+---------------------------------------------------------------------------
//
// SetupKeystroke
//
//----------------------------------------------------------------------------


//+---------------------------------------------------------------------------
//
// SetKeystrokeTable
//
//----------------------------------------------------------------------------


//+---------------------------------------------------------------------------
//
// SetupPreserved
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::SetupPreserved(_In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId)
{
    std::wstring inputMethodModeDescription = Localization::LoadStringOrFallback(IDS_INPUT_MODE_DESCRIPTION, Global::InputMethodModeDescription);
    std::wstring characterFormDescription = Localization::LoadStringOrFallback(IDS_CHARACTER_FORM_DESCRIPTION, Global::CharacterFormDescription);
    std::wstring punctuationFormDescription = Localization::LoadStringOrFallback(IDS_PUNCTUATION_FORM_DESCRIPTION, Global::PunctuationFormDescription);
    std::wstring traditionalCharacterVariantDescription = Localization::LoadStringOrFallback(IDS_CHARACTER_VARIANT_TRADITIONAL, Global::TraditionalCharacterVariantDescription);
    std::wstring hongKongCharacterVariantDescription = Localization::LoadStringOrFallback(IDS_CHARACTER_VARIANT_HONG_KONG, Global::HongKongCharacterVariantDescription);
    std::wstring taiwanCharacterVariantDescription = Localization::LoadStringOrFallback(IDS_CHARACTER_VARIANT_TAIWAN, Global::TaiwanCharacterVariantDescription);
    std::wstring simplifiedCharacterVariantDescription = Localization::LoadStringOrFallback(IDS_CHARACTER_VARIANT_SIMPLIFIED, Global::SimplifiedCharacterVariantDescription);

    TF_PRESERVEDKEY preservedKeyInputMethodMode;
    preservedKeyInputMethodMode.uVKey = VK_SHIFT;
    preservedKeyInputMethodMode.uModifiers = _TF_MOD_ON_KEYUP_SHIFT_ONLY;
    SetPreservedKey(Global::JyutpingGuidInputMethodModePreserveKey, preservedKeyInputMethodMode, inputMethodModeDescription.c_str(), &_PreservedKey_InputMethodMode);

    TF_PRESERVEDKEY preservedKeyCharacterForm;
    preservedKeyCharacterForm.uVKey = VK_SPACE;
    preservedKeyCharacterForm.uModifiers = TF_MOD_SHIFT;
    SetPreservedKey(Global::JyutpingGuidCharacterFormPreserveKey, preservedKeyCharacterForm, characterFormDescription.c_str(), &_PreservedKey_CharacterForm);

    TF_PRESERVEDKEY preservedKeyPunctuationForm;
    preservedKeyPunctuationForm.uVKey = VK_OEM_PERIOD;
    preservedKeyPunctuationForm.uModifiers = TF_MOD_CONTROL;
    SetPreservedKey(Global::JyutpingGuidPunctuationFormPreserveKey, preservedKeyPunctuationForm, punctuationFormDescription.c_str(), &_PreservedKey_PunctuationForm);

    TF_PRESERVEDKEY preservedKeyTraditionalCharacterVariant;
    preservedKeyTraditionalCharacterVariant.uVKey = L'1';
    preservedKeyTraditionalCharacterVariant.uModifiers = TF_MOD_CONTROL | TF_MOD_SHIFT;
    SetPreservedKey(Global::JyutpingGuidTraditionalCharacterVariantPreserveKey, preservedKeyTraditionalCharacterVariant, traditionalCharacterVariantDescription.c_str(), &_PreservedKey_CharacterVariantTraditional);

    TF_PRESERVEDKEY preservedKeyHongKongCharacterVariant;
    preservedKeyHongKongCharacterVariant.uVKey = L'2';
    preservedKeyHongKongCharacterVariant.uModifiers = TF_MOD_CONTROL | TF_MOD_SHIFT;
    SetPreservedKey(Global::JyutpingGuidHongKongCharacterVariantPreserveKey, preservedKeyHongKongCharacterVariant, hongKongCharacterVariantDescription.c_str(), &_PreservedKey_CharacterVariantHongKong);

    TF_PRESERVEDKEY preservedKeyTaiwanCharacterVariant;
    preservedKeyTaiwanCharacterVariant.uVKey = L'3';
    preservedKeyTaiwanCharacterVariant.uModifiers = TF_MOD_CONTROL | TF_MOD_SHIFT;
    SetPreservedKey(Global::JyutpingGuidTaiwanCharacterVariantPreserveKey, preservedKeyTaiwanCharacterVariant, taiwanCharacterVariantDescription.c_str(), &_PreservedKey_CharacterVariantTaiwan);

    TF_PRESERVEDKEY preservedKeySimplifiedCharacterVariant;
    preservedKeySimplifiedCharacterVariant.uVKey = L'4';
    preservedKeySimplifiedCharacterVariant.uModifiers = TF_MOD_CONTROL | TF_MOD_SHIFT;
    SetPreservedKey(Global::JyutpingGuidSimplifiedCharacterVariantPreserveKey, preservedKeySimplifiedCharacterVariant, simplifiedCharacterVariantDescription.c_str(), &_PreservedKey_CharacterVariantSimplified);

    InitPreservedKey(&_PreservedKey_InputMethodMode, pThreadMgr, tfClientId);
    InitPreservedKey(&_PreservedKey_CharacterForm, pThreadMgr, tfClientId);
    InitPreservedKey(&_PreservedKey_PunctuationForm, pThreadMgr, tfClientId);
    InitPreservedKey(&_PreservedKey_CharacterVariantTraditional, pThreadMgr, tfClientId);
    InitPreservedKey(&_PreservedKey_CharacterVariantHongKong, pThreadMgr, tfClientId);
    InitPreservedKey(&_PreservedKey_CharacterVariantTaiwan, pThreadMgr, tfClientId);
    InitPreservedKey(&_PreservedKey_CharacterVariantSimplified, pThreadMgr, tfClientId);

    return;
}

//+---------------------------------------------------------------------------
//
// SetKeystrokeTable
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::SetPreservedKey(const CLSID clsid, TF_PRESERVEDKEY & tfPreservedKey, _In_z_ LPCWSTR pwszDescription, _Out_ XPreservedKey *pXPreservedKey)
{
    pXPreservedKey->Guid = clsid;

    TF_PRESERVEDKEY *ptfPsvKey1 = pXPreservedKey->TSFPreservedKeyTable.Append();
    if (!ptfPsvKey1)
    {
        return;
    }
    *ptfPsvKey1 = tfPreservedKey;

	size_t srgKeystrokeBufLen = 0;
	if (StringCchLength(pwszDescription, STRSAFE_MAX_CCH, &srgKeystrokeBufLen) != S_OK)
    {
        return;
    }
    pXPreservedKey->Description = new (std::nothrow) WCHAR[srgKeystrokeBufLen + 1];
    if (!pXPreservedKey->Description)
    {
        return;
    }

    StringCchCopy((LPWSTR)pXPreservedKey->Description, srgKeystrokeBufLen + 1, pwszDescription);

    return;
}
//+---------------------------------------------------------------------------
//
// InitPreservedKey
//
// Register a hot key.
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::InitPreservedKey(_In_ XPreservedKey *pXPreservedKey, _In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId)
{
    ITfKeystrokeMgr *pKeystrokeMgr = nullptr;

    if (IsEqualGUID(pXPreservedKey->Guid, GUID_NULL))
    {
        return FALSE;
    }

    if (pThreadMgr->QueryInterface(IID_ITfKeystrokeMgr, (void **)&pKeystrokeMgr) != S_OK)
    {
        return FALSE;
    }

    for (UINT i = 0; i < pXPreservedKey->TSFPreservedKeyTable.Count(); i++)
    {
        TF_PRESERVEDKEY preservedKey = *pXPreservedKey->TSFPreservedKeyTable.GetAt(i);
        preservedKey.uModifiers &= 0xffff;

		size_t lenOfDesc = 0;
		if (StringCchLength(pXPreservedKey->Description, STRSAFE_MAX_CCH, &lenOfDesc) != S_OK)
        {
            return FALSE;
        }
        pKeystrokeMgr->PreserveKey(tfClientId, pXPreservedKey->Guid, &preservedKey, pXPreservedKey->Description, static_cast<ULONG>(lenOfDesc));
    }

    pKeystrokeMgr->Release();

    return TRUE;
}

//+---------------------------------------------------------------------------
//
// CheckShiftKeyOnly
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::CheckShiftKeyOnly(_In_ CJyutpingArray<TF_PRESERVEDKEY> *pTSFPreservedKeyTable)
{
    for (UINT i = 0; i < pTSFPreservedKeyTable->Count(); i++)
    {
        TF_PRESERVEDKEY *ptfPsvKey = pTSFPreservedKeyTable->GetAt(i);

        if (((ptfPsvKey->uModifiers & (_TF_MOD_ON_KEYUP_SHIFT_ONLY & 0xffff0000)) && !Global::IsShiftKeyDownOnly) ||
            ((ptfPsvKey->uModifiers & (_TF_MOD_ON_KEYUP_CONTROL_ONLY & 0xffff0000)) && !Global::IsControlKeyDownOnly) ||
            ((ptfPsvKey->uModifiers & (_TF_MOD_ON_KEYUP_ALT_ONLY & 0xffff0000)) && !Global::IsAltKeyDownOnly)         )
        {
            return FALSE;
        }
    }

    return TRUE;
}

//+---------------------------------------------------------------------------
//
// OnPreservedKey
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::OnPreservedKey(REFGUID rguid, _Out_ BOOL *pIsEaten, _In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId)
{
    if (pIsEaten == nullptr)
    {
        return;
    }

    if (IsEqualGUID(rguid, _PreservedKey_InputMethodMode.Guid))
    {
        if (!CheckShiftKeyOnly(&_PreservedKey_InputMethodMode.TSFPreservedKeyTable))
        {
            *pIsEaten = FALSE;
            return;
        }
        BOOL isOpen = FALSE;
        CCompartment CompartmentKeyboardOpen(pThreadMgr, tfClientId, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
        HRESULT hr = CompartmentKeyboardOpen._GetCompartmentBOOL(isOpen);
        if (FAILED(hr))
        {
            *pIsEaten = FALSE;
            return;
        }

        BOOL newIsOpen = isOpen ? FALSE : TRUE;
        hr = CompartmentKeyboardOpen._SetCompartmentBOOL(newIsOpen);
        if (FAILED(hr))
        {
            *pIsEaten = FALSE;
            return;
        }

        InputMethodMode mode = InputMethodModeFromKeyboardOpen(newIsOpen);
        _settings.inputMethodMode = mode;
        _settingsStore.SaveInputMethodMode(mode);

        *pIsEaten = TRUE;
    }
    else if (IsEqualGUID(rguid, _PreservedKey_CharacterForm.Guid))
    {
        if (!CheckShiftKeyOnly(&_PreservedKey_CharacterForm.TSFPreservedKeyTable))
        {
            *pIsEaten = FALSE;
            return;
        }
        BOOL isFullWidth = FALSE;
        CCompartment CompartmentCharacterForm(pThreadMgr, tfClientId, Global::JyutpingGuidCompartmentCharacterForm);
        HRESULT hr = CompartmentCharacterForm._GetCompartmentBOOL(isFullWidth);
        if (FAILED(hr))
        {
            *pIsEaten = FALSE;
            return;
        }

        BOOL newIsFullWidth = isFullWidth ? FALSE : TRUE;
        hr = CompartmentCharacterForm._SetCompartmentBOOL(newIsFullWidth);
        if (FAILED(hr))
        {
            *pIsEaten = FALSE;
            return;
        }

        CharacterForm form = CharacterFormFromFullWidth(newIsFullWidth);
        _settings.characterForm = form;
        _settingsStore.SaveCharacterForm(form);
        *pIsEaten = TRUE;
    }
    else if (IsEqualGUID(rguid, _PreservedKey_PunctuationForm.Guid))
    {
        if (!CheckShiftKeyOnly(&_PreservedKey_PunctuationForm.TSFPreservedKeyTable))
        {
            *pIsEaten = FALSE;
            return;
        }
        BOOL isCantonesePunctuation = FALSE;
        CCompartment CompartmentPunctuationForm(pThreadMgr, tfClientId, Global::JyutpingGuidCompartmentPunctuationForm);
        HRESULT hr = CompartmentPunctuationForm._GetCompartmentBOOL(isCantonesePunctuation);
        if (FAILED(hr))
        {
            *pIsEaten = FALSE;
            return;
        }

        BOOL newIsCantonesePunctuation = isCantonesePunctuation ? FALSE : TRUE;
        hr = CompartmentPunctuationForm._SetCompartmentBOOL(newIsCantonesePunctuation);
        if (FAILED(hr))
        {
            *pIsEaten = FALSE;
            return;
        }

        PunctuationForm form = PunctuationFormFromCantonesePunctuation(newIsCantonesePunctuation);
        _settings.punctuationForm = form;
        _settingsStore.SavePunctuationForm(form);
        *pIsEaten = TRUE;
    }
    else if (IsEqualGUID(rguid, _PreservedKey_CharacterVariantTraditional.Guid))
    {
        _settings.characterVariant = CharacterVariant::Traditional;
        _settingsStore.SaveCharacterVariant(_settings.characterVariant);
        *pIsEaten = TRUE;
    }
    else if (IsEqualGUID(rguid, _PreservedKey_CharacterVariantHongKong.Guid))
    {
        _settings.characterVariant = CharacterVariant::HongKong;
        _settingsStore.SaveCharacterVariant(_settings.characterVariant);
        *pIsEaten = TRUE;
    }
    else if (IsEqualGUID(rguid, _PreservedKey_CharacterVariantTaiwan.Guid))
    {
        _settings.characterVariant = CharacterVariant::Taiwan;
        _settingsStore.SaveCharacterVariant(_settings.characterVariant);
        *pIsEaten = TRUE;
    }
    else if (IsEqualGUID(rguid, _PreservedKey_CharacterVariantSimplified.Guid))
    {
        _settings.characterVariant = CharacterVariant::Simplified;
        _settingsStore.SaveCharacterVariant(_settings.characterVariant);
        *pIsEaten = TRUE;
    }
    else
    {
        *pIsEaten = FALSE;
    }
}

BOOL CCompositionProcessorEngine::IsCharacterVariantPreservedKey(REFGUID rguid) const
{
    return IsEqualGUID(rguid, _PreservedKey_CharacterVariantTraditional.Guid) ||
        IsEqualGUID(rguid, _PreservedKey_CharacterVariantHongKong.Guid) ||
        IsEqualGUID(rguid, _PreservedKey_CharacterVariantTaiwan.Guid) ||
        IsEqualGUID(rguid, _PreservedKey_CharacterVariantSimplified.Guid);
}

//+---------------------------------------------------------------------------
//
// SetupConfiguration
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::SetupConfiguration()
{
    SetCandidateListRange(_settings.candidatePageSize);

    return;
}

//+---------------------------------------------------------------------------
//
// SetupLanguageBar
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::SetupLanguageBar(_In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId, BOOL isSecureMode)
{
    DWORD dwEnable = 1;
    std::wstring inputMethodModeTooltip = Localization::LoadStringOrFallback(IDS_LANGBAR_INPUT_MODE_TOOLTIP, Global::InputMethodModeTooltip);
    std::wstring langbarInputMethodModeDescription = Localization::LoadStringOrFallback(IDS_LANGBAR_INPUT_METHOD_MODE, Global::LangbarInputMethodModeDescription);
    CreateLanguageBarButton(
        dwEnable,
        GUID_LBI_INPUTMODE,
        langbarInputMethodModeDescription.c_str(),
        inputMethodModeTooltip.c_str(),
        Global::InputMethodModeCantoneseIcoIndex,
        Global::InputMethodModeABCIcoIndex,
        Global::InputMethodModeCantoneseAltIcoIndex,
        Global::InputMethodModeABCAltIcoIndex,
        &_pLanguageBar_InputMethodMode,
        isSecureMode);
    if (_pLanguageBar_InputMethodMode)
    {
        _pLanguageBar_InputMethodMode->SetSettingsMenuHandler(this);
    }

    InitLanguageBar(_pLanguageBar_InputMethodMode, pThreadMgr, tfClientId, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);

    _pCompartmentConversion = new (std::nothrow) CCompartment(pThreadMgr, tfClientId, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION);
    _pCompartmentKeyboardOpenEventSink = new (std::nothrow) CCompartmentEventSink(CompartmentCallback, this);
    _pCompartmentConversionEventSink = new (std::nothrow) CCompartmentEventSink(CompartmentCallback, this);
    _pCompartmentCharacterFormEventSink = new (std::nothrow) CCompartmentEventSink(CompartmentCallback, this);
    _pCompartmentPunctuationFormEventSink = new (std::nothrow) CCompartmentEventSink(CompartmentCallback, this);

    if (_pCompartmentKeyboardOpenEventSink)
    {
        _pCompartmentKeyboardOpenEventSink->_Advise(pThreadMgr, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
    }
    if (_pCompartmentConversionEventSink)
    {
        _pCompartmentConversionEventSink->_Advise(pThreadMgr, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION);
    }
    if (_pCompartmentCharacterFormEventSink)
    {
        _pCompartmentCharacterFormEventSink->_Advise(pThreadMgr, Global::JyutpingGuidCompartmentCharacterForm);
    }
    if (_pCompartmentPunctuationFormEventSink)
    {
        _pCompartmentPunctuationFormEventSink->_Advise(pThreadMgr, Global::JyutpingGuidCompartmentPunctuationForm);
    }

    return;
}

//+---------------------------------------------------------------------------
//
// CreateLanguageBarButton
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::CreateLanguageBarButton(
    DWORD dwEnable,
    GUID guidLangBar,
    _In_z_ LPCWSTR pwszDescriptionValue,
    _In_z_ LPCWSTR pwszTooltipValue,
    DWORD dwOnIconIndex,
    DWORD dwOffIconIndex,
    DWORD dwOnDarkIconIndex,
    DWORD dwOffDarkIconIndex,
    _Outptr_result_maybenull_ CLangBarItemButton **ppLangBarItemButton,
    BOOL isSecureMode)
{
	dwEnable;

    if (ppLangBarItemButton)
    {
        *ppLangBarItemButton = new (std::nothrow) CLangBarItemButton(
            guidLangBar,
            pwszDescriptionValue,
            pwszTooltipValue,
            dwOnIconIndex,
            dwOffIconIndex,
            dwOnDarkIconIndex,
            dwOffDarkIconIndex,
            isSecureMode);
    }

    return;
}

//+---------------------------------------------------------------------------
//
// InitLanguageBar
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::InitLanguageBar(_In_ CLangBarItemButton *pLangBarItemButton, _In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId, REFGUID guidCompartment)
{
    if (pLangBarItemButton)
    {
        if (pLangBarItemButton->_AddItem(pThreadMgr) == S_OK)
        {
            if (pLangBarItemButton->_RegisterCompartment(pThreadMgr, tfClientId, guidCompartment))
            {
                return TRUE;
            }
        }
    }
    return FALSE;
}

BOOL CCompositionProcessorEngine::SetupInputEngine()
{
    Global::Log(L"CompositionProcessorEngine input engine setup start");
    _isInputEngineReady = _inputEngine.Prepare() ? TRUE : FALSE;
    if (!_isInputEngineReady)
    {
        Global::Log(L"CompositionProcessorEngine input engine setup failed");
        _cachedInputText.clear();
        _cachedSuggestions.clear();
        return _isInputEngineReady;
    }
    Global::Log(L"CompositionProcessorEngine input engine setup success");
    return _isInputEngineReady;
}

std::wstring CCompositionProcessorEngine::CurrentInputText() const
{
    const WCHAR* buffer = _keystrokeBuffer.Get();
    if (buffer == nullptr || _keystrokeBuffer.GetLength() == 0)
    {
        return std::wstring();
    }
    return std::wstring(buffer, static_cast<size_t>(_keystrokeBuffer.GetLength()));
}

std::vector<VirtualInputKey> CCompositionProcessorEngine::CurrentInputKeys() const
{
    const WCHAR* buffer = _keystrokeBuffer.Get();
    if (buffer == nullptr || _keystrokeBuffer.GetLength() == 0)
    {
        return std::vector<VirtualInputKey>();
    }
    return Ime::InputKeysFromText(std::wstring_view(buffer, static_cast<size_t>(_keystrokeBuffer.GetLength())));
}

Ime::ReverseLookupMethod CCompositionProcessorEngine::CurrentReverseLookupMethod() const
{
    return ReverseLookupMethodFromKeys(CurrentInputKeys());
}

BOOL CCompositionProcessorEngine::IsReverseLookupBuffer() const
{
    return CurrentReverseLookupMethod() != Ime::ReverseLookupMethod::None;
}

std::vector<VirtualInputKey> CCompositionProcessorEngine::ReverseLookupQueryKeys() const
{
    std::vector<VirtualInputKey> inputKeys = CurrentInputKeys();
    if (inputKeys.empty() || ReverseLookupMethodFromKeys(inputKeys) == Ime::ReverseLookupMethod::None)
    {
        return std::vector<VirtualInputKey>();
    }
    return std::vector<VirtualInputKey>(inputKeys.begin() + 1, inputKeys.end());
}

std::wstring CCompositionProcessorEngine::ReverseLookupReadingText(const std::vector<Ime::Lexicon>& suggestions) const
{
    Ime::ReverseLookupMethod method = CurrentReverseLookupMethod();
    if (suggestions.empty() || suggestions.front().mark.empty())
    {
        return CurrentInputText();
    }
    if (suggestions.front().IsNotCantonese())
    {
        return CurrentInputText();
    }

    if (method == Ime::ReverseLookupMethod::Pinyin)
    {
        return std::wstring(VirtualInputKey::letterR.text) + L" " + suggestions.front().mark;
    }
    if (method == Ime::ReverseLookupMethod::Structure)
    {
        return std::wstring(VirtualInputKey::letterQ.text) + L" " + suggestions.front().mark;
    }
    return suggestions.front().mark;
}

BOOL CCompositionProcessorEngine::IsReverseLookupInputKey(UINT uCode) const
{
    Ime::ReverseLookupMethod method = CurrentReverseLookupMethod();
    if (method == Ime::ReverseLookupMethod::None || Global::ModifiersValue != 0)
    {
        return FALSE;
    }

    VirtualInputKey inputKey;
    if (!VirtualInputKey::MatchInputKeyForKeyCode(uCode, &inputKey))
    {
        return FALSE;
    }

    if (IsPinyinOrStructureMethod(method) && inputKey.IsApostrophe())
    {
        return TRUE;
    }

    if (IsStrokeMethod(method) && inputKey.IsToneNumber())
    {
        return TRUE;
    }

    return FALSE;
}

const std::vector<Ime::Lexicon>& CCompositionProcessorEngine::GetInputSuggestions()
{
    std::vector<VirtualInputKey> inputKeys = CurrentInputKeys();
    std::wstring inputText = Ime::TextFromKeys(inputKeys);
    Ime::ReverseLookupMethod reverseLookupMethod = ReverseLookupMethodFromKeys(inputKeys);
    if (!_isInputEngineReady || inputKeys.empty())
    {
        _cachedInputText = inputText;
        _cachedReverseLookupMethod = reverseLookupMethod;
        _cachedSuggestions.clear();
        return _cachedSuggestions;
    }

    std::vector<VirtualInputKey> reverseLookupQueryKeys;
    if (reverseLookupMethod != Ime::ReverseLookupMethod::None)
    {
        reverseLookupQueryKeys = std::vector<VirtualInputKey>(inputKeys.begin() + 1, inputKeys.end());
    }

    if (inputText != _cachedInputText || reverseLookupMethod != _cachedReverseLookupMethod)
    {
        _cachedInputText = inputText;
        _cachedReverseLookupMethod = reverseLookupMethod;
        if (reverseLookupMethod == Ime::ReverseLookupMethod::None)
        {
            _cachedSuggestions = _inputEngine.Suggest(inputKeys);
        }
        else
        {
            _cachedSuggestions = _inputEngine.SearchTextMarks(inputKeys);
            if (!reverseLookupQueryKeys.empty())
            {
                std::vector<Ime::Lexicon> lookupSuggestions = _inputEngine.ReverseLookup(reverseLookupMethod, reverseLookupQueryKeys);
                _cachedSuggestions.insert(_cachedSuggestions.end(), lookupSuggestions.begin(), lookupSuggestions.end());
            }
            _cachedSuggestions = DistinctLexicons(_cachedSuggestions);
        }
    }
    return _cachedSuggestions;
}

std::optional<Ime::Lexicon> CCompositionProcessorEngine::CandidateAt(UINT candidateIndex) const
{
    if (IsReverseLookupBuffer())
    {
        return std::nullopt;
    }

    size_t index = static_cast<size_t>(candidateIndex);
    if (index >= _cachedSuggestions.size())
    {
        return std::nullopt;
    }
    return _cachedSuggestions[index];
}

CharacterStandard CCompositionProcessorEngine::CurrentCharacterStandard() const
{
    return CharacterStandardFromCharacterVariant(_settings.characterVariant);
}

CharacterVariant CCompositionProcessorEngine::CurrentCharacterVariant() const
{
    return _settings.characterVariant;
}

void CCompositionProcessorEngine::SetCharacterVariant(CharacterVariant variant)
{
    if (_settings.characterVariant == variant)
    {
        return;
    }

    _settings.characterVariant = variant;
    _settingsStore.SaveCharacterVariant(variant);

    if (_pTextService)
    {
        _pTextService->RefreshCandidateListAfterCharacterVariantChange();
    }
}

DWORD CCompositionProcessorEngine::CurrentCandidatePageSize() const
{
    return _settings.candidatePageSize;
}

void CCompositionProcessorEngine::SetCandidatePageSize(DWORD pageSize)
{
    DWORD normalizedPageSize = CandidatePageSizeFromRawValue(pageSize);
    if (_settings.candidatePageSize == normalizedPageSize)
    {
        return;
    }

    _settings.candidatePageSize = normalizedPageSize;
    SetCandidateListRange(normalizedPageSize);
    _settingsStore.SaveCandidatePageSize(normalizedPageSize);

    if (_pTextService)
    {
        _pTextService->RefreshCandidateListAfterCharacterVariantChange();
    }
}

std::wstring CCompositionProcessorEngine::DisplayTextForCandidate(const Ime::Lexicon& suggestion) const
{
    if (suggestion.IsCantonese())
    {
        return _inputEngine.ConvertText(suggestion.text, CurrentCharacterStandard());
    }
    return suggestion.text;
}

std::wstring CCompositionProcessorEngine::CommentTextForCandidate(const Ime::Lexicon& suggestion) const
{
    if (suggestion.IsCantonese())
    {
        return suggestion.romanization;
    }
    return std::wstring();
}

void CCompositionProcessorEngine::AppendInputEngineCandidates(_Inout_ CJyutpingArray<CCandidateListItem> *pCandidateList)
{
    if (pCandidateList == nullptr)
    {
        return;
    }

    const std::vector<Ime::Lexicon>& suggestions = GetInputSuggestions();
    if (suggestions.empty())
    {
        return;
    }

    _candidateItemTextStorage.clear();
    _candidateItemCommentStorage.clear();
    _candidateItemTextStorage.reserve(suggestions.size());
    _candidateItemCommentStorage.reserve(suggestions.size());

    std::vector<VirtualInputKey> inputKeys = CurrentInputKeys();
    BOOL isReverseLookupBuffer = ReverseLookupMethodFromKeys(inputKeys) != Ime::ReverseLookupMethod::None;
    size_t reverseLookupQueryLength = (isReverseLookupBuffer && !inputKeys.empty()) ? inputKeys.size() - 1 : 0;

    for (const Ime::Lexicon& suggestion : suggestions)
    {
        CCandidateListItem* pItem = pCandidateList->Append();
        if (pItem == nullptr)
        {
            return;
        }

        _candidateItemTextStorage.push_back(DisplayTextForCandidate(suggestion));
        _candidateItemCommentStorage.push_back(CommentTextForCandidate(suggestion));

        const std::wstring& text = _candidateItemTextStorage.back();
        const std::wstring& comment = _candidateItemCommentStorage.back();

        pItem->_ItemString.Set(text.c_str(), text.length());
        pItem->_ItemComment.Set(comment.c_str(), comment.length());
        if (isReverseLookupBuffer)
        {
            size_t consumedQueryLength = (std::min)(suggestion.inputCount, reverseLookupQueryLength);
            pItem->_InputCount = static_cast<DWORD_PTR>(consumedQueryLength + 1);
        }
        else
        {
            pItem->_InputCount = static_cast<DWORD_PTR>(suggestion.inputCount);
        }
    }
}

//+---------------------------------------------------------------------------
//
// SetupPunctuationPair
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::SetupPunctuationPair()
{
    // Punctuation pair
    const int pair_count = 2;
    CPunctuationPair punc_quotation_mark(L'"', 0x201C, 0x201D);
    CPunctuationPair punc_apostrophe(L'\'', 0x2018, 0x2019);

    CPunctuationPair puncPairs[pair_count] = {
        punc_quotation_mark,
        punc_apostrophe,
    };

    for (int i = 0; i < pair_count; ++i)
    {
        CPunctuationPair *pPuncPair = _PunctuationPair.Append();
        *pPuncPair = puncPairs[i];
    }

    // Punctuation nest pair
    CPunctuationNestPair punc_angle_bracket(L'<', 0x300A, 0x3008, L'>', 0x300B, 0x3009);

    CPunctuationNestPair* pPuncNestPair = _PunctuationNestPair.Append();
    *pPuncNestPair = punc_angle_bracket;
}

void CCompositionProcessorEngine::InitializeJyutpingCompartment(_In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId)
{
    ApplySettingsToCompartments(pThreadMgr, tfClientId);
}

void CCompositionProcessorEngine::ApplySettingsToCompartments(_In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId)
{
    CCompartment CompartmentKeyboardOpen(pThreadMgr, tfClientId, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
    CompartmentKeyboardOpen._SetCompartmentBOOL(KeyboardOpenFromInputMethodMode(_settings.inputMethodMode));

    CCompartment CompartmentCharacterForm(pThreadMgr, tfClientId, Global::JyutpingGuidCompartmentCharacterForm);
    CompartmentCharacterForm._SetCompartmentBOOL(FullWidthFromCharacterForm(_settings.characterForm));

    CCompartment CompartmentPunctuationForm(pThreadMgr, tfClientId, Global::JyutpingGuidCompartmentPunctuationForm);
    CompartmentPunctuationForm._SetCompartmentBOOL(CantonesePunctuationFromPunctuationForm(_settings.punctuationForm));
}
//+---------------------------------------------------------------------------
//
// CompartmentCallback
//
//----------------------------------------------------------------------------

// static
HRESULT CCompositionProcessorEngine::CompartmentCallback(_In_ void *pv, REFGUID guidCompartment)
{
    CCompositionProcessorEngine* fakeThis = (CCompositionProcessorEngine*)pv;
    if (nullptr == fakeThis)
    {
        return E_INVALIDARG;
    }

    ITfThreadMgr* pThreadMgr = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER, IID_ITfThreadMgr, (void**)&pThreadMgr);
    if (FAILED(hr))
    {
        return E_FAIL;
    }

    if (IsEqualGUID(guidCompartment, Global::JyutpingGuidCompartmentCharacterForm) ||
        IsEqualGUID(guidCompartment, Global::JyutpingGuidCompartmentPunctuationForm))
    {
        fakeThis->PrivateCompartmentsUpdated(pThreadMgr);
    }
    else if (IsEqualGUID(guidCompartment, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION) ||
        IsEqualGUID(guidCompartment, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_SENTENCE))
    {
        fakeThis->ConversionModeCompartmentUpdated(pThreadMgr);
    }
    else if (IsEqualGUID(guidCompartment, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE))
    {
        fakeThis->KeyboardOpenCompartmentUpdated(pThreadMgr);
    }

    pThreadMgr->Release();
    pThreadMgr = nullptr;

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// UpdatePrivateCompartments
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::ConversionModeCompartmentUpdated(_In_ ITfThreadMgr *pThreadMgr)
{
    if (!_pCompartmentConversion)
    {
        return;
    }

    if (_isApplyingSettings)
    {
        return;
    }

    DWORD conversionMode = 0;
    if (FAILED(_pCompartmentConversion->_GetCompartmentDWORD(conversionMode)))
    {
        return;
    }

    _isMirroringConversionMode = TRUE;

    BOOL isFullWidth = FALSE;
    CCompartment CompartmentCharacterForm(pThreadMgr, _tfClientId, Global::JyutpingGuidCompartmentCharacterForm);
    if (SUCCEEDED(CompartmentCharacterForm._GetCompartmentBOOL(isFullWidth)))
    {
        if (!isFullWidth && (conversionMode & TF_CONVERSIONMODE_FULLSHAPE))
        {
            CompartmentCharacterForm._SetCompartmentBOOL(TRUE);
        }
        else if (isFullWidth && !(conversionMode & TF_CONVERSIONMODE_FULLSHAPE))
        {
            CompartmentCharacterForm._SetCompartmentBOOL(FALSE);
        }
    }
    BOOL isCantonesePunctuation = FALSE;
    CCompartment CompartmentPunctuationForm(pThreadMgr, _tfClientId, Global::JyutpingGuidCompartmentPunctuationForm);
    if (SUCCEEDED(CompartmentPunctuationForm._GetCompartmentBOOL(isCantonesePunctuation)))
    {
        if (!isCantonesePunctuation && (conversionMode & TF_CONVERSIONMODE_SYMBOL))
        {
            CompartmentPunctuationForm._SetCompartmentBOOL(TRUE);
        }
        else if (isCantonesePunctuation && !(conversionMode & TF_CONVERSIONMODE_SYMBOL))
        {
            CompartmentPunctuationForm._SetCompartmentBOOL(FALSE);
        }
    }

    BOOL fOpen = FALSE;
    CCompartment CompartmentKeyboardOpen(pThreadMgr, _tfClientId, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
    if (SUCCEEDED(CompartmentKeyboardOpen._GetCompartmentBOOL(fOpen)))
    {
        if (fOpen && !(conversionMode & TF_CONVERSIONMODE_NATIVE))
        {
            CompartmentKeyboardOpen._SetCompartmentBOOL(FALSE);
        }
        else if (!fOpen && (conversionMode & TF_CONVERSIONMODE_NATIVE))
        {
            CompartmentKeyboardOpen._SetCompartmentBOOL(TRUE);
        }
    }

    _isMirroringConversionMode = FALSE;
}

//+---------------------------------------------------------------------------
//
// PrivateCompartmentsUpdated()
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::PrivateCompartmentsUpdated(_In_ ITfThreadMgr *pThreadMgr)
{
    if (!_pCompartmentConversion)
    {
        return;
    }

    DWORD conversionMode = 0;
    DWORD conversionModePrev = 0;
    if (FAILED(_pCompartmentConversion->_GetCompartmentDWORD(conversionMode)))
    {
        return;
    }

    conversionModePrev = conversionMode;

    BOOL isFullWidth = FALSE;
    CCompartment CompartmentCharacterForm(pThreadMgr, _tfClientId, Global::JyutpingGuidCompartmentCharacterForm);
    if (SUCCEEDED(CompartmentCharacterForm._GetCompartmentBOOL(isFullWidth)))
    {
        if (!isFullWidth && (conversionMode & TF_CONVERSIONMODE_FULLSHAPE))
        {
            conversionMode &= ~TF_CONVERSIONMODE_FULLSHAPE;
        }
        else if (isFullWidth && !(conversionMode & TF_CONVERSIONMODE_FULLSHAPE))
        {
            conversionMode |= TF_CONVERSIONMODE_FULLSHAPE;
        }
    }

    BOOL isCantonesePunctuation = FALSE;
    CCompartment CompartmentPunctuationForm(pThreadMgr, _tfClientId, Global::JyutpingGuidCompartmentPunctuationForm);
    if (SUCCEEDED(CompartmentPunctuationForm._GetCompartmentBOOL(isCantonesePunctuation)))
    {
        if (!isCantonesePunctuation && (conversionMode & TF_CONVERSIONMODE_SYMBOL))
        {
            conversionMode &= ~TF_CONVERSIONMODE_SYMBOL;
        }
        else if (isCantonesePunctuation && !(conversionMode & TF_CONVERSIONMODE_SYMBOL))
        {
            conversionMode |= TF_CONVERSIONMODE_SYMBOL;
        }
    }

    if (conversionMode != conversionModePrev)
    {
        _pCompartmentConversion->_SetCompartmentDWORD(conversionMode);
    }

    if (!_isApplyingSettings && !_isMirroringConversionMode)
    {
        PersistCharacterFormFromCompartment(pThreadMgr);
        PersistPunctuationFormFromCompartment(pThreadMgr);
    }
}

void CCompositionProcessorEngine::PersistInputMethodModeFromCompartment(_In_ ITfThreadMgr *pThreadMgr)
{
    BOOL isOpen = FALSE;
    CCompartment CompartmentKeyboardOpen(pThreadMgr, _tfClientId, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
    if (FAILED(CompartmentKeyboardOpen._GetCompartmentBOOL(isOpen)))
    {
        return;
    }

    InputMethodMode mode = InputMethodModeFromKeyboardOpen(isOpen);
    _settings.inputMethodMode = mode;
    _settingsStore.SaveInputMethodMode(mode);
}

void CCompositionProcessorEngine::PersistCharacterFormFromCompartment(_In_ ITfThreadMgr *pThreadMgr)
{
    BOOL isFullWidth = FALSE;
    CCompartment CompartmentCharacterForm(pThreadMgr, _tfClientId, Global::JyutpingGuidCompartmentCharacterForm);
    if (FAILED(CompartmentCharacterForm._GetCompartmentBOOL(isFullWidth)))
    {
        return;
    }

    CharacterForm form = CharacterFormFromFullWidth(isFullWidth);
    _settings.characterForm = form;
    _settingsStore.SaveCharacterForm(form);
}

void CCompositionProcessorEngine::PersistPunctuationFormFromCompartment(_In_ ITfThreadMgr *pThreadMgr)
{
    BOOL isCantonesePunctuation = FALSE;
    CCompartment CompartmentPunctuationForm(pThreadMgr, _tfClientId, Global::JyutpingGuidCompartmentPunctuationForm);
    if (FAILED(CompartmentPunctuationForm._GetCompartmentBOOL(isCantonesePunctuation)))
    {
        return;
    }

    PunctuationForm form = PunctuationFormFromCantonesePunctuation(isCantonesePunctuation);
    _settings.punctuationForm = form;
    _settingsStore.SavePunctuationForm(form);
}

//+---------------------------------------------------------------------------
//
// KeyboardOpenCompartmentUpdated
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::KeyboardOpenCompartmentUpdated(_In_ ITfThreadMgr *pThreadMgr)
{
    if (!_pCompartmentConversion)
    {
        return;
    }

    DWORD conversionMode = 0;
    DWORD conversionModePrev = 0;
    if (FAILED(_pCompartmentConversion->_GetCompartmentDWORD(conversionMode)))
    {
        return;
    }

    conversionModePrev = conversionMode;

    BOOL isOpen = FALSE;
    CCompartment CompartmentKeyboardOpen(pThreadMgr, _tfClientId, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
    if (SUCCEEDED(CompartmentKeyboardOpen._GetCompartmentBOOL(isOpen)))
    {
        if (isOpen && !(conversionMode & TF_CONVERSIONMODE_NATIVE))
        {
            conversionMode |= TF_CONVERSIONMODE_NATIVE;
        }
        else if (!isOpen && (conversionMode & TF_CONVERSIONMODE_NATIVE))
        {
            conversionMode &= ~TF_CONVERSIONMODE_NATIVE;
        }
    }

    if (conversionMode != conversionModePrev)
    {
        _pCompartmentConversion->_SetCompartmentDWORD(conversionMode);
    }

    if (!_isApplyingSettings && !_isMirroringConversionMode)
    {
        PersistInputMethodModeFromCompartment(pThreadMgr);
    }
}


//////////////////////////////////////////////////////////////////////
//
// XPreservedKey implementation.
//
//////////////////////////////////////////////////////////////////////

//+---------------------------------------------------------------------------
//
// UninitPreservedKey
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::XPreservedKey::UninitPreservedKey(_In_ ITfThreadMgr *pThreadMgr)
{
    ITfKeystrokeMgr* pKeystrokeMgr = nullptr;

    if (IsEqualGUID(Guid, GUID_NULL))
    {
        return FALSE;
    }

    if (FAILED(pThreadMgr->QueryInterface(IID_ITfKeystrokeMgr, (void **)&pKeystrokeMgr)))
    {
        return FALSE;
    }

    for (UINT i = 0; i < TSFPreservedKeyTable.Count(); i++)
    {
        TF_PRESERVEDKEY pPreservedKey = *TSFPreservedKeyTable.GetAt(i);
        pPreservedKey.uModifiers &= 0xffff;

        pKeystrokeMgr->UnpreserveKey(Guid, &pPreservedKey);
    }

    pKeystrokeMgr->Release();

    return TRUE;
}

CCompositionProcessorEngine::XPreservedKey::XPreservedKey()
{
    Guid = GUID_NULL;
    Description = nullptr;
}

CCompositionProcessorEngine::XPreservedKey::~XPreservedKey()
{
    ITfThreadMgr* pThreadMgr = nullptr;

    HRESULT hr = CoCreateInstance(CLSID_TF_ThreadMgr, NULL, CLSCTX_INPROC_SERVER, IID_ITfThreadMgr, (void**)&pThreadMgr);
    if (SUCCEEDED(hr))
    {
        UninitPreservedKey(pThreadMgr);
        pThreadMgr->Release();
        pThreadMgr = nullptr;
    }

    if (Description)
    {
        delete [] Description;
    }
}


HRESULT CJyutping::CreateInstance(REFCLSID rclsid, REFIID riid, _Outptr_result_maybenull_ LPVOID* ppv, _Out_opt_ HINSTANCE* phInst, BOOL isComLessMode)
{
    HRESULT hr = S_OK;
    if (phInst == nullptr)
    {
        return E_INVALIDARG;
    }

    *phInst = nullptr;

    if (!isComLessMode)
    {
        hr = ::CoCreateInstance(rclsid,
            NULL,
            CLSCTX_INPROC_SERVER,
            riid,
            ppv);
    }
    else
    {
        hr = CJyutping::ComLessCreateInstance(rclsid, riid, ppv, phInst);
    }

    return hr;
}


HRESULT CJyutping::ComLessCreateInstance(REFGUID rclsid, REFIID riid, _Outptr_result_maybenull_ void **ppv, _Out_opt_ HINSTANCE *phInst)
{
    HRESULT hr = S_OK;
    HINSTANCE jyutpingDllHandle = nullptr;
    WCHAR wchPath[MAX_PATH] = {'\0'};
    WCHAR szExpandedPath[MAX_PATH] = {'\0'};
    DWORD dwCnt = 0;
    *ppv = nullptr;

    hr = phInst ? S_OK : E_FAIL;
    if (SUCCEEDED(hr))
    {
        *phInst = nullptr;
        hr = CJyutping::GetComModuleName(rclsid, wchPath, ARRAYSIZE(wchPath));
        if (SUCCEEDED(hr))
        {
            dwCnt = ExpandEnvironmentStringsW(wchPath, szExpandedPath, ARRAYSIZE(szExpandedPath));
            hr = (0 < dwCnt && dwCnt <= ARRAYSIZE(szExpandedPath)) ? S_OK : E_FAIL;
            if (SUCCEEDED(hr))
            {
                jyutpingDllHandle = LoadLibraryEx(szExpandedPath, NULL, 0);
                hr = jyutpingDllHandle ? S_OK : E_FAIL;
                if (SUCCEEDED(hr))
                {
                    *phInst = jyutpingDllHandle;
                    FARPROC pfn = GetProcAddress(jyutpingDllHandle, "DllGetClassObject");
                    hr = pfn ? S_OK : E_FAIL;
                    if (SUCCEEDED(hr))
                    {
                        IClassFactory *pClassFactory = nullptr;
                        hr = ((HRESULT (STDAPICALLTYPE *)(REFCLSID rclsid, REFIID riid, LPVOID *ppv))(pfn))(rclsid, IID_IClassFactory, (void **)&pClassFactory);
                        if (SUCCEEDED(hr) && pClassFactory)
                        {
                            hr = pClassFactory->CreateInstance(NULL, riid, ppv);
                            pClassFactory->Release();
                        }
                    }
                }
            }
        }
    }

    if (!SUCCEEDED(hr) && phInst && *phInst)
    {
        FreeLibrary(*phInst);
        *phInst = 0;
    }
    return hr;
}


HRESULT CJyutping::GetComModuleName(REFGUID rclsid, _Out_writes_(cchPath)WCHAR* wchPath, DWORD cchPath)
{
    HRESULT hr = S_OK;

    CRegKey key;
    WCHAR wchClsid[CLSID_STRLEN + 1];
    hr = CLSIDToString(rclsid, wchClsid) ? S_OK : E_FAIL;
    if (SUCCEEDED(hr))
    {
        WCHAR wchKey[MAX_PATH];
        hr = StringCchPrintfW(wchKey, ARRAYSIZE(wchKey), L"CLSID\\%s\\InProcServer32", wchClsid);
        if (SUCCEEDED(hr))
        {
            hr = (key.Open(HKEY_CLASSES_ROOT, wchKey, KEY_READ) == ERROR_SUCCESS) ? S_OK : E_FAIL;
            if (SUCCEEDED(hr))
            {
                WCHAR wszModel[MAX_PATH];
                ULONG cch = ARRAYSIZE(wszModel);
                hr = (key.QueryStringValue(L"ThreadingModel", wszModel, &cch) == ERROR_SUCCESS) ? S_OK : E_FAIL;
                if (SUCCEEDED(hr))
                {
                    if (CompareStringOrdinal(wszModel,
                        -1,
                        L"Apartment",
                        -1,
                        TRUE) == CSTR_EQUAL)
                    {
                        hr = (key.QueryStringValue(NULL, wchPath, &cchPath) == ERROR_SUCCESS) ? S_OK : E_FAIL;
                    }
                    else
                    {
                        hr = E_FAIL;
                    }
                }
            }
        }
    }

    return hr;
}


void CCompositionProcessorEngine::ShowAllLanguageBarIcons()
{
    SetLanguageBarStatus(TF_LBI_STATUS_HIDDEN, FALSE);
}

void CCompositionProcessorEngine::HideAllLanguageBarIcons()
{
    SetLanguageBarStatus(TF_LBI_STATUS_HIDDEN, TRUE);
}
