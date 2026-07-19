#include "Private.h"
#include "Globals.h"
#include "EditSession.h"
#include "Jyutping.h"
#include "CandidateListUIPresenter.h"
#include "Compartment.h"
#include "CompositionProcessorEngine.h"
#include "Logger.h"
#include "PunctuationKey.h"

namespace {

std::wstring PunctuationComment(const PunctuationSymbol& symbol)
{
    if (symbol.comment == nullptr)
    {
        return std::wstring();
    }

    std::wstring comment(symbol.comment);
    if (symbol.secondaryComment != nullptr)
    {
        comment.append(L" ");
        comment.append(symbol.secondaryComment);
    }
    return comment;
}

} // namespace

//+---------------------------------------------------------------------------
//
// _IsRangeCovered
//
// Returns TRUE if pRangeTest is entirely contained within pRangeCover.
//
//----------------------------------------------------------------------------

BOOL CJyutping::_IsRangeCovered(TfEditCookie ec, _In_ ITfRange *pRangeTest, _In_ ITfRange *pRangeCover)
{
    LONG lResult = 0;;

    if (FAILED(pRangeCover->CompareStart(ec, pRangeTest, TF_ANCHOR_START, &lResult))
        || (lResult > 0))
    {
        return FALSE;
    }

    if (FAILED(pRangeCover->CompareEnd(ec, pRangeTest, TF_ANCHOR_END, &lResult))
        || (lResult < 0))
    {
        return FALSE;
    }

    return TRUE;
}

//+---------------------------------------------------------------------------
//
// _DeleteCandidateList
//
//----------------------------------------------------------------------------

VOID CJyutping::_DeleteCandidateList(BOOL isForce, _In_opt_ ITfContext *pContext)
{
    isForce;pContext;

    CCompositionProcessorEngine* pCompositionProcessorEngine = nullptr;
    pCompositionProcessorEngine = _pCompositionProcessorEngine;
    pCompositionProcessorEngine->ClearInputKeys();

    if (_pCandidateListUIPresenter)
    {
        _pCandidateListUIPresenter->_EndCandidateList();
        delete _pCandidateListUIPresenter;
        _pCandidateListUIPresenter = nullptr;

        _candidateMode = CANDIDATE_NONE;
    }
}

//+---------------------------------------------------------------------------
//
// _HandleComplete
//
//----------------------------------------------------------------------------

HRESULT CJyutping::_HandleComplete(TfEditCookie ec, _In_ ITfContext *pContext)
{
    _DeleteCandidateList(FALSE, pContext);

    // just terminate the composition
    _TerminateComposition(ec, pContext);

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _HandleCancel
//
//----------------------------------------------------------------------------

HRESULT CJyutping::_HandleCancel(TfEditCookie ec, _In_ ITfContext *pContext)
{
    _RemoveDummyCompositionForComposing(ec, _pComposition);

    _DeleteCandidateList(FALSE, pContext);

    _TerminateComposition(ec, pContext);

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _HandleCompositionInput
//
// If the keystroke happens within a composition, eat the key and return S_OK.
//
//----------------------------------------------------------------------------

HRESULT CJyutping::_HandleCompositionInput(TfEditCookie ec, _In_ ITfContext *pContext, const VirtualInputKey& inputKey)
{
    HRESULT hr = S_OK;
    ITfRange* pRangeComposition = nullptr;
    TF_SELECTION tfSelection;
    ULONG fetched = 0;
    BOOL isCovered = TRUE;
    BOOL startedComposition = FALSE;

    CCompositionProcessorEngine* pCompositionProcessorEngine = nullptr;
    pCompositionProcessorEngine = _pCompositionProcessorEngine;

    if ((_pCandidateListUIPresenter != nullptr) && (_candidateMode != CANDIDATE_INCREMENTAL))
    {
        hr = _HandleCompositionFinalize(ec, pContext, FALSE);
        if (FAILED(hr))
        {
            return hr;
        }
    }

    // Start the composition in the write edit session that is handling this key.
    if (!_IsComposing())
    {
        hr = _StartComposition(ec, pContext);
        if (FAILED(hr))
        {
            pCompositionProcessorEngine->ClearInputKeys();
            return hr;
        }
        startedComposition = TRUE;
    }

    if (_pComposition == nullptr)
    {
        pCompositionProcessorEngine->ClearInputKeys();
        return E_UNEXPECTED;
    }

    // first, test where a keystroke would go in the document if we did an insert
    hr = pContext->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &tfSelection, &fetched);
    if (hr != S_OK || fetched != 1)
    {
        Global::Log(L"HandleCompositionInput failed: GetSelection hr=0x%08X fetched=%lu", static_cast<unsigned int>(hr), fetched);
        if (startedComposition)
        {
            pCompositionProcessorEngine->ClearInputKeys();
            _TerminateComposition(ec, pContext);
        }
        return FAILED(hr) ? hr : E_FAIL;
    }

    // is the insertion point covered by a composition?
    hr = _pComposition->GetRange(&pRangeComposition);
    if (FAILED(hr) || pRangeComposition == nullptr)
    {
        Global::Log(L"HandleCompositionInput failed: GetRange hr=0x%08X", static_cast<unsigned int>(hr));
        tfSelection.range->Release();
        if (startedComposition)
        {
            pCompositionProcessorEngine->ClearInputKeys();
            _TerminateComposition(ec, pContext);
        }
        return FAILED(hr) ? hr : E_UNEXPECTED;
    }

    isCovered = _IsRangeCovered(ec, tfSelection.range, pRangeComposition);
    pRangeComposition->Release();

    if (!isCovered)
    {
        tfSelection.range->Release();
        if (startedComposition)
        {
            pCompositionProcessorEngine->ClearInputKeys();
            _TerminateComposition(ec, pContext);
        }
        return S_OK;
    }

    // Add input key to composition processor engine
    pCompositionProcessorEngine->AddInputKey(inputKey);

    hr = _HandleCompositionInputWorker(pCompositionProcessorEngine, ec, pContext);

    tfSelection.range->Release();
    return hr;
}

//+---------------------------------------------------------------------------
//
// _HandleCompositionInputWorker
//
// If the keystroke happens within a composition, eat the key and return S_OK.
//
//----------------------------------------------------------------------------

HRESULT CJyutping::_HandleCompositionInputWorker(_In_ CCompositionProcessorEngine *pCompositionProcessorEngine, TfEditCookie ec, _In_ ITfContext *pContext)
{
    HRESULT hr = S_OK;
    CJyutpingArray<CStringRange> readingStrings;

    //
    // Get reading string from composition processor engine
    //
    pCompositionProcessorEngine->GetReadingStrings(&readingStrings);

    for (UINT index = 0; index < readingStrings.Count(); index++)
    {
        hr = _AddComposingAndChar(ec, pContext, readingStrings.GetAt(index));
        if (FAILED(hr))
        {
            return hr;
        }
    }

    //
    // Get candidate string from composition processor engine
    //
    CJyutpingArray<CCandidateListItem> candidateList;

    pCompositionProcessorEngine->GetCandidateList(&candidateList);

    if ((candidateList.Count()))
    {
        hr = _CreateAndStartCandidate(pCompositionProcessorEngine, ec, pContext);
        if (SUCCEEDED(hr))
        {
            _pCandidateListUIPresenter->_ClearList();
            _pCandidateListUIPresenter->_SetText(&candidateList);
        }
    }
    else if (_pCandidateListUIPresenter)
    {
        _pCandidateListUIPresenter->_EndCandidateList();
        delete _pCandidateListUIPresenter;
        _pCandidateListUIPresenter = nullptr;
        _candidateMode = CANDIDATE_NONE;
    }
    return hr;
}
//+---------------------------------------------------------------------------
//
// _CreateAndStartCandidate
//
//----------------------------------------------------------------------------

HRESULT CJyutping::_CreateAndStartCandidate(_In_ CCompositionProcessorEngine *pCompositionProcessorEngine, TfEditCookie ec, _In_ ITfContext *pContext)
{
    HRESULT hr = S_OK;
    BOOL createdPresenter = FALSE;

    if (((_candidateMode == CANDIDATE_PHRASE) && (_pCandidateListUIPresenter))
        || ((_candidateMode == CANDIDATE_NONE) && (_pCandidateListUIPresenter)))
    {
        // Recreate candidate list
        _pCandidateListUIPresenter->_EndCandidateList();
        delete _pCandidateListUIPresenter;
        _pCandidateListUIPresenter = nullptr;

        _candidateMode = CANDIDATE_NONE;
    }

    if (_pCandidateListUIPresenter == nullptr)
    {
        _pCandidateListUIPresenter = new (std::nothrow) CCandidateListUIPresenter(this, Global::AtomCandidateWindow,
            CATEGORY_CANDIDATE,
            pCompositionProcessorEngine->GetCandidateListIndexRange(),
            FALSE);
        if (!_pCandidateListUIPresenter)
        {
            return E_OUTOFMEMORY;
        }

        _candidateMode = CANDIDATE_INCREMENTAL;
        createdPresenter = TRUE;

        // we don't cache the document manager object. So get it from pContext.
        ITfDocumentMgr* pDocumentMgr = nullptr;
        hr = pContext->GetDocumentMgr(&pDocumentMgr);
        if (SUCCEEDED(hr) && pDocumentMgr != nullptr)
        {
            // get the composition range.
            ITfRange* pRange = nullptr;
            if (_pComposition == nullptr)
            {
                hr = E_UNEXPECTED;
            }
            else
            {
                hr = _pComposition->GetRange(&pRange);
            }
            if (SUCCEEDED(hr) && pRange != nullptr)
            {
                hr = _pCandidateListUIPresenter->_StartCandidateList(_tfClientId, pDocumentMgr, pContext, ec, pRange);
                pRange->Release();
            }
            else if (SUCCEEDED(hr))
            {
                hr = E_UNEXPECTED;
            }
            pDocumentMgr->Release();
        }
        else if (SUCCEEDED(hr))
        {
            hr = E_UNEXPECTED;
        }
    }

    if (FAILED(hr) && createdPresenter)
    {
        Global::Log(L"CreateAndStartCandidate failed: hr=0x%08X", static_cast<unsigned int>(hr));
        _pCandidateListUIPresenter->_EndCandidateList();
        delete _pCandidateListUIPresenter;
        _pCandidateListUIPresenter = nullptr;
        _candidateMode = CANDIDATE_NONE;
    }

    return hr;
}

//+---------------------------------------------------------------------------
//
// _HandleCompositionFinalize
//
//----------------------------------------------------------------------------

HRESULT CJyutping::_HandleCompositionFinalize(TfEditCookie ec, _In_ ITfContext *pContext, BOOL isCandidateList)
{
    HRESULT hr = S_OK;

    if (isCandidateList && _pCandidateListUIPresenter)
    {
        // Finalize selected candidate string from CCandidateListUIPresenter
        DWORD_PTR candidateLen = 0;
        const WCHAR *pCandidateString = nullptr;
        UINT candidateIndex = 0;
        BOOL hasCandidateIndex = _pCandidateListUIPresenter->_GetSelectedCandidateIndex(&candidateIndex);

        candidateLen = _pCandidateListUIPresenter->_GetSelectedCandidateString(&pCandidateString);

        CStringRange candidateString;
        candidateString.Set(pCandidateString, candidateLen);

        if (candidateLen)
        {
            // Finalize character
            hr = _AddCharAndFinalize(ec, pContext, &candidateString);
            if (FAILED(hr))
            {
                return hr;
            }

            if (_candidateMode != CANDIDATE_PUNCTUATION && hasCandidateIndex &&
                _pCompositionProcessorEngine != nullptr)
            {
                _pCompositionProcessorEngine->CommitSelectedCandidateForMemory(candidateIndex);
            }
        }
    }
    else
    {
        // Finalize current text store strings
        if (_IsComposing())
        {
            ULONG fetched = 0;
            TF_SELECTION tfSelection;

            if (FAILED(pContext->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &tfSelection, &fetched)) || fetched != 1)
            {
                return S_FALSE;
            }

            ITfRange* pRangeComposition = nullptr;
            if (SUCCEEDED(_pComposition->GetRange(&pRangeComposition)))
            {
                if (_IsRangeCovered(ec, tfSelection.range, pRangeComposition))
                {
                    _EndComposition(pContext);
                }

                pRangeComposition->Release();
            }

            tfSelection.range->Release();
        }
    }

    _HandleCancel(ec, pContext);

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _HandleCompositionFinalizeRaw
//
//----------------------------------------------------------------------------

HRESULT CJyutping::_HandleCompositionFinalizeRaw(TfEditCookie ec, _In_ ITfContext *pContext)
{
    if (_pCompositionProcessorEngine == nullptr)
    {
        return S_FALSE;
    }

    std::wstring rawInput = _pCompositionProcessorEngine->GetRawInputText();
    if (rawInput.empty())
    {
        return _HandleCancel(ec, pContext);
    }

    CStringRange rawInputString;
    rawInputString.Set(rawInput.c_str(), rawInput.length());

    HRESULT hr = S_OK;
    if (_IsComposing())
    {
        hr = _AddComposingAndChar(ec, pContext, &rawInputString);
    }
    else
    {
        hr = _AddCharAndFinalize(ec, pContext, &rawInputString);
    }
    if (FAILED(hr))
    {
        return hr;
    }

    return _HandleComplete(ec, pContext);
}

//+---------------------------------------------------------------------------
//
// _HandleCompositionConvert
//
//----------------------------------------------------------------------------

HRESULT CJyutping::_HandleCompositionConvert(TfEditCookie ec, _In_ ITfContext *pContext)
{
    HRESULT hr = S_OK;

    CJyutpingArray<CCandidateListItem> candidateList;

    //
    // Get candidate string from composition processor engine
    //
    CCompositionProcessorEngine* pCompositionProcessorEngine = nullptr;
    pCompositionProcessorEngine = _pCompositionProcessorEngine;
    pCompositionProcessorEngine->GetCandidateList(&candidateList);

    // If there is no candlidate listin the current reading string, we don't do anything. Just wait for
    // next char to be ready for the conversion with it.
    int nCount = candidateList.Count();
    if (nCount)
    {
        if (_pCandidateListUIPresenter)
        {
            _pCandidateListUIPresenter->_EndCandidateList();
            delete _pCandidateListUIPresenter;
            _pCandidateListUIPresenter = nullptr;

            _candidateMode = CANDIDATE_NONE;
        }

        //
        // create an instance of the candidate list class.
        //
        if (_pCandidateListUIPresenter == nullptr)
        {
            _pCandidateListUIPresenter = new (std::nothrow) CCandidateListUIPresenter(this, Global::AtomCandidateWindow,
                CATEGORY_CANDIDATE,
                pCompositionProcessorEngine->GetCandidateListIndexRange(),
                FALSE);
            if (!_pCandidateListUIPresenter)
            {
                return E_OUTOFMEMORY;
            }

            _candidateMode = CANDIDATE_ORIGINAL;
        }

        // we don't cache the document manager object. So get it from pContext.
        ITfDocumentMgr* pDocumentMgr = nullptr;
        if (SUCCEEDED(pContext->GetDocumentMgr(&pDocumentMgr)))
        {
            // get the composition range.
            ITfRange* pRange = nullptr;
            if (SUCCEEDED(_pComposition->GetRange(&pRange)))
            {
                hr = _pCandidateListUIPresenter->_StartCandidateList(_tfClientId, pDocumentMgr, pContext, ec, pRange);
                pRange->Release();
            }
            pDocumentMgr->Release();
        }
        if (SUCCEEDED(hr))
        {
            _pCandidateListUIPresenter->_SetText(&candidateList);
        }
    }

    return hr;
}

//+---------------------------------------------------------------------------
//
// _HandleCompositionBackspace
//
//----------------------------------------------------------------------------

HRESULT CJyutping::_HandleCompositionBackspace(TfEditCookie ec, _In_ ITfContext *pContext)
{
    ITfRange* pRangeComposition = nullptr;
    TF_SELECTION tfSelection;
    ULONG fetched = 0;
    BOOL isCovered = TRUE;

    // Start the new (std::nothrow) compositon if there is no composition.
    if (!_IsComposing())
    {
        return S_OK;
    }

    // first, test where a keystroke would go in the document if we did an insert
    if (FAILED(pContext->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &tfSelection, &fetched)) || fetched != 1)
    {
        return S_FALSE;
    }

    // is the insertion point covered by a composition?
    if (SUCCEEDED(_pComposition->GetRange(&pRangeComposition)))
    {
        isCovered = _IsRangeCovered(ec, tfSelection.range, pRangeComposition);

        pRangeComposition->Release();

        if (!isCovered)
        {
            tfSelection.range->Release();
            return S_OK;
        }
    }

    //
    // Add virtual key to composition processor engine
    //
    CCompositionProcessorEngine* pCompositionProcessorEngine = nullptr;
    pCompositionProcessorEngine = _pCompositionProcessorEngine;

    size_t inputKeyCount = pCompositionProcessorEngine->GetInputKeyCount();

    if (inputKeyCount)
    {
        pCompositionProcessorEngine->RemoveInputKey(inputKeyCount - 1);

        if (pCompositionProcessorEngine->GetInputKeyCount())
        {
            _HandleCompositionInputWorker(pCompositionProcessorEngine, ec, pContext);
        }
        else
        {
            _HandleCancel(ec, pContext);
        }
    }

    tfSelection.range->Release();
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _HandleCompositionArrowKey
//
// Update the selection within a composition.
//
//----------------------------------------------------------------------------

HRESULT CJyutping::_HandleCompositionArrowKey(TfEditCookie ec, _In_ ITfContext *pContext, KEYSTROKE_FUNCTION keyFunction)
{
    ITfRange* pRangeComposition = nullptr;
    TF_SELECTION tfSelection;
    ULONG fetched = 0;

    // get the selection
    if (FAILED(pContext->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &tfSelection, &fetched))
        || fetched != 1)
    {
        // no selection, eat the keystroke
        return S_OK;
    }

    // get the composition range
    if (FAILED(_pComposition->GetRange(&pRangeComposition)))
    {
        tfSelection.range->Release();
        return S_OK;
    }

    // For incremental candidate list
    if (_pCandidateListUIPresenter)
    {
        _pCandidateListUIPresenter->AdviseUIChangedByArrowKey(keyFunction);
    }

    pContext->SetSelection(ec, 1, &tfSelection);

    pRangeComposition->Release();
    tfSelection.range->Release();
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _HandlePunctuationKey
//
//----------------------------------------------------------------------------

HRESULT CJyutping::_HandlePunctuationKey(TfEditCookie ec, _In_ ITfContext *pContext, UINT keyCode, BOOL isShifting)
{
    const PunctuationKey* punctuationKey = PunctuationKey::ForVirtualKey(keyCode);
    if (punctuationKey == nullptr || !punctuationKey->ShouldHandle(isShifting))
    {
        return E_INVALIDARG;
    }

    HRESULT hr = _FinalizeBeforePunctuation(ec, pContext);
    if (FAILED(hr))
    {
        return hr;
    }

    BOOL isCantonesePunctuation = TRUE;
    CCompartment punctuationFormCompartment(
        _pThreadMgr,
        _tfClientId,
        Global::JyutpingGuidCompartmentPunctuationForm);
    punctuationFormCompartment._GetCompartmentBOOL(isCantonesePunctuation);

    LPCWSTR output = isCantonesePunctuation ?
        punctuationKey->InstantSymbol(isShifting) : punctuationKey->Text(isShifting);
    if (output != nullptr)
    {
        CStringRange outputString;
        outputString.Set(output, wcslen(output));
        return _AddCharAndFinalize(ec, pContext, &outputString);
    }

    return _StartPunctuationCandidateList(ec, pContext, *punctuationKey, isShifting);
}

HRESULT CJyutping::_FinalizeBeforePunctuation(TfEditCookie ec, _In_ ITfContext *pContext)
{
    if (_pCandidateListUIPresenter != nullptr && _candidateMode != CANDIDATE_NONE)
    {
        const WCHAR* candidateText = nullptr;
        DWORD_PTR candidateLength = _pCandidateListUIPresenter->_GetSelectedCandidateString(&candidateText);
        UINT candidateIndex = 0;
        BOOL hasCandidateIndex = _pCandidateListUIPresenter->_GetSelectedCandidateIndex(&candidateIndex);

        if (candidateLength > 0)
        {
            CStringRange candidateString;
            candidateString.Set(candidateText, candidateLength);

            HRESULT hr = _AddComposingAndChar(ec, pContext, &candidateString);
            if (FAILED(hr))
            {
                return hr;
            }

            if (_candidateMode != CANDIDATE_PUNCTUATION && hasCandidateIndex &&
                _pCompositionProcessorEngine != nullptr)
            {
                _pCompositionProcessorEngine->CommitSelectedCandidateForMemory(candidateIndex);
            }
        }

        return _HandleComplete(ec, pContext);
    }

    if (_IsComposing())
    {
        return _HandleCompositionFinalizeRaw(ec, pContext);
    }
    return S_OK;
}

HRESULT CJyutping::_StartPunctuationCandidateList(
    TfEditCookie ec,
    _In_ ITfContext *pContext,
    const PunctuationKey& punctuationKey,
    BOOL isShifting)
{
    PunctuationSymbolList symbols = punctuationKey.Symbols(isShifting);
    if (symbols.symbols == nullptr || symbols.count == 0)
    {
        return S_FALSE;
    }

    HRESULT hr = _StartComposition(ec, pContext);
    if (FAILED(hr))
    {
        return hr;
    }

    LPCWSTR placeholder = punctuationKey.Text(isShifting);
    CStringRange placeholderString;
    placeholderString.Set(placeholder, wcslen(placeholder));
    hr = _AddComposingAndChar(ec, pContext, &placeholderString);
    if (FAILED(hr))
    {
        _HandleCancel(ec, pContext);
        return hr;
    }

    _pCandidateListUIPresenter = new (std::nothrow) CCandidateListUIPresenter(
        this,
        Global::AtomCandidateWindow,
        CATEGORY_CANDIDATE,
        _pCompositionProcessorEngine->GetCandidateListIndexRange(),
        FALSE);
    if (_pCandidateListUIPresenter == nullptr)
    {
        _HandleCancel(ec, pContext);
        return E_OUTOFMEMORY;
    }
    _candidateMode = CANDIDATE_PUNCTUATION;

    ITfDocumentMgr* pDocumentManager = nullptr;
    hr = pContext->GetDocumentMgr(&pDocumentManager);
    if (SUCCEEDED(hr) && pDocumentManager != nullptr)
    {
        ITfRange* pCompositionRange = nullptr;
        hr = _pComposition->GetRange(&pCompositionRange);
        if (SUCCEEDED(hr) && pCompositionRange != nullptr)
        {
            hr = _pCandidateListUIPresenter->_StartCandidateList(
                _tfClientId,
                pDocumentManager,
                pContext,
                ec,
                pCompositionRange);
            pCompositionRange->Release();
        }
        else if (SUCCEEDED(hr))
        {
            hr = E_UNEXPECTED;
        }
        pDocumentManager->Release();
    }
    else if (SUCCEEDED(hr))
    {
        hr = E_UNEXPECTED;
    }

    if (FAILED(hr))
    {
        _HandleCancel(ec, pContext);
        return hr;
    }

    std::vector<std::wstring> comments;
    comments.reserve(symbols.count);
    for (size_t index = 0; index < symbols.count; index++)
    {
        comments.push_back(PunctuationComment(symbols.symbols[index]));
    }

    CJyutpingArray<CCandidateListItem> candidateList;
    candidateList.reserve(symbols.count);
    for (size_t index = 0; index < symbols.count; index++)
    {
        const PunctuationSymbol& symbol = symbols.symbols[index];
        CCandidateListItem* candidate = candidateList.Append();
        if (candidate == nullptr)
        {
            _HandleCancel(ec, pContext);
            return E_OUTOFMEMORY;
        }

        candidate->_ItemString.Set(symbol.text, wcslen(symbol.text));
        candidate->_ItemComment.Set(comments[index].c_str(), comments[index].length());
    }

    _pCandidateListUIPresenter->_SetText(&candidateList);

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _HandleCompositionCharacterForm
//
//----------------------------------------------------------------------------

HRESULT CJyutping::_HandleCompositionCharacterForm(TfEditCookie ec, _In_ ITfContext *pContext, WCHAR wch)
{
    HRESULT hr = S_OK;

    WCHAR fullWidth = Global::FullWidthCharTable[wch - 0x20];

    CStringRange fullWidthString;
    fullWidthString.Set(&fullWidth, 1);

    // Finalize character
    hr = _AddCharAndFinalize(ec, pContext, &fullWidthString);
    if (FAILED(hr))
    {
        return hr;
    }

    _HandleCancel(ec, pContext);

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _InvokeKeyHandler
//
// This text service is interested in handling keystrokes to demonstrate the
// use the compositions. Some apps will cancel compositions if they receive
// keystrokes while a compositions is ongoing.
//
// param
//    [in] uCode - virtual key code of WM_KEYDOWN wParam
//    [in] dwFlags - WM_KEYDOWN lParam
//    [in] dwKeyFunction - Function regarding virtual key
//----------------------------------------------------------------------------

HRESULT CJyutping::_InvokeKeyHandler(_In_ ITfContext *pContext, UINT code, WCHAR wch, DWORD flags, _KEYSTROKE_STATE keyState)
{
    flags;

    CKeyHandlerEditSession* pEditSession = nullptr;

    // we'll insert a char ourselves in place of this keystroke
    pEditSession = new (std::nothrow) CKeyHandlerEditSession(this, pContext, code, wch, keyState);
    if (pEditSession == nullptr)
    {
        return E_FAIL;
    }

    //
    // Call CKeyHandlerEditSession::DoEditSession().
    //
    // Do not specify TF_ES_SYNC so edit session is not invoked on WinWord
    //
    HRESULT editSessionResult = E_FAIL;
    HRESULT requestResult = pContext->RequestEditSession(
        _tfClientId,
        pEditSession,
        TF_ES_ASYNCDONTCARE | TF_ES_READWRITE,
        &editSessionResult);

    pEditSession->Release();

    if (FAILED(requestResult) || FAILED(editSessionResult))
    {
        Global::Log(
            L"InvokeKeyHandler failed: category=%d function=%d requestHr=0x%08X editSessionHr=0x%08X",
            static_cast<int>(keyState.Category),
            static_cast<int>(keyState.Function),
            static_cast<unsigned int>(requestResult),
            static_cast<unsigned int>(editSessionResult));
    }

    if (FAILED(requestResult))
    {
        return requestResult;
    }

    return (editSessionResult == TF_S_ASYNC) ? requestResult : editSessionResult;
}
