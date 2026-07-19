#include "Private.h"
#include "Globals.h"
#include "Jyutping.h"
#include "CandidateListUIPresenter.h"
#include "CompositionProcessorEngine.h"
#include "EditSession.h"
#include "KeyHandlerEditSession.h"
#include "Compartment.h"
#include "Logger.h"
#include "PunctuationKey.h"
#include "VirtualInputKey.h"

// 0xF003, 0xF004 are the keys that the touch keyboard sends for next/previous
#define THIRDPARTY_NEXTPAGE  static_cast<WORD>(0xF003)
#define THIRDPARTY_PREVPAGE  static_cast<WORD>(0xF004)

namespace {

class CInputMethodModeEditSession final : public CEditSessionBase
{
public:
    CInputMethodModeEditSession(_In_ CJyutping *pTextService, _In_ ITfContext *pContext) :
        CEditSessionBase(pTextService, pContext)
    {
    }

    STDMETHODIMP DoEditSession(TfEditCookie ec) override
    {
        HRESULT hr = _pTextService->_HandleCompositionFinalizeRaw(ec, _pContext);
        if (FAILED(hr))
        {
            return hr;
        }

        CCompositionProcessorEngine *pCompositionProcessorEngine = _pTextService->GetCompositionProcessorEngine();
        if (pCompositionProcessorEngine == nullptr)
        {
            return E_FAIL;
        }

        return pCompositionProcessorEngine->ToggleInputMethodMode(
            _pTextService->_GetThreadMgr(),
            _pTextService->_GetClientId());
    }
};

BOOL IsPageNavigationFunction(KEYSTROKE_FUNCTION function)
{
    return function == FUNCTION_MOVE_PAGE_UP || function == FUNCTION_MOVE_PAGE_DOWN;
}

} // namespace

// Because the code mostly works with VKeys, here map a WCHAR back to a VKKey for certain
// vkeys that the IME handles specially
__inline UINT VKeyFromVKPacketAndWchar(UINT vk, WCHAR wch)
{
    UINT vkRet = vk;
    if (LOWORD(vk) == VK_PACKET)
    {
        VirtualInputKey inputKey;
        if (VirtualInputKey::MatchInputKeyForCharacter(wch, &inputKey))
        {
            vkRet = inputKey.keyCode;
        }
        else if (wch == L' ')
        {
            vkRet = VK_SPACE;
        }
        else if (wch == L'-')
        {
            vkRet = VK_OEM_MINUS;
        }
        else if (wch == L'=')
        {
            vkRet = VK_OEM_PLUS;
        }
        else if (wch == L'[')
        {
            vkRet = VK_OEM_4;
        }
        else if (wch == L']')
        {
            vkRet = VK_OEM_6;
        }
        else if (wch == THIRDPARTY_NEXTPAGE)
        {
            vkRet = VK_NEXT;
        }
        else if (wch == THIRDPARTY_PREVPAGE)
        {
            vkRet = VK_PRIOR;
        }
    }
    return vkRet;
}

//+---------------------------------------------------------------------------
//
// _IsKeyEaten
//
//----------------------------------------------------------------------------

BOOL CJyutping::_IsKeyEaten(_In_ ITfContext *pContext, UINT codeIn, _Out_ UINT *pCodeOut, _Out_writes_(1) WCHAR *pwch, _Out_opt_ _KEYSTROKE_STATE *pKeyState)
{
    pContext;

    *pCodeOut = codeIn;

    BOOL isCantoneseMode = FALSE;
    CCompartment CompartmentKeyboardOpen(_pThreadMgr, _tfClientId, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
    CompartmentKeyboardOpen._GetCompartmentBOOL(isCantoneseMode);

    BOOL isFullWidth = FALSE;
    CCompartment CompartmentCharacterForm(_pThreadMgr, _tfClientId, Global::JyutpingGuidCompartmentCharacterForm);
    CompartmentCharacterForm._GetCompartmentBOOL(isFullWidth);

    if (pKeyState)
    {
        pKeyState->Category = CATEGORY_NONE;
        pKeyState->Function = FUNCTION_NONE;
    }
    if (pwch)
    {
        *pwch = L'\0';
    }

    // if the keyboard is disabled, we don't eat keys.
    if (_IsKeyboardDisabled())
    {
        return FALSE;
    }

    //
    // Map virtual key to character code
    //
    BOOL isTouchKeyboardSpecialKeys = FALSE;
    WCHAR wch = ConvertVKey(codeIn);
    *pCodeOut = VKeyFromVKPacketAndWchar(codeIn, wch);
    if ((wch == THIRDPARTY_NEXTPAGE) || (wch == THIRDPARTY_PREVPAGE))
    {
        // We always eat the above softkeyboard special keys
        isTouchKeyboardSpecialKeys = TRUE;
        if (pwch)
        {
            *pwch = wch;
        }
    }

    // In ABC mode, don't eat keys, with the exception of touch keyboard special keys.
    if (!isCantoneseMode)
    {
        return isTouchKeyboardSpecialKeys;
    }

    if (pwch)
    {
        *pwch = wch;
    }

    //
    // Get composition engine
    //
    CCompositionProcessorEngine *pCompositionProcessorEngine;
    pCompositionProcessorEngine = _pCompositionProcessorEngine;

    BOOL isNoModifier = Global::CheckModifiers(Global::ModifiersValue, 0);
    BOOL isShifting = Global::CheckModifiers(Global::ModifiersValue, TF_MOD_SHIFT);
    const PunctuationKey* punctuationKey = (isNoModifier || isShifting) ? PunctuationKey::ForVirtualKey(*pCodeOut) : nullptr;
    BOOL shouldHandlePunctuation = punctuationKey != nullptr && punctuationKey->ShouldHandle(isShifting);
    BOOL isUnshiftedApostrophe = shouldHandlePunctuation && !isShifting &&
        punctuationKey->keyCode == VK_OEM_7;

    if (isCantoneseMode)
    {
        //
        // The candidate or phrase list handles the keys through ITfKeyEventSink.
        //
        // eat only keys that CKeyHandlerEditSession can handles.
        //
        _KEYSTROKE_STATE evaluatedKeyState;
        _KEYSTROKE_STATE* pEvaluatedKeyState = pKeyState != nullptr ? pKeyState : &evaluatedKeyState;
        if (pCompositionProcessorEngine->IsVirtualKeyNeed(*pCodeOut, pwch, _IsComposing(), _candidateMode, pEvaluatedKeyState))
        {
            BOOL isSyllableSeparator = isUnshiftedApostrophe &&
                pEvaluatedKeyState->Function == FUNCTION_INPUT;
            if (!shouldHandlePunctuation || isSyllableSeparator)
            {
                return TRUE;
            }
            if (!isShifting && IsPageNavigationFunction(pEvaluatedKeyState->Function))
            {
                return TRUE;
            }
        }
    }

    //
    // Punctuation
    //
    if (shouldHandlePunctuation)
    {
        if (pKeyState)
        {
            pKeyState->Category = CATEGORY_COMPOSING;
            pKeyState->Function = FUNCTION_PUNCTUATION_KEY;
        }
        return TRUE;
    }

    //
    // Character form
    //
    if (isFullWidth && pCompositionProcessorEngine->IsCharacterFormConvertible(wch))
    {
        if (_candidateMode == CANDIDATE_NONE)
        {
            if (pKeyState)
            {
                pKeyState->Category = CATEGORY_COMPOSING;
                pKeyState->Function = FUNCTION_CHARACTER_FORM;
            }
            return TRUE;
        }
    }

    return isTouchKeyboardSpecialKeys;
}

//+---------------------------------------------------------------------------
//
// ConvertVKey
//
//----------------------------------------------------------------------------

WCHAR CJyutping::ConvertVKey(UINT code)
{
    //
    // Map virtual key to scan code
    //
    UINT scanCode = 0;
    scanCode = MapVirtualKey(code, 0);

    //
    // Keyboard state
    //
    BYTE abKbdState[256] = {'\0'};
    if (!GetKeyboardState(abKbdState))
    {
        return 0;
    }

    //
    // Map virtual key to character code
    //
    WCHAR wch = '\0';
    if (ToUnicode(code, scanCode, abKbdState, &wch, 1, 0) == 1)
    {
        return wch;
    }

    return 0;
}

//+---------------------------------------------------------------------------
//
// _IsKeyboardDisabled
//
//----------------------------------------------------------------------------

BOOL CJyutping::_IsKeyboardDisabled()
{
    ITfDocumentMgr* pDocMgrFocus = nullptr;
    ITfContext* pContext = nullptr;
    BOOL isDisabled = FALSE;

    if ((_pThreadMgr->GetFocus(&pDocMgrFocus) != S_OK) ||
        (pDocMgrFocus == nullptr))
    {
        // if there is no focus document manager object, the keyboard
        // is disabled.
        isDisabled = TRUE;
    }
    else if ((pDocMgrFocus->GetTop(&pContext) != S_OK) ||
        (pContext == nullptr))
    {
        // if there is no context object, the keyboard is disabled.
        isDisabled = TRUE;
    }
    else
    {
        CCompartment CompartmentKeyboardDisabled(_pThreadMgr, _tfClientId, GUID_COMPARTMENT_KEYBOARD_DISABLED);
        CompartmentKeyboardDisabled._GetCompartmentBOOL(isDisabled);

        CCompartment CompartmentEmptyContext(_pThreadMgr, _tfClientId, GUID_COMPARTMENT_EMPTYCONTEXT);
        CompartmentEmptyContext._GetCompartmentBOOL(isDisabled);
    }

    if (pContext)
    {
        pContext->Release();
    }

    if (pDocMgrFocus)
    {
        pDocMgrFocus->Release();
    }

    return isDisabled;
}

//+---------------------------------------------------------------------------
//
// ITfKeyEventSink::OnSetFocus
//
// Called by the system whenever this service gets the keystroke device focus.
//----------------------------------------------------------------------------

STDAPI CJyutping::OnSetFocus(BOOL fForeground)
{
	fForeground;

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfKeyEventSink::OnTestKeyDown
//
// Called by the system to query this service wants a potential keystroke.
//----------------------------------------------------------------------------

STDAPI CJyutping::OnTestKeyDown(ITfContext *pContext, WPARAM wParam, LPARAM lParam, BOOL *pIsEaten)
{
    Global::UpdateModifiers(wParam, lParam);

    _KEYSTROKE_STATE KeystrokeState;
    WCHAR wch = '\0';
    UINT code = 0;
    *pIsEaten = _IsKeyEaten(pContext, (UINT)wParam, &code, &wch, &KeystrokeState);

    if (KeystrokeState.Category == CATEGORY_INVOKE_COMPOSITION_EDIT_SESSION)
    {
        //
        // Invoke key handler edit session
        //
        KeystrokeState.Category = CATEGORY_COMPOSING;

        _InvokeKeyHandler(pContext, code, wch, (DWORD)lParam, KeystrokeState);
    }

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfKeyEventSink::OnKeyDown
//
// Called by the system to offer this service a keystroke.  If *pIsEaten == TRUE
// on exit, the application will not handle the keystroke.
//----------------------------------------------------------------------------

STDAPI CJyutping::OnKeyDown(ITfContext *pContext, WPARAM wParam, LPARAM lParam, BOOL *pIsEaten)
{
    Global::UpdateModifiers(wParam, lParam);

    _KEYSTROKE_STATE KeystrokeState;
    WCHAR wch = '\0';
    UINT code = 0;

    *pIsEaten = _IsKeyEaten(pContext, (UINT)wParam, &code, &wch, &KeystrokeState);

    if (*pIsEaten)
    {
        bool needInvokeKeyHandler = true;
        //
        // Invoke key handler edit session
        //
        if (code == VK_ESCAPE)
        {
            KeystrokeState.Category = CATEGORY_COMPOSING;
        }

        // Always eat THIRDPARTY_NEXTPAGE and THIRDPARTY_PREVPAGE keys, but don't always process them.
        if ((wch == THIRDPARTY_NEXTPAGE) || (wch == THIRDPARTY_PREVPAGE))
        {
            needInvokeKeyHandler = !((KeystrokeState.Category == CATEGORY_NONE) && (KeystrokeState.Function == FUNCTION_NONE));
        }

        if (needInvokeKeyHandler)
        {
            _InvokeKeyHandler(pContext, code, wch, (DWORD)lParam, KeystrokeState);
        }
    }
    else if (KeystrokeState.Category == CATEGORY_INVOKE_COMPOSITION_EDIT_SESSION)
    {
        // Invoke key handler edit session
        KeystrokeState.Category = CATEGORY_COMPOSING;
        _InvokeKeyHandler(pContext, code, wch, (DWORD)lParam, KeystrokeState);
    }

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfKeyEventSink::OnTestKeyUp
//
// Called by the system to query this service wants a potential keystroke.
//----------------------------------------------------------------------------

STDAPI CJyutping::OnTestKeyUp(ITfContext *pContext, WPARAM wParam, LPARAM lParam, BOOL *pIsEaten)
{
    if (pIsEaten == nullptr)
    {
        return E_INVALIDARG;
    }

    Global::UpdateModifiers(wParam, lParam);

    WCHAR wch = '\0';
    UINT code = 0;

    *pIsEaten = _IsKeyEaten(pContext, (UINT)wParam, &code, &wch, NULL);

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfKeyEventSink::OnKeyUp
//
// Called by the system to offer this service a keystroke.  If *pIsEaten == TRUE
// on exit, the application will not handle the keystroke.
//----------------------------------------------------------------------------

STDAPI CJyutping::OnKeyUp(ITfContext *pContext, WPARAM wParam, LPARAM lParam, BOOL *pIsEaten)
{
    Global::UpdateModifiers(wParam, lParam);

    WCHAR wch = '\0';
    UINT code = 0;

    *pIsEaten = _IsKeyEaten(pContext, (UINT)wParam, &code, &wch, NULL);

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfKeyEventSink::OnPreservedKey
//
// Called when a hotkey (registered by us, or by the system) is typed.
//----------------------------------------------------------------------------

STDAPI CJyutping::OnPreservedKey(ITfContext *pContext, REFGUID rguid, BOOL *pIsEaten)
{
    if (pIsEaten == nullptr)
    {
        return E_INVALIDARG;
    }

    CCompositionProcessorEngine *pCompositionProcessorEngine;
    pCompositionProcessorEngine = _pCompositionProcessorEngine;

    if (pContext != nullptr && _IsComposing() &&
        pCompositionProcessorEngine->ShouldHandleInputMethodModePreservedKey(rguid))
    {
        CInputMethodModeEditSession *pEditSession = new (std::nothrow) CInputMethodModeEditSession(this, pContext);
        if (pEditSession == nullptr)
        {
            *pIsEaten = FALSE;
            return S_OK;
        }

        HRESULT editSessionResult = E_FAIL;
        HRESULT requestResult = pContext->RequestEditSession(
            _tfClientId,
            pEditSession,
            TF_ES_ASYNCDONTCARE | TF_ES_READWRITE,
            &editSessionResult);
        pEditSession->Release();

        *pIsEaten = SUCCEEDED(requestResult) &&
            (editSessionResult == TF_S_ASYNC || SUCCEEDED(editSessionResult));
        if (!*pIsEaten)
        {
            Global::Log(
                L"InputMethodMode edit session failed: requestHr=0x%08X editSessionHr=0x%08X",
                static_cast<unsigned int>(requestResult),
                static_cast<unsigned int>(editSessionResult));
        }
        return S_OK;
    }

    BOOL isCharacterVariantPreservedKey = pCompositionProcessorEngine->IsCharacterVariantPreservedKey(rguid);
    pCompositionProcessorEngine->OnPreservedKey(rguid, pIsEaten, _GetThreadMgr(), _GetClientId());

    if (*pIsEaten && isCharacterVariantPreservedKey && _pCandidateListUIPresenter)
    {
        RefreshCandidateListAfterCharacterVariantChange();
    }

    return S_OK;
}

void CJyutping::RefreshCandidateListAfterCharacterVariantChange()
{
    if (!_pCandidateListUIPresenter || !_pCompositionProcessorEngine ||
        _candidateMode == CANDIDATE_PUNCTUATION)
    {
        return;
    }

    UINT selectedCandidateIndex = 0;
    _pCandidateListUIPresenter->GetSelection(&selectedCandidateIndex);

    CJyutpingArray<CCandidateListItem> candidateList;
    _pCompositionProcessorEngine->GetCandidateList(&candidateList);
    if (candidateList.Count() > 0)
    {
        _pCandidateListUIPresenter->_ClearList();
        _pCandidateListUIPresenter->_SetText(&candidateList);
        _pCandidateListUIPresenter->_SetSelection(static_cast<int>(selectedCandidateIndex));
    }
}

void CJyutping::RefreshCandidateWindowFontSizes()
{
    if (_pCandidateListUIPresenter)
    {
        _pCandidateListUIPresenter->_UpdateFontSizes();
    }
}

//+---------------------------------------------------------------------------
//
// _InitKeyEventSink
//
// Advise a keystroke sink.
//----------------------------------------------------------------------------

BOOL CJyutping::_InitKeyEventSink()
{
    ITfKeystrokeMgr* pKeystrokeMgr = nullptr;
    HRESULT hr = S_OK;

    if (FAILED(_pThreadMgr->QueryInterface(IID_ITfKeystrokeMgr, (void **)&pKeystrokeMgr)))
    {
        return FALSE;
    }

    hr = pKeystrokeMgr->AdviseKeyEventSink(_tfClientId, (ITfKeyEventSink *)this, TRUE);

    pKeystrokeMgr->Release();

    return (hr == S_OK);
}

//+---------------------------------------------------------------------------
//
// _UninitKeyEventSink
//
// Unadvise a keystroke sink.  Assumes we have advised one already.
//----------------------------------------------------------------------------

void CJyutping::_UninitKeyEventSink()
{
    ITfKeystrokeMgr* pKeystrokeMgr = nullptr;

    if (FAILED(_pThreadMgr->QueryInterface(IID_ITfKeystrokeMgr, (void **)&pKeystrokeMgr)))
    {
        return;
    }

    pKeystrokeMgr->UnadviseKeyEventSink(_tfClientId);

    pKeystrokeMgr->Release();
}
