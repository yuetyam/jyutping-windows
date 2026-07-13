#pragma once

#include "EditSession.h"
#include "Globals.h"
#include "VirtualInputKey.h"

class CKeyHandlerEditSession : public CEditSessionBase
{
public:
    CKeyHandlerEditSession(CJyutping *pTextService, ITfContext *pContext, UINT uCode, WCHAR wch, _KEYSTROKE_STATE keyState) : CEditSessionBase(pTextService, pContext)
    {
        _uCode = uCode;
        _wch = wch;
        VirtualInputKey::MatchInputKeyForCharacter(wch, &_inputKey);
        _KeyState = keyState;
    }

    // ITfEditSession
    STDMETHODIMP DoEditSession(TfEditCookie ec);

private:
    UINT _uCode;    // virtual key code
    WCHAR _wch;      // character code
    VirtualInputKey _inputKey;
    _KEYSTROKE_STATE _KeyState;     // key function regarding virtual key
};
