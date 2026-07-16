#include "Private.h"
#include "Globals.h"
#include "Jyutping.h"
#include "Logger.h"

//+---------------------------------------------------------------------------
//
// _StartComposition
//
//----------------------------------------------------------------------------

HRESULT CJyutping::_StartComposition(TfEditCookie ec, _In_ ITfContext *pContext)
{
    if (pContext == nullptr)
    {
        return E_INVALIDARG;
    }

    if (_pComposition != nullptr)
    {
        return S_OK;
    }

    if (_pContext != nullptr)
    {
        Global::Log(L"StartComposition failed: saved context exists without a composition");
        return E_UNEXPECTED;
    }

    HRESULT hr = E_FAIL;
    ITfInsertAtSelection* pInsertAtSelection = nullptr;
    ITfRange* pRangeInsert = nullptr;
    ITfContextComposition* pContextComposition = nullptr;
    ITfComposition* pComposition = nullptr;

    hr = pContext->QueryInterface(IID_ITfInsertAtSelection, (void **)&pInsertAtSelection);
    if (FAILED(hr) || pInsertAtSelection == nullptr)
    {
        hr = FAILED(hr) ? hr : E_UNEXPECTED;
        Global::Log(L"StartComposition failed: QueryInterface(ITfInsertAtSelection) hr=0x%08X", static_cast<unsigned int>(hr));
        return hr;
    }

    hr = pInsertAtSelection->InsertTextAtSelection(ec, TF_IAS_QUERYONLY, nullptr, 0, &pRangeInsert);
    if (FAILED(hr) || pRangeInsert == nullptr)
    {
        hr = FAILED(hr) ? hr : E_UNEXPECTED;
        Global::Log(L"StartComposition failed: InsertTextAtSelection hr=0x%08X", static_cast<unsigned int>(hr));
        pInsertAtSelection->Release();
        return hr;
    }

    hr = pContext->QueryInterface(IID_ITfContextComposition, (void **)&pContextComposition);
    if (FAILED(hr) || pContextComposition == nullptr)
    {
        hr = FAILED(hr) ? hr : E_UNEXPECTED;
        Global::Log(L"StartComposition failed: QueryInterface(ITfContextComposition) hr=0x%08X", static_cast<unsigned int>(hr));
        pRangeInsert->Release();
        pInsertAtSelection->Release();
        return hr;
    }

    hr = pContextComposition->StartComposition(ec, pRangeInsert, this, &pComposition);
    if (SUCCEEDED(hr) && pComposition == nullptr)
    {
        hr = E_UNEXPECTED;
    }

    if (SUCCEEDED(hr))
    {
        TF_SELECTION tfSelection = {};
        tfSelection.range = pRangeInsert;
        tfSelection.style.ase = TF_AE_NONE;
        tfSelection.style.fInterimChar = FALSE;

        hr = pContext->SetSelection(ec, 1, &tfSelection);
        if (FAILED(hr))
        {
            Global::Log(L"StartComposition failed: SetSelection hr=0x%08X", static_cast<unsigned int>(hr));
        }
    }
    else
    {
        Global::Log(L"StartComposition failed: StartComposition hr=0x%08X", static_cast<unsigned int>(hr));
    }

    if (SUCCEEDED(hr))
    {
        hr = _SaveCompositionContext(pContext);
    }

    if (SUCCEEDED(hr))
    {
        _SetComposition(pComposition);
        pComposition = nullptr;
    }
    else if (pComposition != nullptr)
    {
        pComposition->EndComposition(ec);
    }

    if (pComposition != nullptr)
    {
        pComposition->Release();
    }
    pContextComposition->Release();
    pRangeInsert->Release();
    pInsertAtSelection->Release();

    return hr;
}

//+---------------------------------------------------------------------------
//
// _SaveCompositionContext
//
// this saves the context _pComposition belongs to; we need this to clear
// text attribute in case composition has not been terminated on
// deactivation
//----------------------------------------------------------------------------

HRESULT CJyutping::_SaveCompositionContext(_In_ ITfContext *pContext)
{
    if (pContext == nullptr)
    {
        return E_INVALIDARG;
    }

    if (_pContext != nullptr)
    {
        Global::Log(L"SaveCompositionContext failed: a context is already saved");
        return E_UNEXPECTED;
    }

    pContext->AddRef();
    _pContext = pContext;

    return S_OK;
}
