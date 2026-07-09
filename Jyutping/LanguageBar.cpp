#include "Private.h"
#include "Jyutping.h"
#include "CompositionProcessorEngine.h"
#include "LanguageBar.h"
#include "Globals.h"
#include "Compartment.h"
#include "Localization.h"
#include "resource.h"

namespace {

constexpr UINT MenuIdCharacterVariant = 1;
constexpr UINT MenuIdCharacterVariantTraditional = 2;
constexpr UINT MenuIdCharacterVariantHongKong = 3;
constexpr UINT MenuIdCharacterVariantTaiwan = 4;
constexpr UINT MenuIdCharacterVariantSimplified = 5;
constexpr UINT MenuIdCandidatePageSize = 6;
constexpr UINT MenuIdCandidatePageSizeFirst = 7;
constexpr UINT MenuIdCandidatePageSizeLast = 16;
constexpr UINT MenuIdMoreSettings = 17;
constexpr UINT MenuIdSeparator = 18;

std::wstring LoadMenuString(UINT resourceId, PCWSTR fallback)
{
    return Localization::LoadStringOrFallback(resourceId, fallback);
}

HRESULT AddMenuItem(_In_ ITfMenu *pMenu, UINT id, DWORD flags, const std::wstring& text, _Inout_opt_ ITfMenu **ppSubMenu = nullptr)
{
    return pMenu->AddMenuItem(
        id,
        flags,
        nullptr,
        nullptr,
        text.c_str(),
        static_cast<ULONG>(text.length()),
        ppSubMenu);
}

DWORD RadioFlagForVariant(CharacterVariant currentVariant, CharacterVariant itemVariant)
{
    return currentVariant == itemVariant ? TF_LBMENUF_RADIOCHECKED : 0;
}

UINT MenuIdForCharacterVariant(CharacterVariant variant)
{
    switch (variant)
    {
    case CharacterVariant::HongKong:
        return MenuIdCharacterVariantHongKong;
    case CharacterVariant::Taiwan:
        return MenuIdCharacterVariantTaiwan;
    case CharacterVariant::Simplified:
        return MenuIdCharacterVariantSimplified;
    case CharacterVariant::Traditional:
    default:
        return MenuIdCharacterVariantTraditional;
    }
}

UINT MenuIdForCandidatePageSize(DWORD pageSize)
{
    return MenuIdCandidatePageSizeFirst + pageSize - 1;
}

DWORD CandidatePageSizeForMenuId(UINT id)
{
    return id - MenuIdCandidatePageSizeFirst + 1;
}

BOOL AppendPopupMenuItem(_In_ HMENU menuHandle, UINT id, UINT flags, UINT resourceId, PCWSTR fallback)
{
    std::wstring text = LoadMenuString(resourceId, fallback);
    return AppendMenuW(menuHandle, flags, id, text.c_str());
}

POINT PopupPointFromClick(POINT pt, _In_opt_ const RECT *prcArea)
{
    POINT popupPoint = pt;
    if (popupPoint.x != 0 || popupPoint.y != 0)
    {
        return popupPoint;
    }

    if (GetCursorPos(&popupPoint))
    {
        return popupPoint;
    }

    if (prcArea != nullptr)
    {
        popupPoint.x = prcArea->left;
        popupPoint.y = prcArea->bottom;
    }
    return popupPoint;
}

} // namespace

void CJyutping::_UpdateLanguageBarOnSetFocus(_In_ ITfDocumentMgr *pDocMgrFocus)
{
    BOOL needDisableButtons = FALSE;

    if (!pDocMgrFocus)
    {
        needDisableButtons = TRUE;
    }
    else
    {
        IEnumTfContexts* pEnumContext = nullptr;

        if (FAILED(pDocMgrFocus->EnumContexts(&pEnumContext)) || !pEnumContext)
        {
            needDisableButtons = TRUE;
        }
        else
        {
            ULONG fetched = 0;
            ITfContext* pContext = nullptr;

            if (FAILED(pEnumContext->Next(1, &pContext, &fetched)) || fetched != 1)
            {
                needDisableButtons = TRUE;
            }

            if (!pContext)
            {
                // context is not associated
                needDisableButtons = TRUE;
            }
            else
            {
                pContext->Release();
            }
        }

        if (pEnumContext)
        {
            pEnumContext->Release();
        }
    }

    CCompositionProcessorEngine* pCompositionProcessorEngine = nullptr;
    pCompositionProcessorEngine = _pCompositionProcessorEngine;

    pCompositionProcessorEngine->SetLanguageBarStatus(TF_LBI_STATUS_DISABLED, needDisableButtons);
}

//+---------------------------------------------------------------------------
//
// CCompositionProcessorEngine::SetLanguageBarStatus
//
//----------------------------------------------------------------------------

VOID CCompositionProcessorEngine::SetLanguageBarStatus(DWORD status, BOOL isSet)
{
    if (_pLanguageBar_InputMethodMode) {
        _pLanguageBar_InputMethodMode->SetStatus(status, isSet);
    }
}

//+---------------------------------------------------------------------------
//
// CLangBarItemButton::ctor
//
//----------------------------------------------------------------------------

CLangBarItemButton::CLangBarItemButton(
    REFGUID guidLangBar,
    LPCWSTR description,
    LPCWSTR tooltip,
    DWORD onIconIndex,
    DWORD offIconIndex,
    DWORD onDarkIconIndex,
    DWORD offDarkIconIndex,
    BOOL isSecureMode)
{
    DWORD bufLen = 0;

    DllAddRef();

    // initialize TF_LANGBARITEMINFO structure.
    _tfLangBarItemInfo.clsidService = Global::JyutpingCLSID;												    // This LangBarItem belongs to this TextService.
    _tfLangBarItemInfo.guidItem = guidLangBar;															        // GUID of this LangBarItem.
    _tfLangBarItemInfo.dwStyle = (TF_LBI_STYLE_BTN_BUTTON | TF_LBI_STYLE_SHOWNINTRAY);						    // This LangBar is a button type.
    _tfLangBarItemInfo.ulSort = 0;																			    // The position of this LangBar Item is not specified.
    StringCchCopy(_tfLangBarItemInfo.szDescription, ARRAYSIZE(_tfLangBarItemInfo.szDescription), description);  // Set the description of this LangBar Item.

    // Initialize the sink pointer to NULL.
    _pLangBarItemSink = nullptr;
    _pSettingsMenuHandler = nullptr;

    // Initialize ICON index and file name.
    _onIconIndex = onIconIndex;
    _offIconIndex = offIconIndex;
    _onDarkIconIndex = onDarkIconIndex;
    _offDarkIconIndex = offDarkIconIndex;

    // Initialize compartment.
    _pCompartment = nullptr;
    _pCompartmentEventSink = nullptr;

    _isAddedToLanguageBar = FALSE;
    _isSecureMode = isSecureMode;
    _status = 0;

    _refCount = 1;

    // Initialize Tooltip
    _pTooltipText = nullptr;
    if (tooltip)
    {
        size_t len = 0;
        if (StringCchLength(tooltip, STRSAFE_MAX_CCH, &len) != S_OK)
        {
            len = 0;
        }
        bufLen = static_cast<DWORD>(len) + 1;
        _pTooltipText = (LPCWSTR) new (std::nothrow) WCHAR[ bufLen ];
        if (_pTooltipText)
        {
            StringCchCopy((LPWSTR)_pTooltipText, bufLen, tooltip);
        }
    }
}

//+---------------------------------------------------------------------------
//
// CLangBarItemButton::dtor
//
//----------------------------------------------------------------------------

CLangBarItemButton::~CLangBarItemButton()
{
    DllRelease();
    CleanUp();
}

//+---------------------------------------------------------------------------
//
// CLangBarItemButton::CleanUp
//
//----------------------------------------------------------------------------

void CLangBarItemButton::CleanUp()
{
    if (_pTooltipText)
    {
        delete [] _pTooltipText;
        _pTooltipText = nullptr;
    }

    ITfThreadMgr* pThreadMgr = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TF_ThreadMgr,
        NULL,
        CLSCTX_INPROC_SERVER,
        IID_ITfThreadMgr,
        (void**)&pThreadMgr);
    if (SUCCEEDED(hr))
    {
        _UnregisterCompartment(pThreadMgr);

        _RemoveItem(pThreadMgr);
        pThreadMgr->Release();
        pThreadMgr = nullptr;
    }

    if (_pCompartment)
    {
        delete _pCompartment;
        _pCompartment = nullptr;
    }

    if (_pCompartmentEventSink)
    {
        delete _pCompartmentEventSink;
        _pCompartmentEventSink = nullptr;
    }
}

//+---------------------------------------------------------------------------
//
// CLangBarItemButton::QueryInterface
//
//----------------------------------------------------------------------------

STDAPI CLangBarItemButton::QueryInterface(REFIID riid, _Outptr_ void **ppvObj)
{
    if (ppvObj == nullptr)
    {
        return E_INVALIDARG;
    }

    *ppvObj = nullptr;

    if (IsEqualIID(riid, IID_IUnknown) ||
        IsEqualIID(riid, IID_ITfLangBarItem) ||
        IsEqualIID(riid, IID_ITfLangBarItemButton))
    {
        *ppvObj = (ITfLangBarItemButton *)this;
    }
    else if (IsEqualIID(riid, IID_ITfSource))
    {
        *ppvObj = (ITfSource *)this;
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
// CLangBarItemButton::AddRef
//
//----------------------------------------------------------------------------

STDAPI_(ULONG) CLangBarItemButton::AddRef()
{
    return ++_refCount;
}

//+---------------------------------------------------------------------------
//
// CLangBarItemButton::Release
//
//----------------------------------------------------------------------------

STDAPI_(ULONG) CLangBarItemButton::Release()
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
// GetInfo
//
//----------------------------------------------------------------------------

STDAPI CLangBarItemButton::GetInfo(_Out_ TF_LANGBARITEMINFO *pInfo)
{
    _tfLangBarItemInfo.dwStyle |= TF_LBI_STYLE_SHOWNINTRAY;
    *pInfo = _tfLangBarItemInfo;
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// GetStatus
//
//----------------------------------------------------------------------------

STDAPI CLangBarItemButton::GetStatus(_Out_ DWORD *pdwStatus)
{
    if (pdwStatus == nullptr)
    {
        E_INVALIDARG;
    }

    *pdwStatus = _status;
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// SetStatus
//
//----------------------------------------------------------------------------

void CLangBarItemButton::SetStatus(DWORD dwStatus, BOOL fSet)
{
    BOOL isChange = FALSE;

    if (fSet)
    {
        if (!(_status & dwStatus))
        {
            _status |= dwStatus;
            isChange = TRUE;
        }
    }
    else
    {
        if (_status & dwStatus)
        {
            _status &= ~dwStatus;
            isChange = TRUE;
        }
    }

    if (isChange && _pLangBarItemSink)
    {
        _pLangBarItemSink->OnUpdate(TF_LBI_STATUS | TF_LBI_ICON);
    }

    return;
}

void CLangBarItemButton::SetSettingsMenuHandler(_In_opt_ ILangBarItemButtonSettingsMenuHandler *pSettingsMenuHandler)
{
    _pSettingsMenuHandler = pSettingsMenuHandler;
}

HRESULT CLangBarItemButton::ShowSettingsMenu(POINT pt, _In_opt_ const RECT *prcArea)
{
    if (_pSettingsMenuHandler == nullptr)
    {
        return S_OK;
    }

    HMENU menuHandle = CreatePopupMenu();
    HMENU characterVariantMenuHandle = CreatePopupMenu();
    HMENU candidatePageSizeMenuHandle = CreatePopupMenu();
    if (menuHandle == nullptr || characterVariantMenuHandle == nullptr || candidatePageSizeMenuHandle == nullptr)
    {
        if (candidatePageSizeMenuHandle != nullptr)
        {
            DestroyMenu(candidatePageSizeMenuHandle);
        }
        if (characterVariantMenuHandle != nullptr)
        {
            DestroyMenu(characterVariantMenuHandle);
        }
        if (menuHandle != nullptr)
        {
            DestroyMenu(menuHandle);
        }
        return E_OUTOFMEMORY;
    }

    std::wstring candidatePageSizeText = LoadMenuString(IDS_MENU_CANDIDATE_PAGE_SIZE, L"Candidate Count per Page");
    if (!AppendMenuW(menuHandle, MF_POPUP, reinterpret_cast<UINT_PTR>(candidatePageSizeMenuHandle), candidatePageSizeText.c_str()))
    {
        DestroyMenu(candidatePageSizeMenuHandle);
        DestroyMenu(characterVariantMenuHandle);
        DestroyMenu(menuHandle);
        return E_FAIL;
    }

    for (DWORD pageSize = 1; pageSize <= 10; pageSize++)
    {
        WCHAR pageSizeText[3] = {};
        StringCchPrintf(pageSizeText, ARRAYSIZE(pageSizeText), L"%lu", pageSize);
        AppendMenuW(candidatePageSizeMenuHandle, MF_STRING, MenuIdForCandidatePageSize(pageSize), pageSizeText);
    }

    CheckMenuRadioItem(
        candidatePageSizeMenuHandle,
        MenuIdCandidatePageSizeFirst,
        MenuIdCandidatePageSizeLast,
        MenuIdForCandidatePageSize(_pSettingsMenuHandler->CurrentCandidatePageSize()),
        MF_BYCOMMAND);

    std::wstring characterVariantText = LoadMenuString(IDS_MENU_CHARACTER_VARIANT, L"Character Variant");
    if (!AppendMenuW(menuHandle, MF_POPUP, reinterpret_cast<UINT_PTR>(characterVariantMenuHandle), characterVariantText.c_str()))
    {
        DestroyMenu(characterVariantMenuHandle);
        DestroyMenu(menuHandle);
        return E_FAIL;
    }

    AppendPopupMenuItem(characterVariantMenuHandle, MenuIdCharacterVariantTraditional, MF_STRING, IDS_MENU_CHARACTER_VARIANT_TRADITIONAL, L"Traditional");
    AppendPopupMenuItem(characterVariantMenuHandle, MenuIdCharacterVariantHongKong, MF_STRING, IDS_MENU_CHARACTER_VARIANT_HONG_KONG, L"Hong Kong");
    AppendPopupMenuItem(characterVariantMenuHandle, MenuIdCharacterVariantTaiwan, MF_STRING, IDS_MENU_CHARACTER_VARIANT_TAIWAN, L"Taiwan");
    AppendPopupMenuItem(characterVariantMenuHandle, MenuIdCharacterVariantSimplified, MF_STRING, IDS_MENU_CHARACTER_VARIANT_SIMPLIFIED, L"Simplified");

    CharacterVariant currentVariant = _pSettingsMenuHandler->CurrentCharacterVariant();
    CheckMenuRadioItem(
        characterVariantMenuHandle,
        MenuIdCharacterVariantTraditional,
        MenuIdCharacterVariantSimplified,
        MenuIdForCharacterVariant(currentVariant),
        MF_BYCOMMAND);

    AppendMenuW(menuHandle, MF_SEPARATOR, MenuIdSeparator, nullptr);
    AppendPopupMenuItem(menuHandle, MenuIdMoreSettings, MF_STRING | MF_GRAYED, IDS_MENU_MORE_SETTINGS, L"More Settings...");

    POINT popupPoint = PopupPointFromClick(pt, prcArea);
    HWND ownerWndHandle = GetActiveWindow();
    if (ownerWndHandle == nullptr)
    {
        ownerWndHandle = GetForegroundWindow();
    }
    if (ownerWndHandle == nullptr)
    {
        ownerWndHandle = GetDesktopWindow();
    }

    SetForegroundWindow(ownerWndHandle);
    UINT command = TrackPopupMenu(
        menuHandle,
        TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
        popupPoint.x,
        popupPoint.y,
        0,
        ownerWndHandle,
        nullptr);
    if (command != 0)
    {
        OnMenuSelect(command);
    }
    PostMessage(ownerWndHandle, WM_NULL, 0, 0);

    DestroyMenu(menuHandle);
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// Show
//
//----------------------------------------------------------------------------

STDAPI CLangBarItemButton::Show(BOOL fShow)
{
    fShow;
    if (_pLangBarItemSink)
    {
        _pLangBarItemSink->OnUpdate(TF_LBI_STATUS);
    }
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// GetTooltipString
//
//----------------------------------------------------------------------------

STDAPI CLangBarItemButton::GetTooltipString(_Out_ BSTR *pbstrToolTip)
{
    *pbstrToolTip = SysAllocString(_pTooltipText);

    return (*pbstrToolTip == nullptr) ? E_OUTOFMEMORY : S_OK;
}

//+---------------------------------------------------------------------------
//
// OnClick
//
//----------------------------------------------------------------------------

STDAPI CLangBarItemButton::OnClick(TfLBIClick click, POINT pt, _In_ const RECT *prcArea)
{
    if (click == TF_LBI_CLK_RIGHT)
    {
        return ShowSettingsMenu(pt, prcArea);
    }

    if (click != TF_LBI_CLK_LEFT)
    {
        return S_OK;
    }

    if (!_pCompartment)
    {
        return E_FAIL;
    }

    BOOL isOn = FALSE;

    _pCompartment->_GetCompartmentBOOL(isOn);
    _pCompartment->_SetCompartmentBOOL(isOn ? FALSE : TRUE);

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// InitMenu
//
//----------------------------------------------------------------------------

STDAPI CLangBarItemButton::InitMenu(_In_ ITfMenu *pMenu)
{
    if (pMenu == nullptr)
    {
        return E_INVALIDARG;
    }

    if (_pSettingsMenuHandler == nullptr)
    {
        return S_OK;
    }

    std::wstring candidatePageSizeText = LoadMenuString(IDS_MENU_CANDIDATE_PAGE_SIZE, L"Candidate Count per Page");
    ITfMenu* pCandidatePageSizeMenu = nullptr;
    HRESULT hr = AddMenuItem(
        pMenu,
        MenuIdCandidatePageSize,
        TF_LBMENUF_SUBMENU,
        candidatePageSizeText,
        &pCandidatePageSizeMenu);
    if (FAILED(hr))
    {
        return hr;
    }

    if (pCandidatePageSizeMenu)
    {
        DWORD currentPageSize = _pSettingsMenuHandler->CurrentCandidatePageSize();
        for (DWORD pageSize = 1; pageSize <= 10; pageSize++)
        {
            WCHAR pageSizeTextBuffer[3] = {};
            StringCchPrintf(pageSizeTextBuffer, ARRAYSIZE(pageSizeTextBuffer), L"%lu", pageSize);
            AddMenuItem(
                pCandidatePageSizeMenu,
                MenuIdForCandidatePageSize(pageSize),
                currentPageSize == pageSize ? TF_LBMENUF_RADIOCHECKED : 0,
                pageSizeTextBuffer);
        }
        pCandidatePageSizeMenu->Release();
    }

    CharacterVariant currentVariant = _pSettingsMenuHandler->CurrentCharacterVariant();
    std::wstring characterVariantText = LoadMenuString(IDS_MENU_CHARACTER_VARIANT, L"Character Variant");

    ITfMenu* pCharacterVariantMenu = nullptr;
    hr = AddMenuItem(
        pMenu,
        MenuIdCharacterVariant,
        TF_LBMENUF_SUBMENU,
        characterVariantText,
        &pCharacterVariantMenu);
    if (FAILED(hr))
    {
        return hr;
    }

    if (pCharacterVariantMenu)
    {
        AddMenuItem(
            pCharacterVariantMenu,
            MenuIdCharacterVariantTraditional,
            RadioFlagForVariant(currentVariant, CharacterVariant::Traditional),
            LoadMenuString(IDS_MENU_CHARACTER_VARIANT_TRADITIONAL, L"Traditional"));
        AddMenuItem(
            pCharacterVariantMenu,
            MenuIdCharacterVariantHongKong,
            RadioFlagForVariant(currentVariant, CharacterVariant::HongKong),
            LoadMenuString(IDS_MENU_CHARACTER_VARIANT_HONG_KONG, L"Hong Kong"));
        AddMenuItem(
            pCharacterVariantMenu,
            MenuIdCharacterVariantTaiwan,
            RadioFlagForVariant(currentVariant, CharacterVariant::Taiwan),
            LoadMenuString(IDS_MENU_CHARACTER_VARIANT_TAIWAN, L"Taiwan"));
        AddMenuItem(
            pCharacterVariantMenu,
            MenuIdCharacterVariantSimplified,
            RadioFlagForVariant(currentVariant, CharacterVariant::Simplified),
            LoadMenuString(IDS_MENU_CHARACTER_VARIANT_SIMPLIFIED, L"Simplified"));
        pCharacterVariantMenu->Release();
    }

    pMenu->AddMenuItem(MenuIdSeparator, TF_LBMENUF_SEPARATOR, nullptr, nullptr, nullptr, 0, nullptr);
    AddMenuItem(
        pMenu,
        MenuIdMoreSettings,
        TF_LBMENUF_GRAYED,
        LoadMenuString(IDS_MENU_MORE_SETTINGS, L"More Settings..."));

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// OnMenuSelect
//
//----------------------------------------------------------------------------

STDAPI CLangBarItemButton::OnMenuSelect(UINT wID)
{
    if (_pSettingsMenuHandler == nullptr)
    {
        return S_OK;
    }

    if (wID >= MenuIdCandidatePageSizeFirst && wID <= MenuIdCandidatePageSizeLast)
    {
        _pSettingsMenuHandler->SetCandidatePageSize(CandidatePageSizeForMenuId(wID));
        return S_OK;
    }

    switch (wID)
    {
    case MenuIdCharacterVariantTraditional:
        _pSettingsMenuHandler->SetCharacterVariant(CharacterVariant::Traditional);
        break;
    case MenuIdCharacterVariantHongKong:
        _pSettingsMenuHandler->SetCharacterVariant(CharacterVariant::HongKong);
        break;
    case MenuIdCharacterVariantTaiwan:
        _pSettingsMenuHandler->SetCharacterVariant(CharacterVariant::Taiwan);
        break;
    case MenuIdCharacterVariantSimplified:
        _pSettingsMenuHandler->SetCharacterVariant(CharacterVariant::Simplified);
        break;
    case MenuIdMoreSettings:
    default:
        break;
    }

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// GetIcon
//
//----------------------------------------------------------------------------

STDAPI CLangBarItemButton::GetIcon(_Out_ HICON *phIcon)
{
    BOOL isOn = FALSE;

    if (!_pCompartment)
    {
        return E_FAIL;
    }
    if (!phIcon)
    {
        return E_FAIL;
    }
    *phIcon = nullptr;

    _pCompartment->_GetCompartmentBOOL(isOn);

    DWORD status = 0;
    GetStatus(&status);

    // If IME is working on the UAC mode, the size of ICON should be 24 x 24.
    /*
    int desiredSize = 16;
    if (_isSecureMode) // detect UAC mode
    {
        desiredSize = _isSecureMode ? 24 : 16;
    }
    */
    int cx = GetSystemMetrics(SM_CXICON);
    int cy = GetSystemMetrics(SM_CYICON);

    // If we can get a window handle, we should use DPI-aware metrics.
    // However, LangBar items don't have a direct HWND.
    // We'll rely on the system-wide SM_CXICON for now as is, but we could improve this
    // if we had a reference window.
    // For now, let's at least make sure it's loaded with the correct size.
    Global::UpdateSystemTheme();
    BOOL isDarkTheme = (Global::GetSystemTheme() == Global::DARK_MODE);

    if (isOn && !(status & TF_LBI_STATUS_DISABLED))
    {
        if (Global::dllInstanceHandle)
        {
            DWORD iconIndex = isDarkTheme ? _onDarkIconIndex : _onIconIndex;
            *phIcon = reinterpret_cast<HICON>(LoadImage(Global::dllInstanceHandle, MAKEINTRESOURCE(iconIndex), IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR));
        }
    }
    else
    {
        if (Global::dllInstanceHandle)
        {
            DWORD iconIndex = isDarkTheme ? _offDarkIconIndex : _offIconIndex;
            *phIcon = reinterpret_cast<HICON>(LoadImage(Global::dllInstanceHandle, MAKEINTRESOURCE(iconIndex), IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR));
        }
    }

    return (*phIcon != NULL) ? S_OK : E_FAIL;
}

//+---------------------------------------------------------------------------
//
// GetText
//
//----------------------------------------------------------------------------

STDAPI CLangBarItemButton::GetText(_Out_ BSTR *pbstrText)
{
    *pbstrText = SysAllocString(_tfLangBarItemInfo.szDescription);

    return (*pbstrText == nullptr) ? E_OUTOFMEMORY : S_OK;
}

//+---------------------------------------------------------------------------
//
// AdviseSink
//
//----------------------------------------------------------------------------

STDAPI CLangBarItemButton::AdviseSink(__RPC__in REFIID riid, __RPC__in_opt IUnknown *punk, __RPC__out DWORD *pdwCookie)
{
    // We allow only ITfLangBarItemSink interface.
    if (!IsEqualIID(IID_ITfLangBarItemSink, riid))
    {
        return CONNECT_E_CANNOTCONNECT;
    }

    // We support only one sink once.
    if (_pLangBarItemSink != nullptr)
    {
        return CONNECT_E_ADVISELIMIT;
    }

    // Query the ITfLangBarItemSink interface and store it into _pLangBarItemSink.
    if (punk == nullptr)
    {
        return E_INVALIDARG;
    }
    if (punk->QueryInterface(IID_ITfLangBarItemSink, (void **)&_pLangBarItemSink) != S_OK)
    {
        _pLangBarItemSink = nullptr;
        return E_NOINTERFACE;
    }

    // return our cookie.
    *pdwCookie = _cookie;
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// UnadviseSink
//
//----------------------------------------------------------------------------

STDAPI CLangBarItemButton::UnadviseSink(DWORD dwCookie)
{
    // Check the given cookie.
    if (dwCookie != _cookie)
    {
        return CONNECT_E_NOCONNECTION;
    }

    // If there is nno connected sink, we just fail.
    if (_pLangBarItemSink == nullptr)
    {
        return CONNECT_E_NOCONNECTION;
    }

    _pLangBarItemSink->Release();
    _pLangBarItemSink = nullptr;

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _AddItem
//
//----------------------------------------------------------------------------

HRESULT CLangBarItemButton::_AddItem(_In_ ITfThreadMgr *pThreadMgr)
{
    HRESULT hr = S_OK;
    ITfLangBarItemMgr* pLangBarItemMgr = nullptr;

    if (_isAddedToLanguageBar)
    {
        return S_OK;
    }

    hr = pThreadMgr->QueryInterface(IID_ITfLangBarItemMgr, (void **)&pLangBarItemMgr);
    if (SUCCEEDED(hr))
    {
        hr = pLangBarItemMgr->AddItem(this);
        if (SUCCEEDED(hr))
        {
            _isAddedToLanguageBar = TRUE;
        }
        pLangBarItemMgr->Release();
    }

    return hr;
}

//+---------------------------------------------------------------------------
//
// _RemoveItem
//
//----------------------------------------------------------------------------

HRESULT CLangBarItemButton::_RemoveItem(_In_ ITfThreadMgr *pThreadMgr)
{
    HRESULT hr = S_OK;
    ITfLangBarItemMgr* pLangBarItemMgr = nullptr;

    if (!_isAddedToLanguageBar)
    {
        return S_OK;
    }

    hr = pThreadMgr->QueryInterface(IID_ITfLangBarItemMgr, (void **)&pLangBarItemMgr);
    if (SUCCEEDED(hr))
    {
        hr = pLangBarItemMgr->RemoveItem(this);
        if (SUCCEEDED(hr))
        {
            _isAddedToLanguageBar = FALSE;
        }
        pLangBarItemMgr->Release();
    }

    return hr;
}

//+---------------------------------------------------------------------------
//
// _RegisterCompartment
//
//----------------------------------------------------------------------------

BOOL CLangBarItemButton::_RegisterCompartment(_In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId, REFGUID guidCompartment)
{
    _pCompartment = new (std::nothrow) CCompartment(pThreadMgr, tfClientId, guidCompartment);
    if (_pCompartment)
    {
        // Advice ITfCompartmentEventSink
        _pCompartmentEventSink = new (std::nothrow) CCompartmentEventSink(_CompartmentCallback, this);
        if (_pCompartmentEventSink)
        {
            _pCompartmentEventSink->_Advise(pThreadMgr, guidCompartment);
        }
        else
        {
            delete _pCompartment;
            _pCompartment = nullptr;
        }
    }

    return _pCompartment ? TRUE : FALSE;
}

//+---------------------------------------------------------------------------
//
// _UnregisterCompartment
//
//----------------------------------------------------------------------------

BOOL CLangBarItemButton::_UnregisterCompartment(_In_ ITfThreadMgr *pThreadMgr)
{
    pThreadMgr;
    if (_pCompartment)
    {
        // Unadvice ITfCompartmentEventSink
        if (_pCompartmentEventSink)
        {
            _pCompartmentEventSink->_Unadvise();
        }

        // clear ITfCompartment
        _pCompartment->_ClearCompartment();
    }

    return TRUE;
}

//+---------------------------------------------------------------------------
//
// _CompartmentCallback
//
//----------------------------------------------------------------------------

// static
HRESULT CLangBarItemButton::_CompartmentCallback(_In_ void *pv, REFGUID guidCompartment)
{
    CLangBarItemButton* fakeThis = (CLangBarItemButton*)pv;

    GUID guid = GUID_NULL;
    fakeThis->_pCompartment->_GetGUID(&guid);

    if (IsEqualGUID(guid, guidCompartment))
    {
        if (fakeThis->_pLangBarItemSink)
        {
            fakeThis->_pLangBarItemSink->OnUpdate(TF_LBI_STATUS | TF_LBI_ICON);
        }
    }

    return S_OK;
}
