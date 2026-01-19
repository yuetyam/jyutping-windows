#pragma once

#include "EditSession.h"

class CJyutping;
class CTfTextLayoutSink;

//////////////////////////////////////////////////////////////////////
//
//    ITfEditSession
//        CEditSessionBase
// CGetTextExtentEditSession class
//
//////////////////////////////////////////////////////////////////////

//+---------------------------------------------------------------------------
//
// CGetTextExtentEditSession
//
//----------------------------------------------------------------------------

class CGetTextExtentEditSession : public CEditSessionBase
{
public:
    CGetTextExtentEditSession(_In_ CJyutping *pTextService, _In_ ITfContext *pContext, _In_ ITfContextView *pContextView, _In_ ITfRange *pRangeComposition, _In_ CTfTextLayoutSink *pTextLayoutSink);

    // ITfEditSession
    STDMETHODIMP DoEditSession(TfEditCookie ec);

private:
    ITfContextView* _pContextView;
    ITfRange* _pRangeComposition;
    CTfTextLayoutSink* _pTfTextLayoutSink;
};
