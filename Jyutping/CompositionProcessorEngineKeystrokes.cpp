#include "Private.h"
#include "CompositionProcessorEngine.h"
#include "Globals.h"

namespace {

BOOL IsNoModifier()
{
    return Global::CheckModifiers(Global::ModifiersValue, 0);
}

BOOL IsShiftOnlyModifier()
{
    return Global::CheckModifiers(Global::ModifiersValue, TF_MOD_SHIFT);
}

BOOL IsControlShiftModifier()
{
    return Global::CheckModifiers(Global::ModifiersValue, TF_MOD_CONTROL | TF_MOD_SHIFT);
}

BOOL SetKeystrokeState(_Out_opt_ _KEYSTROKE_STATE *pKeyState, KEYSTROKE_CATEGORY category, KEYSTROKE_FUNCTION function)
{
    if (pKeyState)
    {
        pKeyState->Category = category;
        pKeyState->Function = function;
    }
    return TRUE;
}

BOOL TrySetCandidateForgetKey(UINT uCode, _Out_opt_ _KEYSTROKE_STATE *pKeyState)
{
    if ((uCode == VK_BACK || uCode == VK_DELETE) && IsControlShiftModifier())
    {
        return SetKeystrokeState(pKeyState, CATEGORY_CANDIDATE, FUNCTION_FORGET_CANDIDATE);
    }
    return FALSE;
}

BOOL TrySetCandidateNavigationKey(UINT uCode, KEYSTROKE_CATEGORY category, _Out_opt_ _KEYSTROKE_STATE *pKeyState)
{
    switch (uCode)
    {
    case VK_TAB:
        if (IsNoModifier())
        {
            return SetKeystrokeState(pKeyState, category, FUNCTION_MOVE_DOWN);
        }
        if (IsShiftOnlyModifier())
        {
            return SetKeystrokeState(pKeyState, category, FUNCTION_MOVE_UP);
        }
        return FALSE;

    case VK_UP:
        return SetKeystrokeState(pKeyState, category, FUNCTION_MOVE_UP);
    case VK_DOWN:
        return SetKeystrokeState(pKeyState, category, FUNCTION_MOVE_DOWN);
    case VK_LEFT:
        return SetKeystrokeState(pKeyState, category, FUNCTION_MOVE_PAGE_UP);
    case VK_RIGHT:
        return SetKeystrokeState(pKeyState, category, FUNCTION_MOVE_PAGE_DOWN);
    case VK_PRIOR:
        return SetKeystrokeState(pKeyState, category, FUNCTION_MOVE_PAGE_UP);
    case VK_NEXT:
        return SetKeystrokeState(pKeyState, category, FUNCTION_MOVE_PAGE_DOWN);
    case VK_HOME:
        return SetKeystrokeState(pKeyState, category, FUNCTION_MOVE_PAGE_TOP);
    case VK_END:
        return SetKeystrokeState(pKeyState, category, FUNCTION_MOVE_PAGE_BOTTOM);
    case VK_OEM_MINUS:
    case VK_OEM_4:
        if (IsNoModifier())
        {
            return SetKeystrokeState(pKeyState, category, FUNCTION_MOVE_PAGE_UP);
        }
        return FALSE;
    case VK_OEM_PLUS:
    case VK_OEM_6:
        if (IsNoModifier())
        {
            return SetKeystrokeState(pKeyState, category, FUNCTION_MOVE_PAGE_DOWN);
        }
        return FALSE;
    }

    return FALSE;
}

} // namespace


void CCompositionProcessorEngine::SetupKeystroke()
{
    SetKeystrokeTable(&_KeystrokeComposition);
    return;
}

void CCompositionProcessorEngine::SetKeystrokeTable(_Inout_ CJyutpingArray<_KEYSTROKE> *pKeystroke)
{
    for (size_t i = 0; i < VirtualInputKey::alphabetSetCount; i++)
    {
        _KEYSTROKE* pKS = nullptr;

        pKS = pKeystroke->Append();
        if (!pKS)
        {
            break;
        }
        *pKS = _keystrokeTable[i];
    }
}

void CCompositionProcessorEngine::InitKeyStrokeTable()
{
    const VirtualInputKey* alphabetSet = VirtualInputKey::AlphabetSet();

    for (size_t i = 0; i < VirtualInputKey::alphabetSetCount; i++)
    {
        _keystrokeTable[i].VirtualKey = alphabetSet[i].keyCode;
        _keystrokeTable[i].Modifiers = 0;
        _keystrokeTable[i].Function = FUNCTION_INPUT;
    }
}

void CCompositionProcessorEngine::SetCandidateListRange(DWORD pageSize)
{
    _candidateListIndexRange.Clear();

    for (DWORD i = 1; i <= pageSize; i++)
    {
        DWORD* pNewIndexRange = nullptr;

        pNewIndexRange = _candidateListIndexRange.Append();
        if (pNewIndexRange != nullptr)
        {
            if (i != 10)
            {
                *pNewIndexRange = i;
            }
            else
            {
                *pNewIndexRange = 0;
            }
        }
    }
}

//////////////////////////////////////////////////////////////////////
//
//    CCompositionProcessorEngine
//
//////////////////////////////////////////////////////////////////////

//+---------------------------------------------------------------------------
//
// CCompositionProcessorEngine::IsVirtualKeyNeed
//
// Test virtual key code need to the Composition Processor Engine.
// param
//     [in] uCode - Specify virtual key code.
//     [in/out] pwch       - char code
//     [in] fComposing     - Specified composing.
//     [in] fCandidateMode - Specified candidate mode.
//     [out] pKeyState     - Returns function regarding virtual key.
// returns
//     If engine need this virtual key code, returns true. Otherwise returns false.
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::IsVirtualKeyNeed(UINT uCode, _In_reads_(1) WCHAR *pwch, BOOL fComposing, CANDIDATE_MODE candidateMode, _Out_opt_ _KEYSTROKE_STATE *pKeyState)
{
    if (pKeyState)
    {
        pKeyState->Category = CATEGORY_NONE;
        pKeyState->Function = FUNCTION_NONE;
    }

    if (candidateMode == CANDIDATE_ORIGINAL || candidateMode == CANDIDATE_PHRASE || candidateMode == CANDIDATE_WITH_NEXT_COMPOSITION)
    {
        fComposing = FALSE;
    }

    if (fComposing || candidateMode == CANDIDATE_INCREMENTAL || candidateMode == CANDIDATE_NONE)
    {
        if ((fComposing || candidateMode == CANDIDATE_INCREMENTAL) && IsReverseLookupInputKey(uCode))
        {
            if (pKeyState)
            {
                pKeyState->Category = CATEGORY_COMPOSING;
                pKeyState->Function = FUNCTION_INPUT;
            }
            return TRUE;
        }
        else if (VirtualInputKey::IsMatchedLetter(uCode) &&
            (Global::ModifiersValue == 0 || Global::CheckModifiers(Global::ModifiersValue, TF_MOD_SHIFT)))
        {
            if (pKeyState)
            {
                pKeyState->Category = CATEGORY_COMPOSING;
                pKeyState->Function = FUNCTION_INPUT;
            }
            return TRUE;
        }
        else if (IsVirtualKeyKeystrokeComposition(uCode, pKeyState, FUNCTION_NONE))
        {
            return TRUE;
        }
    }

    if (candidateMode == CANDIDATE_ORIGINAL || candidateMode == CANDIDATE_PHRASE || candidateMode == CANDIDATE_WITH_NEXT_COMPOSITION)
    {
        KEYSTROKE_CATEGORY category = (candidateMode == CANDIDATE_PHRASE) ? CATEGORY_PHRASE : CATEGORY_CANDIDATE;
        if (candidateMode != CANDIDATE_PHRASE && TrySetCandidateForgetKey(uCode, pKeyState))
        {
            return TRUE;
        }

        if (TrySetCandidateNavigationKey(uCode, category, pKeyState))
        {
            return TRUE;
        }

        BOOL isRetCode = TRUE;
        if (IsVirtualKeyKeystrokeCandidate(uCode, pKeyState, candidateMode, &isRetCode, &_KeystrokeCandidate))
        {
            return isRetCode;
        }

        // Candidate list could not handle key. We can try to restart the composition.
        if (IsVirtualKeyKeystrokeComposition(uCode, pKeyState, FUNCTION_INPUT))
        {
            if (candidateMode != CANDIDATE_ORIGINAL)
            {
                return TRUE;
            }
            else
            {
                if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_FINALIZE_CANDIDATELIST_AND_INPUT; }
                return TRUE;
            }
        }
    }

    // CANDIDATE_INCREMENTAL should process Keystroke.Candidate virtual keys.
    else if (candidateMode == CANDIDATE_INCREMENTAL)
    {
        if (TrySetCandidateForgetKey(uCode, pKeyState))
        {
            return TRUE;
        }

        if (TrySetCandidateNavigationKey(uCode, CATEGORY_CANDIDATE, pKeyState))
        {
            return TRUE;
        }

        BOOL isRetCode = TRUE;
        if (IsVirtualKeyKeystrokeCandidate(uCode, pKeyState, candidateMode, &isRetCode, &_KeystrokeCandidate))
        {
            return isRetCode;
        }
    }

    if (!fComposing && candidateMode != CANDIDATE_ORIGINAL && candidateMode != CANDIDATE_PHRASE && candidateMode != CANDIDATE_WITH_NEXT_COMPOSITION)
    {
        if (IsVirtualKeyKeystrokeComposition(uCode, pKeyState, FUNCTION_INPUT))
        {
            return TRUE;
        }
    }

    // System pre-defined keystroke
    if (fComposing)
    {
        if ((candidateMode != CANDIDATE_INCREMENTAL))
        {
            switch (uCode)
            {
            case VK_LEFT:   if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_MOVE_LEFT; } return TRUE;
            case VK_RIGHT:  if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_MOVE_RIGHT; } return TRUE;
            case VK_RETURN: if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_FINALIZE_TEXTSTORE; } return TRUE;
            case VK_ESCAPE: if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_CANCEL; } return TRUE;
            case VK_BACK:   if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_BACKSPACE; } return TRUE;

            case VK_UP:     if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_MOVE_UP; } return TRUE;
            case VK_DOWN:   if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_MOVE_DOWN; } return TRUE;
            case VK_PRIOR:  if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_MOVE_PAGE_UP; } return TRUE;
            case VK_NEXT:   if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_MOVE_PAGE_DOWN; } return TRUE;

            case VK_HOME:   if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_MOVE_PAGE_TOP; } return TRUE;
            case VK_END:    if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_MOVE_PAGE_BOTTOM; } return TRUE;

            case VK_SPACE:  if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_FINALIZE_TEXTSTORE; } return TRUE;
            }
        }
        else if ((candidateMode == CANDIDATE_INCREMENTAL))
        {
            switch (uCode)
            {
            case VK_RETURN: if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_FINALIZE_TEXTSTORE; } return TRUE;
            case VK_ESCAPE: if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_CANCEL; } return TRUE;

                // VK_BACK - remove one char from reading string.
            case VK_BACK:   if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_BACKSPACE; } return TRUE;

            case VK_SPACE:
                {
                    if (candidateMode == CANDIDATE_INCREMENTAL)
                    {
                        if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_CONVERT; } return TRUE;
                    }
                    else
                    {
                        if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_CONVERT; } return TRUE;
                    }
                }
            }
        }
    }

    if ((candidateMode == CANDIDATE_ORIGINAL) || (candidateMode == CANDIDATE_WITH_NEXT_COMPOSITION))
    {
        switch (uCode)
        {
        case VK_RETURN: if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_FINALIZE_CANDIDATELIST; } return TRUE;
        case VK_SPACE:  if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_CONVERT; } return TRUE;
        case VK_BACK:   if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_CANCEL; } return TRUE;

        case VK_ESCAPE:
            {
                if (candidateMode == CANDIDATE_WITH_NEXT_COMPOSITION)
                {
                    if (pKeyState)
                    {
                        pKeyState->Category = CATEGORY_INVOKE_COMPOSITION_EDIT_SESSION;
                        pKeyState->Function = FUNCTION_FINALIZE_TEXTSTORE;
                    }
                    return TRUE;
                }
                else
                {
                    if (pKeyState)
                    {
                        pKeyState->Category = CATEGORY_CANDIDATE;
                        pKeyState->Function = FUNCTION_CANCEL;
                    }
                    return TRUE;
                }
            }
        }

        if (candidateMode == CANDIDATE_WITH_NEXT_COMPOSITION)
        {
            if (IsVirtualKeyKeystrokeComposition(uCode, NULL, FUNCTION_NONE))
            {
                if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_FINALIZE_TEXTSTORE_AND_INPUT; } return TRUE;
            }
        }
    }

    if (candidateMode == CANDIDATE_PHRASE)
    {
        switch (uCode)
        {
        case VK_RETURN: if (pKeyState) { pKeyState->Category = CATEGORY_PHRASE; pKeyState->Function = FUNCTION_FINALIZE_CANDIDATELIST; } return TRUE;
        case VK_SPACE:  if (pKeyState) { pKeyState->Category = CATEGORY_PHRASE; pKeyState->Function = FUNCTION_CONVERT; } return TRUE;
        case VK_ESCAPE: if (pKeyState) { pKeyState->Category = CATEGORY_PHRASE; pKeyState->Function = FUNCTION_CANCEL; } return TRUE;
        case VK_BACK:   if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_CANCEL; } return TRUE;
        }
    }

    if (IsKeystrokeRange(uCode, pKeyState, candidateMode))
    {
        return TRUE;
    }
    else if (pKeyState && pKeyState->Category != CATEGORY_NONE)
    {
        return FALSE;
    }

    if (*pwch && !IsVirtualKeyKeystrokeComposition(uCode, pKeyState, FUNCTION_NONE))
    {
        if (pKeyState)
        {
            pKeyState->Category = CATEGORY_INVOKE_COMPOSITION_EDIT_SESSION;
            pKeyState->Function = FUNCTION_FINALIZE_TEXTSTORE;
        }
        return FALSE;
    }

    return FALSE;
}

//+---------------------------------------------------------------------------
//
// CCompositionProcessorEngine::IsVirtualKeyKeystrokeComposition
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::IsVirtualKeyKeystrokeComposition(UINT uCode, _Out_opt_ _KEYSTROKE_STATE *pKeyState, KEYSTROKE_FUNCTION function)
{
    if (pKeyState == nullptr)
    {
        return FALSE;
    }

    pKeyState->Category = CATEGORY_NONE;
    pKeyState->Function = FUNCTION_NONE;

    for (UINT i = 0; i < _KeystrokeComposition.Count(); i++)
    {
        _KEYSTROKE *pKeystroke = nullptr;

        pKeystroke = _KeystrokeComposition.GetAt(i);

        if ((pKeystroke->VirtualKey == uCode) && Global::CheckModifiers(Global::ModifiersValue, pKeystroke->Modifiers))
        {
            if (function == FUNCTION_NONE)
            {
                pKeyState->Category = CATEGORY_COMPOSING;
                pKeyState->Function = pKeystroke->Function;
                return TRUE;
            }
            else if (function == pKeystroke->Function)
            {
                pKeyState->Category = CATEGORY_COMPOSING;
                pKeyState->Function = pKeystroke->Function;
                return TRUE;
            }
        }
    }

    return FALSE;
}

//+---------------------------------------------------------------------------
//
// CCompositionProcessorEngine::IsVirtualKeyKeystrokeCandidate
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::IsVirtualKeyKeystrokeCandidate(UINT uCode, _In_ _KEYSTROKE_STATE *pKeyState, CANDIDATE_MODE candidateMode, _Out_ BOOL *pfRetCode, _In_ CJyutpingArray<_KEYSTROKE> *pKeystrokeMetric)
{
    if (pfRetCode == nullptr)
    {
        return FALSE;
    }
    *pfRetCode = FALSE;

    for (UINT i = 0; i < pKeystrokeMetric->Count(); i++)
    {
        _KEYSTROKE *pKeystroke = nullptr;

        pKeystroke = pKeystrokeMetric->GetAt(i);

        if ((pKeystroke->VirtualKey == uCode) && Global::CheckModifiers(Global::ModifiersValue, pKeystroke->Modifiers))
        {
            *pfRetCode = TRUE;
            if (pKeyState)
            {
                pKeyState->Category = (candidateMode == CANDIDATE_ORIGINAL ? CATEGORY_CANDIDATE :
                    candidateMode == CANDIDATE_PHRASE ? CATEGORY_PHRASE : CATEGORY_CANDIDATE);

                pKeyState->Function = pKeystroke->Function;
            }
            return TRUE;
        }
    }

    return FALSE;
}

//+---------------------------------------------------------------------------
//
// CCompositionProcessorEngine::IsKeyKeystrokeRange
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::IsKeystrokeRange(UINT uCode, _Out_ _KEYSTROKE_STATE *pKeyState, CANDIDATE_MODE candidateMode)
{
    if (pKeyState == nullptr)
    {
        return FALSE;
    }

    pKeyState->Category = CATEGORY_NONE;
    pKeyState->Function = FUNCTION_NONE;

    if (_candidateListIndexRange.IsRange(uCode))
    {
        if (candidateMode == CANDIDATE_PHRASE)
        {
            // Candidate phrase could specify modifier
            if ((GetCandidateListPhraseModifier() == 0 && Global::ModifiersValue == 0) ||
                (GetCandidateListPhraseModifier() != 0 && Global::CheckModifiers(Global::ModifiersValue, GetCandidateListPhraseModifier())))
            {
                pKeyState->Category = CATEGORY_PHRASE; pKeyState->Function = FUNCTION_SELECT_BY_NUMBER;
                return TRUE;
            }
            else
            {
                pKeyState->Category = CATEGORY_INVOKE_COMPOSITION_EDIT_SESSION; pKeyState->Function = FUNCTION_FINALIZE_TEXTSTORE_AND_INPUT;
                return FALSE;
            }
        }
        else if (candidateMode == CANDIDATE_WITH_NEXT_COMPOSITION)
        {
            // Candidate phrase could specify modifier
            if ((GetCandidateListPhraseModifier() == 0 && Global::ModifiersValue == 0) ||
                (GetCandidateListPhraseModifier() != 0 && Global::CheckModifiers(Global::ModifiersValue, GetCandidateListPhraseModifier())))
            {
                pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_SELECT_BY_NUMBER;
                return TRUE;
            }
            // else next composition
        }
        else if (candidateMode != CANDIDATE_NONE)
        {
            pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_SELECT_BY_NUMBER;
            return TRUE;
        }
    }
    return FALSE;
}
