#pragma once

#include "Settings.h"

class CCompartment;
class CCompartmentEventSink;

class ILangBarItemButtonSettingsMenuHandler
{
public:
    virtual ~ILangBarItemButtonSettingsMenuHandler() = default;
    virtual DWORD CurrentCandidateFontSize() const = 0;
    virtual void SetCandidateFontSize(DWORD fontSize) = 0;
    virtual DWORD CurrentCandidateNumberFontSize() const = 0;
    virtual void SetCandidateNumberFontSize(DWORD fontSize) = 0;
    virtual DWORD CurrentCandidateCommentFontSize() const = 0;
    virtual void SetCandidateCommentFontSize(DWORD fontSize) = 0;
    virtual DWORD CurrentCandidatePageSize() const = 0;
    virtual void SetCandidatePageSize(DWORD pageSize) = 0;
    virtual PunctuationForm CurrentPunctuationForm() const = 0;
    virtual void SetPunctuationForm(PunctuationForm form) = 0;
    virtual CharacterForm CurrentCharacterForm() const = 0;
    virtual void SetCharacterForm(CharacterForm form) = 0;
    virtual CharacterVariant CurrentCharacterVariant() const = 0;
    virtual void SetCharacterVariant(CharacterVariant variant) = 0;
};

class CLangBarItemButton : public ITfLangBarItemButton,
    public ITfSource
{
public:
    CLangBarItemButton(
        REFGUID guidLangBar,
        LPCWSTR description,
        LPCWSTR tooltip,
        DWORD onIconIndex,
        DWORD offIconIndex,
        DWORD onDarkIconIndex,
        DWORD offDarkIconIndex,
        BOOL isSecureMode);
    ~CLangBarItemButton();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, _Outptr_ void **ppvObj);
    STDMETHODIMP_(ULONG) AddRef(void);
    STDMETHODIMP_(ULONG) Release(void);

    // ITfLangBarItem
    STDMETHODIMP GetInfo(_Out_ TF_LANGBARITEMINFO *pInfo);
    STDMETHODIMP GetStatus(_Out_ DWORD *pdwStatus);
    STDMETHODIMP Show(BOOL fShow);
    STDMETHODIMP GetTooltipString(_Out_ BSTR *pbstrToolTip);

    // ITfLangBarItemButton
    STDMETHODIMP OnClick(TfLBIClick click, POINT pt, _In_ const RECT *prcArea);
    STDMETHODIMP InitMenu(_In_ ITfMenu *pMenu);
    STDMETHODIMP OnMenuSelect(UINT wID);
    STDMETHODIMP GetIcon(_Out_ HICON *phIcon);
    STDMETHODIMP GetText(_Out_ BSTR *pbstrText);

    // ITfSource
    STDMETHODIMP AdviseSink(__RPC__in REFIID riid, __RPC__in_opt IUnknown *punk, __RPC__out DWORD *pdwCookie);
    STDMETHODIMP UnadviseSink(DWORD dwCookie);

    // Add/Remove languagebar item
    HRESULT _AddItem(_In_ ITfThreadMgr *pThreadMgr);
    HRESULT _RemoveItem(_In_ ITfThreadMgr *pThreadMgr);

    // Register compartment for button On/Off switch
    BOOL _RegisterCompartment(_In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId, REFGUID guidCompartment);
    BOOL _UnregisterCompartment(_In_ ITfThreadMgr *pThreadMgr);

    void CleanUp();

    void SetStatus(DWORD dwStatus, BOOL fSet);
    void SetSettingsMenuHandler(_In_opt_ ILangBarItemButtonSettingsMenuHandler *pSettingsMenuHandler);

private:
    HRESULT ShowSettingsMenu(POINT pt, _In_opt_ const RECT *prcArea);

    ITfLangBarItemSink* _pLangBarItemSink;
    ILangBarItemButtonSettingsMenuHandler* _pSettingsMenuHandler;

    TF_LANGBARITEMINFO _tfLangBarItemInfo;
    LPCWSTR _pTooltipText;
    DWORD _onIconIndex;
    DWORD _offIconIndex;
    DWORD _onDarkIconIndex;
    DWORD _offDarkIconIndex;

    BOOL _isAddedToLanguageBar;
    BOOL _isSecureMode;
    DWORD _status;

    CCompartment* _pCompartment;
    CCompartmentEventSink* _pCompartmentEventSink;
    static HRESULT _CompartmentCallback(_In_ void *pv, REFGUID guidCompartment);

    // The cookie for the sink to CLangBarItemButton.
    static const DWORD _cookie = 0;

    LONG _refCount;
};
