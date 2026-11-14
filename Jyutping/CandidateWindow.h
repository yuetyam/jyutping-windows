#pragma once

#include "private.h"
#include "BaseWindow.h"
#include "ShadowWindow.h"
#include "JyutpingBaseStructure.h"
#include "OptionsView.h"

enum CANDWND_ACTION
{
    CAND_ITEM_SELECT,
    CAND_OPTION_SELECT
};

typedef HRESULT (*CANDWNDCALLBACK)(void *pv, enum CANDWND_ACTION action);

class CCandidateWindow : public CBaseWindow
{
public:
    CCandidateWindow(_In_ CANDWNDCALLBACK pfnCallback, _In_ void *pv, _In_ CCandidateRange *pIndexRange,
        DWORD candidateFontSize, DWORD numberFontSize, DWORD commentFontSize);
    virtual ~CCandidateWindow();

    BOOL _Create(ATOM atom, _In_opt_ HWND parentWndHandle);

    void _Move(int x, int y);
    void _Show(BOOL isShowWnd);
    void _ResizeWindow();
    void _SetFontSizes(DWORD candidateFontSize, DWORD numberFontSize, DWORD commentFontSize);

    VOID _SetTextColor(_In_ COLORREF crColor, _In_ COLORREF crBkColor);
    VOID _SetFillColor(_In_ HBRUSH hBrush);

    LRESULT CALLBACK _WindowProcCallback(_In_ HWND wndHandle, UINT uMsg, _In_ WPARAM wParam, _In_ LPARAM lParam);
    void _OnPaint(_In_ HDC dcHandle, _In_ PAINTSTRUCT *pps);
    void _OnLButtonDown(POINT pt);
    void _OnLButtonUp(POINT pt);
    void _OnMouseMove(POINT pt);

    void _AddString(_Inout_ CCandidateListItem *pCandidateItem);
    void _ClearList();
    UINT _GetCount()
    {
        return _candidateList.Count();
    }
    UINT _GetSelection()
    {
        return _currentSelection;
    }
    void _SetOptions(_In_reads_(count) const COptionsView::Row* rows, UINT count, UINT selection);
    void _ClearOptions();
    BOOL _IsOptionsMode() const { return _optionsMode; }
    UINT _GetOptionsSelection() const { return _optionsView.Selection(); }
    void _SetOptionsSelection(UINT selection);
    void _MoveOptionsSelection(int offset);

    DWORD _GetCandidateString(_In_ int iIndex, _Outptr_result_maybenull_z_ const WCHAR **ppwchCandidateString);
    DWORD _GetSelectedCandidateString(_Outptr_result_maybenull_ const WCHAR **ppwchCandidateString);
    DWORD_PTR _GetSelectedCandidateInputCount();

    BOOL _MoveSelection(_In_ int offSet);
    BOOL _SetSelection(_In_ int selectedIndex);
    void _SetSelectionIndex(_In_ UINT index);
    BOOL _MovePage(_In_ int offSet);
    BOOL _SetSelectionInPage(int nPos);

    HRESULT _GetPageIndex(UINT *pIndex, _In_ UINT uSize, _Inout_ UINT *puPageCnt);
    HRESULT _SetPageIndex(UINT *pIndex, _In_ UINT uPageCnt);
    HRESULT _GetCurrentPage(_Inout_ UINT *pCurrentPage);
    HRESULT _GetCurrentPage(_Inout_ int *pCurrentPage);

private:
    void _HandleMouseMsg(_In_ UINT mouseMsg, _In_ POINT point);
    HRESULT _DrawList(_In_ HDC dcHandle, _In_ UINT iIndex, _In_ RECT *prc);
    void _DrawBorder(_In_ HDC dcHandle, _In_ RECT *prc, _In_ int cx);
    HRESULT _GetPageBounds(_In_ UINT page, _Out_ UINT *pStart, _Out_ UINT *pEnd);
    BOOL _AdjustPageIndexForSelection();
    HRESULT _CurrentPageHasEmptyItems(_Inout_ BOOL *pfHasEmptyItems);

	// LightDismiss feature support, it will fire messages lightdismiss-related to the light dismiss layout.
    void _FireMessageToLightDismiss(_In_ HWND wndHandle, _In_ WINDOWPOS *pWndPos);

    BOOL _CreateMainWindow(ATOM atom, _In_opt_ HWND parentWndHandle);
    BOOL _CreateBackGroundShadowWindow();

    HRESULT _AdjustPageIndex(_Inout_ UINT & currentPage, _Inout_ UINT & currentPageIndex);

    void _DeleteShadowWnd();
    HRESULT _InitializeTextFormats(_In_ HWND wndHandle);
    HRESULT _EnsureRenderTarget();
    HRESULT _DrawOptions(_In_ HDC dcHandle, _In_ const RECT* prc);

private:
    // Selection and display state
    UINT _currentSelection;                             // Currently selected candidate index
    CJyutpingArray<CCandidateListItem> _candidateList; // List of all candidate items
    CJyutpingArray<UINT> _pageStartIndices;            // Starting indices for each page

    // Colors and brushes
    COLORREF _textColor;         // Text foreground color
    COLORREF _backgroundColor;   // Window background color
    HBRUSH _backgroundBrush;     // Background brush for painting

    // Text metrics for layout calculations
    TEXTMETRIC _candidateTextMetric;   // Metrics for candidate text
    TEXTMETRIC _numberLabelTextMetric; // Metrics for number labels

    // DirectWrite and Direct2D resources
    ComPtr<IDWriteTextFormat1> _pDWriteTextFormat;      // Text format for candidate strings
    ComPtr<IDWriteTextFormat1> _pDWriteNumberFormat;    // Text format for number labels
    ComPtr<IDWriteTextFormat1> _pDWriteCommentFormat;   // Text format for candidate comments
    ComPtr<ID2D1DCRenderTarget> _pDirect2DRenderTarget; // Direct2D render target
    BOOL _isAcrylic;

    // Window dimensions
    int _rowHeight;   // Height of each candidate row in pixels
    int _windowWidth; // Total width of the candidate window

    // Configuration
    CCandidateRange* _pIndexRange; // Range of indices shown per page (e.g., 1-9)

    // Callback mechanism
    CANDWNDCALLBACK _pfnCallback;   // Function pointer for selection callbacks
    void* _pCallbackObject;         // Object context passed to callback

    // Child windows
    CShadowWindow* _pShadowWnd;         // Drop shadow window

    // Behavioral flags
    BOOL _skipEmptyPageAdjustment; // Skip page index adjustment when page has empty slots
    DWORD _candidateFontSize;
    DWORD _numberFontSize;
    DWORD _commentFontSize;
    COptionsView _optionsView;
    BOOL _optionsMode;
};
