#include "Private.h"
#include "globals.h"
#include "Jyutping.h"
#include "CandidateListUIPresenter.h"
#include "Logger.h"
#include "CompositionProcessorEngine.h"
#include "Compartment.h"
#include "EditSession.h"
#include "Localization.h"
#include "Settings.h"
#include "resource.h"

namespace
{
class COptionsStartEditSession final : public CEditSessionBase
{
public:
    COptionsStartEditSession(_In_ CJyutping *pTextService, _In_ ITfContext *pContext) :
        CEditSessionBase(pTextService, pContext)
    {
    }

    STDMETHODIMP DoEditSession(TfEditCookie ec) override
    {
        return _pTextService->_StartOptions(ec, _pContext);
    }
};

class COptionsSelectionEditSession final : public CEditSessionBase
{
public:
    COptionsSelectionEditSession(_In_ CJyutping *pTextService, _In_ ITfContext *pContext, UINT row) :
        CEditSessionBase(pTextService, pContext), _row(row)
    {
    }

    STDMETHODIMP DoEditSession(TfEditCookie ec) override
    {
        return _pTextService->_ApplyOptionsSelection(ec, _pContext, _row);
    }

private:
    UINT _row;
};
}

//+---------------------------------------------------------------------------
//
// CreateInstance
//
//----------------------------------------------------------------------------

/* static */
HRESULT CJyutping::CreateInstance(_In_ IUnknown *pUnkOuter, REFIID riid, _Outptr_ void **ppvObj)
{
    CJyutping* pJyutping = nullptr;
    HRESULT hr = S_OK;

    if (ppvObj == nullptr)
    {
        return E_INVALIDARG;
    }

    *ppvObj = nullptr;

    if (nullptr != pUnkOuter)
    {
        return CLASS_E_NOAGGREGATION;
    }

    pJyutping = new (std::nothrow) CJyutping();
    if (pJyutping == nullptr)
    {
        return E_OUTOFMEMORY;
    }

    hr = pJyutping->QueryInterface(riid, ppvObj);

    pJyutping->Release();

    return hr;
}

//+---------------------------------------------------------------------------
//
// ctor
//
//----------------------------------------------------------------------------

CJyutping::CJyutping()
{
    DllAddRef();

    _pThreadMgr = nullptr;

    _threadMgrEventSinkCookie = TF_INVALID_COOKIE;

    _pTextEditSinkContext = nullptr;
    _textEditSinkCookie = TF_INVALID_COOKIE;

    _activeLanguageProfileNotifySinkCookie = TF_INVALID_COOKIE;

    _dwThreadFocusSinkCookie = TF_INVALID_COOKIE;

    _pComposition = nullptr;

    _pCompositionProcessorEngine = nullptr;

    _candidateMode = CANDIDATE_NONE;
    _pCandidateListUIPresenter = nullptr;
    _optionsMode = FALSE;
    _optionsStandalonePresenter = FALSE;

    _pDocMgrLastFocused = nullptr;

    _pSIPIMEOnOffCompartment = nullptr;
    _dwSIPIMEOnOffCompartmentSinkCookie = 0;
    _msgWndHandle = nullptr;

    _pContext = nullptr;

    _refCount = 1;
}

//+---------------------------------------------------------------------------
//
// dtor
//
//----------------------------------------------------------------------------

CJyutping::~CJyutping()
{
    if (_pCandidateListUIPresenter)
    {
        delete _pCandidateListUIPresenter;
        _pCandidateListUIPresenter = nullptr;
    }
    DllRelease();
}

void CJyutping::_BuildOptionsRows(_Out_writes_(10) COptionsView::Row* rows)
{
    if (rows == nullptr)
    {
        return;
    }
    static const UINT resourceIds[10] = {
        IDS_MENU_CHARACTER_VARIANT_TRADITIONAL,
        IDS_MENU_CHARACTER_VARIANT_HONG_KONG,
        IDS_MENU_CHARACTER_VARIANT_TAIWAN,
        IDS_MENU_CHARACTER_VARIANT_SIMPLIFIED,
        IDS_MENU_CHARACTER_FORM_HALF_WIDTH,
        IDS_MENU_CHARACTER_FORM_FULL_WIDTH,
        IDS_MENU_PUNCTUATION_FORM_CANTONESE,
        IDS_MENU_PUNCTUATION_FORM_ENGLISH,
        IDS_OPTIONS_INPUT_MODE_CANTONESE,
        IDS_OPTIONS_INPUT_MODE_ABC
    };
    static const WCHAR* fallbackLabels[10] = { L"Traditional", L"Hong Kong", L"Taiwan", L"Simplified", L"Half-width", L"Full-width", L"Cantonese", L"English", L"Cantonese", L"ABC" };
    CharacterVariant variant = _pCompositionProcessorEngine ? _pCompositionProcessorEngine->CurrentCharacterVariant() : CharacterVariant::Traditional;
    CharacterForm characterForm = _pCompositionProcessorEngine ? _pCompositionProcessorEngine->CurrentCharacterForm() : CharacterForm::HalfWidth;
    PunctuationForm punctuationForm = _pCompositionProcessorEngine ? _pCompositionProcessorEngine->CurrentPunctuationForm() : PunctuationForm::Cantonese;
    InputMethodMode inputMode = _pCompositionProcessorEngine ? _pCompositionProcessorEngine->CurrentInputMethodMode() : InputMethodMode::Cantonese;
    const BOOL selected[10] = { variant == CharacterVariant::Traditional, variant == CharacterVariant::HongKong, variant == CharacterVariant::Taiwan, variant == CharacterVariant::Simplified, characterForm == CharacterForm::HalfWidth, characterForm == CharacterForm::FullWidth, punctuationForm == PunctuationForm::Cantonese, punctuationForm == PunctuationForm::English, inputMode == InputMethodMode::Cantonese, inputMode == InputMethodMode::ABC };
    const UINT ids[10] = { COptionsView::CharacterTraditional, COptionsView::CharacterHongKong, COptionsView::CharacterTaiwan, COptionsView::CharacterSimplified, COptionsView::CharacterHalfWidth, COptionsView::CharacterFullWidth, COptionsView::PunctuationCantonese, COptionsView::PunctuationEnglish, COptionsView::InputCantonese, COptionsView::InputABC };
    for (UINT index = 0; index < 10; ++index)
    {
        rows[index] = { ids[index], Localization::LoadStringOrFallback(resourceIds[index], fallbackLabels[index]), selected[index], index == 4 || index == 6 || index == 8 };
    }
}

HRESULT CJyutping::ToggleOptionsMode(_In_ ITfContext *pContext)
{
    if (_optionsMode)
    {
        if (_pCandidateListUIPresenter)
        {
            _pCandidateListUIPresenter->_ExitOptions();
            if (_optionsStandalonePresenter)
            {
                _pCandidateListUIPresenter->_EndCandidateList();
                delete _pCandidateListUIPresenter;
                _pCandidateListUIPresenter = nullptr;
                _optionsStandalonePresenter = FALSE;
            }
        }
        _optionsMode = FALSE;
        return S_OK;
    }
    if (_pCandidateListUIPresenter != nullptr)
    {
        COptionsView::Row rows[10] = {};
        _BuildOptionsRows(rows);
        _pCandidateListUIPresenter->_EnterOptions(rows, 0);
        _optionsMode = TRUE;
        return S_OK;
    }

    if (pContext == nullptr || _pCompositionProcessorEngine == nullptr)
    {
        return E_INVALIDARG;
    }
    COptionsStartEditSession *pEditSession = new (std::nothrow) COptionsStartEditSession(this, pContext);
    if (pEditSession == nullptr)
    {
        return E_OUTOFMEMORY;
    }
    HRESULT editSessionResult = E_FAIL;
    HRESULT hr = pContext->RequestEditSession(_tfClientId, pEditSession, TF_ES_SYNC | TF_ES_READ, &editSessionResult);
    if (editSessionResult == TF_E_SYNCHRONOUS)
    {
        hr = pContext->RequestEditSession(_tfClientId, pEditSession, TF_ES_ASYNCDONTCARE | TF_ES_READ, &editSessionResult);
    }
    pEditSession->Release();
    return FAILED(hr) ? hr : editSessionResult;
}

HRESULT CJyutping::_StartOptions(TfEditCookie ec, _In_ ITfContext *pContext)
{
    if (_optionsMode || pContext == nullptr || _pCompositionProcessorEngine == nullptr)
    {
        return _optionsMode ? S_OK : E_INVALIDARG;
    }
    if (_pCandidateListUIPresenter == nullptr)
    {
        _pCandidateListUIPresenter = new (std::nothrow) CCandidateListUIPresenter(this, Global::AtomCandidateWindow, CATEGORY_CANDIDATE, _pCompositionProcessorEngine->GetCandidateListIndexRange(), FALSE);
        if (_pCandidateListUIPresenter == nullptr)
        {
            return E_OUTOFMEMORY;
        }
        HRESULT hr = _pCandidateListUIPresenter->_StartOptions(pContext, ec);
        if (FAILED(hr))
        {
            delete _pCandidateListUIPresenter;
            _pCandidateListUIPresenter = nullptr;
            return hr;
        }
        _optionsStandalonePresenter = TRUE;
    }
    COptionsView::Row rows[10] = {};
    _BuildOptionsRows(rows);
    _pCandidateListUIPresenter->_EnterOptions(rows, 0);
    _optionsMode = TRUE;
    return S_OK;
}

HRESULT CJyutping::_HandleOptionsSelection(UINT row)
{
    if (!_optionsMode || _pCandidateListUIPresenter == nullptr || row >= 10)
    {
        return S_FALSE;
    }
    ITfContext* pContext = _pCandidateListUIPresenter->_GetContextDocument();
    if (pContext == nullptr)
    {
        return E_UNEXPECTED;
    }
    COptionsSelectionEditSession *pEditSession = new (std::nothrow) COptionsSelectionEditSession(this, pContext, row);
    if (pEditSession == nullptr)
    {
        return E_OUTOFMEMORY;
    }
    HRESULT editSessionResult = E_FAIL;
    HRESULT hr = pContext->RequestEditSession(_tfClientId, pEditSession, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, &editSessionResult);
    pEditSession->Release();
    return FAILED(hr) ? hr : editSessionResult;
}

HRESULT CJyutping::_ApplyOptionsSelection(TfEditCookie ec, _In_ ITfContext *pContext, UINT row)
{
    if (!_optionsMode || _pCompositionProcessorEngine == nullptr || pContext == nullptr || row >= 10)
    {
        return S_FALSE;
    }
    if (row == COptionsView::InputABC &&
        _pCompositionProcessorEngine->CurrentInputMethodMode() != InputMethodMode::ABC && _IsComposing())
    {
        ToggleOptionsMode(pContext);
        HRESULT hr = _HandleCompositionFinalizeRaw(ec, pContext);
        if (FAILED(hr))
        {
            return hr;
        }
    }
    switch (row)
    {
    case COptionsView::CharacterTraditional: _pCompositionProcessorEngine->SetCharacterVariant(CharacterVariant::Traditional); break;
    case COptionsView::CharacterHongKong: _pCompositionProcessorEngine->SetCharacterVariant(CharacterVariant::HongKong); break;
    case COptionsView::CharacterTaiwan: _pCompositionProcessorEngine->SetCharacterVariant(CharacterVariant::Taiwan); break;
    case COptionsView::CharacterSimplified: _pCompositionProcessorEngine->SetCharacterVariant(CharacterVariant::Simplified); break;
    case COptionsView::CharacterHalfWidth: _pCompositionProcessorEngine->SetCharacterForm(CharacterForm::HalfWidth); break;
    case COptionsView::CharacterFullWidth: _pCompositionProcessorEngine->SetCharacterForm(CharacterForm::FullWidth); break;
    case COptionsView::PunctuationCantonese: _pCompositionProcessorEngine->SetPunctuationForm(PunctuationForm::Cantonese); break;
    case COptionsView::PunctuationEnglish: _pCompositionProcessorEngine->SetPunctuationForm(PunctuationForm::English); break;
    case COptionsView::InputCantonese: _pCompositionProcessorEngine->SetInputMethodMode(InputMethodMode::Cantonese); break;
    case COptionsView::InputABC: _pCompositionProcessorEngine->SetInputMethodMode(InputMethodMode::ABC); break;
    default: return E_INVALIDARG;
    }
    if (_optionsMode)
    {
        ToggleOptionsMode(pContext);
    }
    return S_OK;
}

BOOL CJyutping::_HandleOptionsKey(_In_ ITfContext *pContext, UINT code)
{
    if (!_optionsMode || _pCandidateListUIPresenter == nullptr)
    {
        return FALSE;
    }
    if (code >= '1' && code <= '9')
    {
        _pCandidateListUIPresenter->_SetOptionsSelection(code - '1');
        _HandleOptionsSelection(code - '1');
        return TRUE;
    }
    if (code == '0')
    {
        _pCandidateListUIPresenter->_SetOptionsSelection(9);
        _HandleOptionsSelection(9);
        return TRUE;
    }
    switch (code)
    {
    case VK_UP: _pCandidateListUIPresenter->_MoveOptionsSelection(-1); return TRUE;
    case VK_DOWN: _pCandidateListUIPresenter->_MoveOptionsSelection(1); return TRUE;
    case VK_TAB: _pCandidateListUIPresenter->_MoveOptionsSelection(Global::CheckModifiers(Global::ModifiersValue, TF_MOD_SHIFT) ? -1 : 1); return TRUE;
    case VK_RETURN:
    case VK_SPACE: return SUCCEEDED(_HandleOptionsSelection(_pCandidateListUIPresenter->_GetOptionsSelection()));
    case VK_ESCAPE:
    case VK_BACK: ToggleOptionsMode(pContext); return TRUE;
    default: return TRUE;
    }
}

//+---------------------------------------------------------------------------
//
// QueryInterface
//
//----------------------------------------------------------------------------

STDAPI CJyutping::QueryInterface(REFIID riid, _Outptr_ void **ppvObj)
{
    if (ppvObj == nullptr)
    {
        return E_INVALIDARG;
    }

    *ppvObj = nullptr;

    if (IsEqualIID(riid, IID_IUnknown) ||
        IsEqualIID(riid, IID_ITfTextInputProcessor))
    {
        *ppvObj = (ITfTextInputProcessor *)this;
    }
    else if (IsEqualIID(riid, IID_ITfTextInputProcessorEx))
    {
        *ppvObj = (ITfTextInputProcessorEx *)this;
    }
    else if (IsEqualIID(riid, IID_ITfThreadMgrEventSink))
    {
        *ppvObj = (ITfThreadMgrEventSink *)this;
    }
    else if (IsEqualIID(riid, IID_ITfTextEditSink))
    {
        *ppvObj = (ITfTextEditSink *)this;
    }
    else if (IsEqualIID(riid, IID_ITfKeyEventSink))
    {
        *ppvObj = (ITfKeyEventSink *)this;
    }
    else if (IsEqualIID(riid, IID_ITfActiveLanguageProfileNotifySink))
    {
        *ppvObj = (ITfActiveLanguageProfileNotifySink *)this;
    }
    else if (IsEqualIID(riid, IID_ITfCompositionSink))
    {
        *ppvObj = (ITfKeyEventSink *)this;
    }
    else if (IsEqualIID(riid, IID_ITfDisplayAttributeProvider))
    {
        *ppvObj = (ITfDisplayAttributeProvider *)this;
    }
    else if (IsEqualIID(riid, IID_ITfThreadFocusSink))
    {
        *ppvObj = (ITfThreadFocusSink *)this;
    }
    else if (IsEqualIID(riid, IID_ITfFunctionProvider))
    {
        *ppvObj = (ITfFunctionProvider *)this;
    }
    else if (IsEqualIID(riid, IID_ITfFunction))
    {
        *ppvObj = (ITfFunction *)this;
    }
    else if (IsEqualIID(riid, IID_ITfFnGetPreferredTouchKeyboardLayout))
    {
        *ppvObj = (ITfFnGetPreferredTouchKeyboardLayout *)this;
    }

    if (*ppvObj)
    {
        AddRef();
        return S_OK;
    }

    return E_NOINTERFACE;
}


//+---------------------------------------------------------------------------
//
// AddRef
//
//----------------------------------------------------------------------------

STDAPI_(ULONG) CJyutping::AddRef()
{
    return ++_refCount;
}

//+---------------------------------------------------------------------------
//
// Release
//
//----------------------------------------------------------------------------

STDAPI_(ULONG) CJyutping::Release()
{
    LONG cr = --_refCount;

    assert(_refCount >= 0);

    if (_refCount == 0)
    {
        delete this;
    }

    return cr;
}

//+---------------------------------------------------------------------------
//
// ITfTextInputProcessorEx::ActivateEx
//
//----------------------------------------------------------------------------

STDAPI CJyutping::ActivateEx(ITfThreadMgr *pThreadMgr, TfClientId tfClientId, DWORD dwFlags)
{
    Global::Log(L"ActivateEx start: clientId=%lu flags=0x%08X", tfClientId, dwFlags);
    if (pThreadMgr == nullptr)
    {
        Global::Log(L"ActivateEx failed: thread manager is null");
        return E_INVALIDARG;
    }

    _pThreadMgr = pThreadMgr;
    _pThreadMgr->AddRef();

    _tfClientId = tfClientId;
    _dwActivateFlags = dwFlags;

    if (!_InitThreadMgrEventSink())
    {
        Global::Log(L"ActivateEx: _InitThreadMgrEventSink failed");
        Deactivate();
        return E_FAIL;
    }

    ITfDocumentMgr* pDocMgrFocus = nullptr;
    HRESULT hrFocus = _pThreadMgr->GetFocus(&pDocMgrFocus);
    if (SUCCEEDED(hrFocus) && (pDocMgrFocus != nullptr))
    {
        _InitTextEditSink(pDocMgrFocus);
        pDocMgrFocus->Release();
    }
    else
    {
        Global::Log(L"ActivateEx: focused document manager unavailable: hr=0x%08X", static_cast<unsigned int>(hrFocus));
    }

    if (!_InitKeyEventSink())
    {
        Global::Log(L"ActivateEx: _InitKeyEventSink failed");
        Deactivate();
        return E_FAIL;
    }

    if (!_InitActiveLanguageProfileNotifySink())
    {
        Global::Log(L"ActivateEx: _InitActiveLanguageProfileNotifySink failed");
        Deactivate();
        return E_FAIL;
    }

    if (!_InitThreadFocusSink())
    {
        Global::Log(L"ActivateEx: _InitThreadFocusSink failed");
        Deactivate();
        return E_FAIL;
    }

    if (!_InitDisplayAttributeGuidAtom())
    {
        Global::Log(L"ActivateEx: _InitDisplayAttributeGuidAtom failed");
        Deactivate();
        return E_FAIL;
    }

    if (!_InitFunctionProviderSink())
    {
        Global::Log(L"ActivateEx: _InitFunctionProviderSink failed");
        Deactivate();
        return E_FAIL;
    }

    if (!_AddTextProcessorEngine())
    {
        Global::Log(L"ActivateEx: _AddTextProcessorEngine failed");
        Deactivate();
        return E_FAIL;
    }

    if (!Global::InitDirectWrite())
    {
        Global::Log(L"ActivateEx: InitDirectWrite failed");
    }

    Global::Log(L"ActivateEx success");
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfTextInputProcessorEx::Deactivate
//
//----------------------------------------------------------------------------

STDAPI CJyutping::Deactivate()
{
    Global::Log(L"Deactivate start");
    if (_pCompositionProcessorEngine)
    {
        delete _pCompositionProcessorEngine;
        _pCompositionProcessorEngine = nullptr;
    }

    ITfContext* pContext = _pContext;
    if (_pContext)
    {
        pContext->AddRef();
        _EndComposition(_pContext);
    }

    if (_pCandidateListUIPresenter)
    {
        delete _pCandidateListUIPresenter;
        _pCandidateListUIPresenter = nullptr;

        if (pContext)
        {
            pContext->Release();
        }

        _candidateMode = CANDIDATE_NONE;
    }

    _UninitFunctionProviderSink();

    _UninitThreadFocusSink();

    _UninitActiveLanguageProfileNotifySink();

    _UninitKeyEventSink();

    _UninitThreadMgrEventSink();

    CCompartment CompartmentKeyboardOpen(_pThreadMgr, _tfClientId, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
    CompartmentKeyboardOpen._ClearCompartment();

    CCompartment CompartmentCharacterForm(_pThreadMgr, _tfClientId, Global::JyutpingGuidCompartmentCharacterForm);
    CompartmentCharacterForm._ClearCompartment();

    CCompartment CompartmentPunctuationForm(_pThreadMgr, _tfClientId, Global::JyutpingGuidCompartmentPunctuationForm);
    CompartmentPunctuationForm._ClearCompartment();

    if (_pThreadMgr != nullptr)
    {
        _pThreadMgr->Release();
    }

    _tfClientId = TF_CLIENTID_NULL;

    if (_pDocMgrLastFocused)
    {
        _pDocMgrLastFocused->Release();
        _pDocMgrLastFocused = nullptr;
    }

    Global::UninitDirectWrite();

    Global::Log(L"Deactivate success");
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfFunctionProvider::GetType
//
//----------------------------------------------------------------------------
HRESULT CJyutping::GetType(__RPC__out GUID *pguid)
{
    HRESULT hr = E_INVALIDARG;
    if (pguid)
    {
        *pguid = Global::JyutpingCLSID;
        hr = S_OK;
    }
    return hr;
}

//+---------------------------------------------------------------------------
//
// ITfFunctionProvider::::GetDescription
//
//----------------------------------------------------------------------------
HRESULT CJyutping::GetDescription(__RPC__deref_out_opt BSTR *pbstrDesc)
{
    HRESULT hr = E_INVALIDARG;
    if (pbstrDesc != nullptr)
    {
        *pbstrDesc = nullptr;
        hr = E_NOTIMPL;
    }
    return hr;
}

//+---------------------------------------------------------------------------
//
// ITfFunctionProvider::::GetFunction
//
//----------------------------------------------------------------------------
HRESULT CJyutping::GetFunction(__RPC__in REFGUID rguid, __RPC__in REFIID riid, __RPC__deref_out_opt IUnknown **ppunk)
{
    HRESULT hr = E_NOINTERFACE;

    if ((IsEqualGUID(rguid, GUID_NULL))
        && (IsEqualGUID(riid, __uuidof(ITfFnSearchCandidateProvider))))
    {
        hr = _pITfFnSearchCandidateProvider->QueryInterface(riid, (void**)ppunk);
    }
    else if (IsEqualGUID(rguid, GUID_NULL))
    {
        hr = QueryInterface(riid, (void **)ppunk);
    }

    return hr;
}

//+---------------------------------------------------------------------------
//
// ITfFunction::GetDisplayName
//
//----------------------------------------------------------------------------
HRESULT CJyutping::GetDisplayName(_Out_ BSTR *pbstrDisplayName)
{
    HRESULT hr = E_INVALIDARG;
    if (pbstrDisplayName != nullptr)
    {
        *pbstrDisplayName = nullptr;
        hr = E_NOTIMPL;
    }
    return hr;
}

//+---------------------------------------------------------------------------
//
// ITfFnGetPreferredTouchKeyboardLayout::GetLayout
// The tkblayout will be Optimized layout.
//----------------------------------------------------------------------------
HRESULT CJyutping::GetLayout(_Out_ TKBLayoutType *ptkblayoutType, _Out_ WORD *pwPreferredLayoutId)
{
    HRESULT hr = E_INVALIDARG;
    if ((ptkblayoutType != nullptr) && (pwPreferredLayoutId != nullptr))
    {
        *ptkblayoutType = TKBLT_OPTIMIZED;
        *pwPreferredLayoutId = TKBL_OPT_SIMPLIFIED_CHINESE_PINYIN;
        hr = S_OK;
    }
    return hr;
}
