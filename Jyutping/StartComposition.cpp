#include "Private.h"
#include "Globals.h"
#include "EditSession.h"
#include "Jyutping.h"

//+---------------------------------------------------------------------------
//
// CStartCompositinoEditSession
//
//----------------------------------------------------------------------------

class CStartCompositionEditSession : public CEditSessionBase
{
public:
    CStartCompositionEditSession(_In_ CJyutping *pTextService, _In_ ITfContext *pContext) : CEditSessionBase(pTextService, pContext)
    {
    }

    // ITfEditSession
    STDMETHODIMP DoEditSession(TfEditCookie ec);
};

//+---------------------------------------------------------------------------
//
// ITfEditSession::DoEditSession
//
//----------------------------------------------------------------------------

STDAPI CStartCompositionEditSession::DoEditSession(TfEditCookie ec)
{
    ITfInsertAtSelection* pInsertAtSelection = nullptr;
    ITfRange* pRangeInsert = nullptr;
    ITfContextComposition* pContextComposition = nullptr;
    ITfComposition* pComposition = nullptr;

    if (FAILED(_pContext->QueryInterface(IID_ITfInsertAtSelection, (void **)&pInsertAtSelection)))
    {
        return S_OK;
    }

    if (FAILED(pInsertAtSelection->InsertTextAtSelection(ec, TF_IAS_QUERYONLY, NULL, 0, &pRangeInsert)))
    {
        pInsertAtSelection->Release();
        return S_OK;
    }

    if (FAILED(_pContext->QueryInterface(IID_ITfContextComposition, (void **)&pContextComposition)))
    {
        pRangeInsert->Release();
        pInsertAtSelection->Release();
        return S_OK;
    }

    if (SUCCEEDED(pContextComposition->StartComposition(ec, pRangeInsert, _pTextService, &pComposition)) && (nullptr != pComposition))
    {
        _pTextService->_SetComposition(pComposition);

        // set selection to the adjusted range
        TF_SELECTION tfSelection;
        tfSelection.range = pRangeInsert;
        tfSelection.style.ase = TF_AE_NONE;
        tfSelection.style.fInterimChar = FALSE;

        _pContext->SetSelection(ec, 1, &tfSelection);
        _pTextService->_SaveCompositionContext(_pContext);
    }

    pContextComposition->Release();
    pRangeInsert->Release();
    pInsertAtSelection->Release();

    return S_OK;
}


//+---------------------------------------------------------------------------
//
// _StartComposition
//
// this starts the new (std::nothrow) composition at the selection of the current
// focus context.
//----------------------------------------------------------------------------

void CJyutping::_StartComposition(_In_ ITfContext *pContext)
{
    CStartCompositionEditSession* pStartCompositionEditSession = new (std::nothrow) CStartCompositionEditSession(this, pContext);

    if (nullptr != pStartCompositionEditSession)
    {
        HRESULT hr = S_OK;
        pContext->RequestEditSession(_tfClientId, pStartCompositionEditSession, TF_ES_SYNC | TF_ES_READWRITE, &hr);

        pStartCompositionEditSession->Release();
    }
}

//+---------------------------------------------------------------------------
//
// _SaveCompositionContext
//
// this saves the context _pComposition belongs to; we need this to clear
// text attribute in case composition has not been terminated on
// deactivation
//----------------------------------------------------------------------------

void CJyutping::_SaveCompositionContext(_In_ ITfContext *pContext)
{
    assert(_pContext == nullptr);

    pContext->AddRef();
    _pContext = pContext;
}
