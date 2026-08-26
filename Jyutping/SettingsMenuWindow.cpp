#include "Private.h"
#include "Globals.h"
#include "BaseWindow.h"
#include "SettingsMenuWindow.h"
#include "WindowAppearance.h"

#include <oleacc.h>
#include <algorithm>
#include <memory>
#include <vector>

#pragma comment(lib, "oleacc.lib")

namespace {

constexpr BYTE MenuAcrylicAlpha = 0xD8;
constexpr UINT DefaultSubmenuDelay = 400;
constexpr float MaximumTextLayoutWidth = 4096.0f;

void SetSettingsMenuCursor()
{
    static HCURSOR cursor = LoadCursorW(nullptr, IDC_ARROW);
    if (cursor != nullptr)
    {
        SetCursor(cursor);
    }
}

class CSettingsMenuTracker;
class CSettingsMenuLevel;

class CSettingsMenuAccessible : public IAccessible
{
public:
    explicit CSettingsMenuAccessible(_In_ CSettingsMenuLevel* level);

    void Detach();

    STDMETHODIMP QueryInterface(REFIID riid, _Outptr_ void** object) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;
    STDMETHODIMP GetTypeInfoCount(_Out_ UINT* count) override;
    STDMETHODIMP GetTypeInfo(UINT typeInfo, LCID locale, _Outptr_ ITypeInfo** information) override;
    STDMETHODIMP GetIDsOfNames(REFIID riid, _In_reads_(nameCount) LPOLESTR* names, UINT nameCount, LCID locale, _Out_writes_(nameCount) DISPID* dispatchIds) override;
    STDMETHODIMP Invoke(DISPID dispatchId, REFIID riid, LCID locale, WORD flags, _In_ DISPPARAMS* parameters, _Out_opt_ VARIANT* result, _Out_opt_ EXCEPINFO* exception, _Out_opt_ UINT* argumentError) override;
    STDMETHODIMP get_accParent(_Outptr_result_maybenull_ IDispatch** parent) override;
    STDMETHODIMP get_accChildCount(_Out_ LONG* count) override;
    STDMETHODIMP get_accChild(VARIANT child, _Outptr_result_maybenull_ IDispatch** accessibleChild) override;
    STDMETHODIMP get_accName(VARIANT child, _Outptr_result_maybenull_ BSTR* name) override;
    STDMETHODIMP get_accValue(VARIANT child, _Outptr_result_maybenull_ BSTR* value) override;
    STDMETHODIMP get_accDescription(VARIANT child, _Outptr_result_maybenull_ BSTR* description) override;
    STDMETHODIMP get_accRole(VARIANT child, _Out_ VARIANT* role) override;
    STDMETHODIMP get_accState(VARIANT child, _Out_ VARIANT* state) override;
    STDMETHODIMP get_accHelp(VARIANT child, _Outptr_result_maybenull_ BSTR* help) override;
    STDMETHODIMP get_accHelpTopic(_Outptr_result_maybenull_ BSTR* helpFile, VARIANT child, _Out_ LONG* topicId) override;
    STDMETHODIMP get_accKeyboardShortcut(VARIANT child, _Outptr_result_maybenull_ BSTR* shortcut) override;
    STDMETHODIMP get_accFocus(_Out_ VARIANT* focus) override;
    STDMETHODIMP get_accSelection(_Out_ VARIANT* selection) override;
    STDMETHODIMP get_accDefaultAction(VARIANT child, _Outptr_result_maybenull_ BSTR* action) override;
    STDMETHODIMP accSelect(LONG flags, VARIANT child) override;
    STDMETHODIMP accLocation(_Out_ LONG* left, _Out_ LONG* top, _Out_ LONG* width, _Out_ LONG* height, VARIANT child) override;
    STDMETHODIMP accNavigate(LONG direction, VARIANT start, _Out_ VARIANT* destination) override;
    STDMETHODIMP accHitTest(LONG x, LONG y, _Out_ VARIANT* child) override;
    STDMETHODIMP accDoDefaultAction(VARIANT child) override;
    STDMETHODIMP put_accName(VARIANT child, _In_ BSTR name) override;
    STDMETHODIMP put_accValue(VARIANT child, _In_ BSTR value) override;

private:
    HRESULT ChildIndex(VARIANT child, _Out_ int* index, BOOL allowSelf) const;

    LONG _refCount = 1;
    CSettingsMenuLevel* _level;
};

class CSettingsMenuLevel : public CBaseWindow
{
public:
    CSettingsMenuLevel(_In_ CSettingsMenuTracker* tracker, _In_ const std::vector<SettingsMenu::Item>* items, _In_opt_ CSettingsMenuLevel* parent, int parentItemIndex);
    ~CSettingsMenuLevel() override;

    BOOL Create(POINT point, _In_opt_ HWND ownerWndHandle);
    void Destroy();
    void Relayout(POINT point);
    int HitTest(POINT screenPoint) const;
    RECT ItemScreenRect(int index) const;
    RECT WindowRect() const;
    void SetSelection(int index);
    int FirstSelectable() const;
    int LastSelectable() const;
    int NextSelectable(int current, int direction) const;
    const SettingsMenu::Item* ItemAt(int index) const;
    size_t ItemCount() const;
    int Selection() const;
    CSettingsMenuLevel* Parent() const;
    int ParentItemIndex() const;
    BOOL IsSubmenuOpen(int index) const;
    void StartSubmenuTimer(UINT delay);
    void StopSubmenuTimer();
    CSettingsMenuAccessible* AccessibleObject();
    void InvokeAccessibleItem(int index);

    LRESULT CALLBACK _WindowProcCallback(_In_ HWND wndHandle, UINT message, _In_ WPARAM wParam, _In_ LPARAM lParam) override;
    void _OnPaint(_In_ HDC dcHandle, _In_ PAINTSTRUCT* paint) override;
    void _OnTimer() override;

private:
    void InitializeResources(_In_ HWND wndHandle);
    void ReleaseResources();
    void Measure();
    void PaintDirect2D(_In_ HDC dcHandle, _In_ const RECT& clientRect);
    void PaintGdi(_In_ HDC dcHandle, _In_ const RECT& clientRect);
    COLORREF BackgroundColor() const;
    COLORREF TextColor(const SettingsMenu::Item& item, BOOL selected) const;
    COLORREF HighlightColor() const;
    COLORREF BorderColor() const;
    COLORREF SeparatorColor() const;
    int Scale(int value) const;

    CSettingsMenuTracker* _tracker;
    const std::vector<SettingsMenu::Item>* _items;
    CSettingsMenuLevel* _parent;
    int _parentItemIndex;
    int _selection = -1;
    UINT _dpi = USER_DEFAULT_SCREEN_DPI;
    int _rowHeight = 0;
    int _separatorHeight = 0;
    int _width = 0;
    int _height = 0;
    int _leftGutter = 0;
    int _rightGutter = 0;
    std::vector<RECT> _rows;
    LOGFONTW _menuLogFont = {};
    HFONT _menuFont = nullptr;
    ComPtr<IDWriteTextFormat1> _textFormat;
    ComPtr<ID2D1DCRenderTarget> _renderTarget;
    BOOL _acrylic = FALSE;
    CSettingsMenuAccessible* _accessible = nullptr;
};

class CSettingsMenuTracker
{
public:
    CSettingsMenuTracker(_In_ const SettingsMenu::Snapshot& snapshot, _In_opt_ HWND ownerWndHandle);
    ~CSettingsMenuTracker();

    HRESULT Track(POINT popupPoint, _Out_ UINT* selectedCommand);
    void HandleMouseMessage(UINT message, POINT screenPoint);
    void HandlePointerMessage(UINT message, POINT screenPoint);
    void HandleTimer();
    void HandleKeyboard(UINT virtualKey);
    void HandleSystemChange();
    void Cancel();
    void InvokeAccessible(_In_ CSettingsMenuLevel* level, int index);
    BOOL IsSubmenuOpen(_In_ const CSettingsMenuLevel* level, int index) const;

private:
    CSettingsMenuLevel* LevelAtPoint(POINT screenPoint) const;
    int LevelIndex(_In_ const CSettingsMenuLevel* level) const;
    void Select(_In_ CSettingsMenuLevel* level, int index);
    void ScheduleSubmenu(_In_ CSettingsMenuLevel* level, int index);
    void CancelPendingSubmenu();
    void OpenSubmenu(_In_ CSettingsMenuLevel* level, int index);
    void CloseLevelsAfter(int levelIndex);
    void Invoke(_In_ CSettingsMenuLevel* level, int index);
    void DestroyLevels();
    POINT SubmenuPoint(_In_ CSettingsMenuLevel* parent, int parentIndex, int submenuWidth, int submenuHeight) const;
    RECT WorkAreaForPoint(POINT point) const;
    void StopTracking(UINT command = 0);

    const SettingsMenu::Snapshot& _snapshot;
    HWND _ownerWndHandle;
    HWND _previousForegroundWndHandle = nullptr;
    HWND _previousFocusWndHandle = nullptr;
    std::vector<std::unique_ptr<CSettingsMenuLevel>> _levels;
    CSettingsMenuLevel* _pendingLevel = nullptr;
    int _pendingIndex = -1;
    BOOL _tracking = FALSE;
    UINT _command = 0;
    BOOL _buttonDown = FALSE;
};

COLORREF Blend(COLORREF foreground, COLORREF background, BYTE foregroundWeight)
{
    BYTE backgroundWeight = 255 - foregroundWeight;
    return RGB(
        (GetRValue(foreground) * foregroundWeight + GetRValue(background) * backgroundWeight) / 255,
        (GetGValue(foreground) * foregroundWeight + GetGValue(background) * backgroundWeight) / 255,
        (GetBValue(foreground) * foregroundWeight + GetBValue(background) * backgroundWeight) / 255);
}

CSettingsMenuLevel::CSettingsMenuLevel(_In_ CSettingsMenuTracker* tracker, _In_ const std::vector<SettingsMenu::Item>* items, _In_opt_ CSettingsMenuLevel* parent, int parentItemIndex) :
    _tracker(tracker), _items(items), _parent(parent), _parentItemIndex(parentItemIndex)
{
    _SetUIWnd(this);
}

CSettingsMenuLevel::~CSettingsMenuLevel()
{
    Destroy();
    if (_accessible != nullptr)
    {
        _accessible->Detach();
        _accessible->Release();
        _accessible = nullptr;
    }
}

BOOL CSettingsMenuLevel::Create(POINT point, _In_opt_ HWND ownerWndHandle)
{
    if (!CBaseWindow::_Create(Global::AtomSettingsMenuWindow, WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, WS_POPUP, nullptr, 1, 1, ownerWndHandle))
    {
        return FALSE;
    }
    InitializeResources(_wndHandle);
    Measure();
    Relayout(point);
    _acrylic = WindowAppearance::ApplyAcrylic(_wndHandle, BackgroundColor(), MenuAcrylicAlpha) && _renderTarget != nullptr;
    WindowAppearance::ApplyRoundedCorners(_wndHandle, TRUE);
    return TRUE;
}

void CSettingsMenuLevel::Destroy()
{
    if (_wndHandle != nullptr)
    {
        NotifyWinEvent(EVENT_SYSTEM_MENUPOPUPEND, _wndHandle, OBJID_CLIENT, CHILDID_SELF);
    }
    ReleaseResources();
    CBaseWindow::_Destroy();
}

void CSettingsMenuLevel::InitializeResources(_In_ HWND wndHandle)
{
    ReleaseResources();
    _dpi = GetDpiForWindow(wndHandle);
    if (_dpi == 0)
    {
        _dpi = USER_DEFAULT_SCREEN_DPI;
    }

    NONCLIENTMETRICSW metrics = {};
    metrics.cbSize = sizeof(metrics);
    if (!SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, _dpi))
    {
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
    }
    _menuLogFont = metrics.lfMenuFont;
    _menuFont = CreateFontIndirectW(&_menuLogFont);

    if (Global::pDWriteFactory != nullptr && _menuLogFont.lfFaceName[0] != L'\0')
    {
        LPCWSTR family = _menuLogFont.lfFaceName;
        FLOAT fontSize = static_cast<FLOAT>(max(1L, abs(_menuLogFont.lfHeight)));
        WindowAppearance::CreateTextFormat(fontSize, &family, 1, Global::pDWriteMenuFontFallback, &_textFormat);
        if (_textFormat != nullptr)
        {
            _textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        }

        D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1_PIXEL_FORMAT{ DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED },
            USER_DEFAULT_SCREEN_DPI,
            USER_DEFAULT_SCREEN_DPI,
            D2D1_RENDER_TARGET_USAGE_NONE,
            D2D1_FEATURE_LEVEL_DEFAULT);
        ComPtr<ID2D1Factory> factory;
        if (SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.ReleaseAndGetAddressOf())))
        {
            factory->CreateDCRenderTarget(&properties, &_renderTarget);
        }
    }
}

void CSettingsMenuLevel::ReleaseResources()
{
    _renderTarget.Reset();
    _textFormat.Reset();
    if (_menuFont != nullptr)
    {
        DeleteObject(_menuFont);
        _menuFont = nullptr;
    }
}

int CSettingsMenuLevel::Scale(int value) const
{
    return MulDiv(value, _dpi, USER_DEFAULT_SCREEN_DPI);
}

void CSettingsMenuLevel::Measure()
{
    _leftGutter = Scale(30);
    _rightGutter = Scale(26);
    _separatorHeight = max(Scale(9), 3);
    int fontHeight = max(abs(_menuLogFont.lfHeight), Scale(12));
    _rowHeight = max(fontHeight + Scale(10), GetSystemMetricsForDpi(SM_CYMENU, _dpi));
    int maximumTextWidth = 0;

    HDC dcHandle = GetDC(_wndHandle);
    HFONT previousFont = nullptr;
    if (dcHandle != nullptr && _menuFont != nullptr)
    {
        previousFont = static_cast<HFONT>(SelectObject(dcHandle, _menuFont));
    }
    for (const SettingsMenu::Item& item : *_items)
    {
        if (item.kind == SettingsMenu::ItemKind::Separator)
        {
            continue;
        }
        int width = 0;
        if (_textFormat != nullptr && Global::pDWriteFactory != nullptr)
        {
            ComPtr<IDWriteTextLayout> layout;
            if (SUCCEEDED(Global::pDWriteFactory->CreateTextLayout(item.text.c_str(), static_cast<UINT32>(item.text.length()), _textFormat.Get(), MaximumTextLayoutWidth, static_cast<FLOAT>(_rowHeight), &layout)))
            {
                DWRITE_TEXT_METRICS textMetrics = {};
                if (SUCCEEDED(layout->GetMetrics(&textMetrics)))
                {
                    width = static_cast<int>(ceil(textMetrics.widthIncludingTrailingWhitespace));
                }
            }
        }
        if (width == 0 && dcHandle != nullptr)
        {
            SIZE size = {};
            if (GetTextExtentPoint32W(dcHandle, item.text.c_str(), static_cast<int>(item.text.length()), &size))
            {
                width = size.cx;
            }
        }
        maximumTextWidth = max(maximumTextWidth, width);
    }
    if (dcHandle != nullptr)
    {
        if (previousFont != nullptr)
        {
            SelectObject(dcHandle, previousFont);
        }
        ReleaseDC(_wndHandle, dcHandle);
    }

    _width = max(Scale(150), _leftGutter + maximumTextWidth + _rightGutter + Scale(12));
    _height = Scale(8);
    _rows.clear();
    for (const SettingsMenu::Item& item : *_items)
    {
        int rowHeight = item.kind == SettingsMenu::ItemKind::Separator ? _separatorHeight : _rowHeight;
        RECT row = { 0, _height, _width, _height + rowHeight };
        _rows.push_back(row);
        _height += rowHeight;
    }
    _height += Scale(8);
}

void CSettingsMenuLevel::Relayout(POINT point)
{
    HMONITOR monitor = MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo = {};
    monitorInfo.cbSize = sizeof(monitorInfo);
    GetMonitorInfoW(monitor, &monitorInfo);
    RECT workArea = monitorInfo.rcWork;
    point.x = max(workArea.left, min(point.x, workArea.right - _width));
    point.y = max(workArea.top, min(point.y, workArea.bottom - _height));
    SetWindowPos(_wndHandle, HWND_TOPMOST, point.x, point.y, _width, _height, SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

int CSettingsMenuLevel::HitTest(POINT screenPoint) const
{
    POINT clientPoint = screenPoint;
    ScreenToClient(_wndHandle, &clientPoint);
    for (size_t index = 0; index < _rows.size(); index++)
    {
        if (PtInRect(&_rows[index], clientPoint))
        {
            return static_cast<int>(index);
        }
    }
    return -1;
}

RECT CSettingsMenuLevel::ItemScreenRect(int index) const
{
    RECT row = {};
    if (index >= 0 && static_cast<size_t>(index) < _rows.size())
    {
        row = _rows[index];
        MapWindowPoints(_wndHandle, HWND_DESKTOP, reinterpret_cast<POINT*>(&row), 2);
    }
    return row;
}

RECT CSettingsMenuLevel::WindowRect() const
{
    RECT rect = {};
    GetWindowRect(_wndHandle, &rect);
    return rect;
}

const SettingsMenu::Item* CSettingsMenuLevel::ItemAt(int index) const
{
    return index >= 0 && static_cast<size_t>(index) < _items->size() ? &(*_items)[index] : nullptr;
}

size_t CSettingsMenuLevel::ItemCount() const
{
    return _items->size();
}

void CSettingsMenuLevel::SetSelection(int index)
{
    if (_selection == index)
    {
        return;
    }
    _selection = index;
    _InvalidateRect();
    if (_selection >= 0)
    {
        NotifyWinEvent(EVENT_OBJECT_FOCUS, _wndHandle, OBJID_CLIENT, _selection + 1);
        NotifyWinEvent(EVENT_OBJECT_SELECTION, _wndHandle, OBJID_CLIENT, _selection + 1);
    }
}

int CSettingsMenuLevel::FirstSelectable() const
{
    return NextSelectable(-1, 1);
}

int CSettingsMenuLevel::LastSelectable() const
{
    return NextSelectable(static_cast<int>(_items->size()), -1);
}

int CSettingsMenuLevel::NextSelectable(int current, int direction) const
{
    if (_items->empty())
    {
        return -1;
    }
    int count = static_cast<int>(_items->size());
    for (int offset = 1; offset <= count; offset++)
    {
        int index = (current + direction * offset) % count;
        if (index < 0)
        {
            index += count;
        }
        const SettingsMenu::Item& item = (*_items)[index];
        if (item.kind != SettingsMenu::ItemKind::Separator && item.enabled)
        {
            return index;
        }
    }
    return -1;
}

int CSettingsMenuLevel::Selection() const { return _selection; }
CSettingsMenuLevel* CSettingsMenuLevel::Parent() const { return _parent; }
int CSettingsMenuLevel::ParentItemIndex() const { return _parentItemIndex; }
BOOL CSettingsMenuLevel::IsSubmenuOpen(int index) const { return _tracker->IsSubmenuOpen(this, index); }

void CSettingsMenuLevel::StartSubmenuTimer(UINT delay)
{
    _StartTimer(delay);
}

void CSettingsMenuLevel::StopSubmenuTimer()
{
    _EndTimer();
}

CSettingsMenuAccessible* CSettingsMenuLevel::AccessibleObject()
{
    if (_accessible == nullptr)
    {
        _accessible = new (std::nothrow) CSettingsMenuAccessible(this);
    }
    return _accessible;
}

void CSettingsMenuLevel::InvokeAccessibleItem(int index)
{
    _tracker->InvokeAccessible(this, index);
}

COLORREF CSettingsMenuLevel::BackgroundColor() const
{
    return WindowAppearance::IsHighContrastEnabled() ? GetSysColor(COLOR_MENU) : Global::GetCandidateWindowBackgroundColor();
}

COLORREF CSettingsMenuLevel::HighlightColor() const
{
    return WindowAppearance::IsHighContrastEnabled() ? GetSysColor(COLOR_HIGHLIGHT) : Global::GetHighlightedBackColor();
}

COLORREF CSettingsMenuLevel::BorderColor() const
{
    return WindowAppearance::IsHighContrastEnabled() ? GetSysColor(COLOR_WINDOWFRAME) : Global::GetCandidateWindowBorderColor();
}

COLORREF CSettingsMenuLevel::SeparatorColor() const
{
    return WindowAppearance::IsHighContrastEnabled() ? GetSysColor(COLOR_GRAYTEXT) : Blend(Global::GetNormalTextColor(), BackgroundColor(), 64);
}

COLORREF CSettingsMenuLevel::TextColor(const SettingsMenu::Item& item, BOOL selected) const
{
    if (!item.enabled)
    {
        return WindowAppearance::IsHighContrastEnabled() ? GetSysColor(COLOR_GRAYTEXT) : Blend(Global::GetNormalTextColor(), BackgroundColor(), 120);
    }
    if (selected)
    {
        return WindowAppearance::IsHighContrastEnabled() ? GetSysColor(COLOR_HIGHLIGHTTEXT) : Global::GetHighlightedTextColor();
    }
    return WindowAppearance::IsHighContrastEnabled() ? GetSysColor(COLOR_MENUTEXT) : Global::GetNormalTextColor();
}

void CSettingsMenuLevel::_OnPaint(_In_ HDC dcHandle, _In_ PAINTSTRUCT* paint)
{
    RECT clientRect = {};
    GetClientRect(_wndHandle, &clientRect);
    if (_renderTarget != nullptr && _textFormat != nullptr)
    {
        PaintDirect2D(dcHandle, clientRect);
    }
    else
    {
        PaintGdi(dcHandle, clientRect);
    }
    paint;
}

void CSettingsMenuLevel::PaintDirect2D(_In_ HDC dcHandle, _In_ const RECT& clientRect)
{
    if (FAILED(_renderTarget->BindDC(dcHandle, &clientRect)))
    {
        PaintGdi(dcHandle, clientRect);
        return;
    }
    _renderTarget->BeginDraw();
    _renderTarget->SetTransform(D2D1::IdentityMatrix());
    _renderTarget->Clear(WindowAppearance::ColorFromColorRef(BackgroundColor(), _acrylic ? 0.0f : 1.0f));

    ComPtr<ID2D1SolidColorBrush> separatorBrush;
    _renderTarget->CreateSolidColorBrush(WindowAppearance::ColorFromColorRef(SeparatorColor()), &separatorBrush);
    for (size_t index = 0; index < _items->size(); index++)
    {
        const SettingsMenu::Item& item = (*_items)[index];
        const RECT& row = _rows[index];
        if (item.kind == SettingsMenu::ItemKind::Separator)
        {
            if (separatorBrush != nullptr)
            {
                FLOAT y = static_cast<FLOAT>((row.top + row.bottom) / 2);
                _renderTarget->DrawLine(D2D1::Point2F(static_cast<FLOAT>(Scale(10)), y), D2D1::Point2F(static_cast<FLOAT>(_width - Scale(10)), y), separatorBrush.Get(), 1.0f);
            }
            continue;
        }

        BOOL selected = static_cast<int>(index) == _selection;
        if (selected)
        {
            ComPtr<ID2D1SolidColorBrush> highlightBrush;
            _renderTarget->CreateSolidColorBrush(WindowAppearance::ColorFromColorRef(HighlightColor()), &highlightBrush);
            if (highlightBrush != nullptr)
            {
                _renderTarget->FillRectangle(D2D1::RectF(0.0f, static_cast<FLOAT>(row.top), static_cast<FLOAT>(_width), static_cast<FLOAT>(row.bottom)), highlightBrush.Get());
            }
        }

        ComPtr<ID2D1SolidColorBrush> textBrush;
        _renderTarget->CreateSolidColorBrush(WindowAppearance::ColorFromColorRef(TextColor(item, selected)), &textBrush);
        if (textBrush == nullptr)
        {
            continue;
        }
        D2D1_RECT_F textRect = D2D1::RectF(static_cast<FLOAT>(_leftGutter), static_cast<FLOAT>(row.top), static_cast<FLOAT>(_width - _rightGutter), static_cast<FLOAT>(row.bottom));
        _renderTarget->DrawTextW(item.text.c_str(), static_cast<UINT32>(item.text.length()), _textFormat.Get(), textRect, textBrush.Get(), D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);

        FLOAT centerY = static_cast<FLOAT>((row.top + row.bottom) / 2);
        if (item.checked)
        {
            D2D1_ELLIPSE outer = D2D1::Ellipse(D2D1::Point2F(static_cast<FLOAT>(_leftGutter / 2), centerY), static_cast<FLOAT>(Scale(6)), static_cast<FLOAT>(Scale(6)));
            D2D1_ELLIPSE inner = D2D1::Ellipse(outer.point, static_cast<FLOAT>(Scale(3)), static_cast<FLOAT>(Scale(3)));
            _renderTarget->DrawEllipse(outer, textBrush.Get(), 1.0f);
            _renderTarget->FillEllipse(inner, textBrush.Get());
        }
        if (item.kind == SettingsMenu::ItemKind::Submenu)
        {
            FLOAT x = static_cast<FLOAT>(_width - _rightGutter / 2);
            FLOAT size = static_cast<FLOAT>(Scale(4));
            _renderTarget->DrawLine(D2D1::Point2F(x - size, centerY - size), D2D1::Point2F(x, centerY), textBrush.Get(), 1.5f);
            _renderTarget->DrawLine(D2D1::Point2F(x, centerY), D2D1::Point2F(x - size, centerY + size), textBrush.Get(), 1.5f);
        }
    }
    if (!WindowAppearance::IsWindows11OrGreater())
    {
        ComPtr<ID2D1SolidColorBrush> borderBrush;
        _renderTarget->CreateSolidColorBrush(WindowAppearance::ColorFromColorRef(BorderColor()), &borderBrush);
        if (borderBrush != nullptr)
        {
            FLOAT borderWidth = static_cast<FLOAT>(CANDWND_BORDER_WIDTH);
            _renderTarget->FillRectangle(D2D1::RectF(0.0f, 0.0f, static_cast<FLOAT>(_width), borderWidth), borderBrush.Get());
            _renderTarget->FillRectangle(D2D1::RectF(0.0f, static_cast<FLOAT>(_height) - borderWidth, static_cast<FLOAT>(_width), static_cast<FLOAT>(_height)), borderBrush.Get());
            _renderTarget->FillRectangle(D2D1::RectF(0.0f, borderWidth, borderWidth, static_cast<FLOAT>(_height) - borderWidth), borderBrush.Get());
            _renderTarget->FillRectangle(D2D1::RectF(static_cast<FLOAT>(_width) - borderWidth, borderWidth, static_cast<FLOAT>(_width), static_cast<FLOAT>(_height) - borderWidth), borderBrush.Get());
        }
    }
    if (FAILED(_renderTarget->EndDraw()))
    {
        _renderTarget.Reset();
    }
}

void CSettingsMenuLevel::PaintGdi(_In_ HDC dcHandle, _In_ const RECT& clientRect)
{
    HBRUSH backgroundBrush = CreateSolidBrush(BackgroundColor());
    FillRect(dcHandle, &clientRect, backgroundBrush);
    DeleteObject(backgroundBrush);
    HFONT previousFont = _menuFont == nullptr ? nullptr : static_cast<HFONT>(SelectObject(dcHandle, _menuFont));
    SetBkMode(dcHandle, TRANSPARENT);

    for (size_t index = 0; index < _items->size(); index++)
    {
        const SettingsMenu::Item& item = (*_items)[index];
        RECT row = _rows[index];
        if (item.kind == SettingsMenu::ItemKind::Separator)
        {
            HPEN pen = CreatePen(PS_SOLID, 1, SeparatorColor());
            HPEN previousPen = static_cast<HPEN>(SelectObject(dcHandle, pen));
            MoveToEx(dcHandle, Scale(10), (row.top + row.bottom) / 2, nullptr);
            LineTo(dcHandle, _width - Scale(10), (row.top + row.bottom) / 2);
            SelectObject(dcHandle, previousPen);
            DeleteObject(pen);
            continue;
        }

        BOOL selected = static_cast<int>(index) == _selection;
        if (selected)
        {
            HBRUSH highlightBrush = CreateSolidBrush(HighlightColor());
            FillRect(dcHandle, &row, highlightBrush);
            DeleteObject(highlightBrush);
        }
        SetTextColor(dcHandle, TextColor(item, selected));
        RECT textRect = { _leftGutter, row.top, _width - _rightGutter, row.bottom };
        DrawTextW(dcHandle, item.text.c_str(), static_cast<int>(item.text.length()), &textRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

        int centerY = (row.top + row.bottom) / 2;
        HPEN pen = CreatePen(PS_SOLID, max(1, Scale(1)), TextColor(item, selected));
        HPEN previousPen = static_cast<HPEN>(SelectObject(dcHandle, pen));
        if (item.checked)
        {
            HBRUSH brush = CreateSolidBrush(TextColor(item, selected));
            HBRUSH previousBrush = static_cast<HBRUSH>(SelectObject(dcHandle, brush));
            Ellipse(dcHandle, _leftGutter / 2 - Scale(3), centerY - Scale(3), _leftGutter / 2 + Scale(3), centerY + Scale(3));
            SelectObject(dcHandle, previousBrush);
            DeleteObject(brush);
        }
        if (item.kind == SettingsMenu::ItemKind::Submenu)
        {
            int x = _width - _rightGutter / 2;
            int size = Scale(4);
            MoveToEx(dcHandle, x - size, centerY - size, nullptr);
            LineTo(dcHandle, x, centerY);
            LineTo(dcHandle, x - size, centerY + size);
        }
        SelectObject(dcHandle, previousPen);
        DeleteObject(pen);
        if (selected && WindowAppearance::IsHighContrastEnabled())
        {
            DrawFocusRect(dcHandle, &row);
        }
    }
    if (!WindowAppearance::IsWindows11OrGreater())
    {
        HBRUSH borderBrush = CreateSolidBrush(BorderColor());
        if (borderBrush != nullptr)
        {
            RECT border = clientRect;
            border.bottom = clientRect.top + CANDWND_BORDER_WIDTH;
            FillRect(dcHandle, &border, borderBrush);
            border = clientRect;
            border.top = clientRect.bottom - CANDWND_BORDER_WIDTH;
            FillRect(dcHandle, &border, borderBrush);
            border = clientRect;
            border.top += CANDWND_BORDER_WIDTH;
            border.right = clientRect.left + CANDWND_BORDER_WIDTH;
            border.bottom -= CANDWND_BORDER_WIDTH;
            FillRect(dcHandle, &border, borderBrush);
            border = clientRect;
            border.top += CANDWND_BORDER_WIDTH;
            border.left = clientRect.right - CANDWND_BORDER_WIDTH;
            border.bottom -= CANDWND_BORDER_WIDTH;
            FillRect(dcHandle, &border, borderBrush);
            DeleteObject(borderBrush);
        }
    }
    if (previousFont != nullptr)
    {
        SelectObject(dcHandle, previousFont);
    }
}

LRESULT CALLBACK CSettingsMenuLevel::_WindowProcCallback(_In_ HWND wndHandle, UINT message, _In_ WPARAM wParam, _In_ LPARAM lParam)
{
    switch (message)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT paint = {};
        HDC dcHandle = BeginPaint(wndHandle, &paint);
        _OnPaint(dcHandle, &paint);
        EndPaint(wndHandle, &paint);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_POINTERACTIVATE:
        return PA_NOACTIVATE;
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT)
        {
            SetSettingsMenuCursor();
            return TRUE;
        }
        break;
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    {
        SetSettingsMenuCursor();
        POINT point = {};
        POINTSTOPOINT(point, MAKEPOINTS(lParam));
        ClientToScreen(wndHandle, &point);
        _tracker->HandleMouseMessage(message, point);
        return 0;
    }
    case WM_POINTERDOWN:
    case WM_POINTERUPDATE:
    case WM_POINTERUP:
    {
        SetSettingsMenuCursor();
        POINTER_INFO pointerInfo = {};
        if (GetPointerInfo(GET_POINTERID_WPARAM(wParam), &pointerInfo))
        {
            _tracker->HandlePointerMessage(message, pointerInfo.ptPixelLocation);
        }
        return 0;
    }
    case WM_CANCELMODE:
    case WM_CAPTURECHANGED:
        _tracker->Cancel();
        return 0;
    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
    case WM_DWMCOLORIZATIONCOLORCHANGED:
        _tracker->HandleSystemChange();
        return 0;
    case WM_DPICHANGED:
        _tracker->HandleSystemChange();
        return 0;
    case WM_GETOBJECT:
        if (static_cast<LONG>(lParam) == OBJID_CLIENT)
        {
            CSettingsMenuAccessible* accessible = AccessibleObject();
            return accessible == nullptr ? 0 : LresultFromObject(IID_IAccessible, wParam, accessible);
        }
        break;
    }
    return DefWindowProcW(wndHandle, message, wParam, lParam);
}

void CSettingsMenuLevel::_OnTimer()
{
    _tracker->HandleTimer();
}

CSettingsMenuTracker::CSettingsMenuTracker(_In_ const SettingsMenu::Snapshot& snapshot, _In_opt_ HWND ownerWndHandle) : _snapshot(snapshot), _ownerWndHandle(ownerWndHandle)
{
}

CSettingsMenuTracker::~CSettingsMenuTracker()
{
    StopTracking();
    DestroyLevels();
}

RECT CSettingsMenuTracker::WorkAreaForPoint(POINT point) const
{
    MONITORINFO monitorInfo = {};
    monitorInfo.cbSize = sizeof(monitorInfo);
    GetMonitorInfoW(MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST), &monitorInfo);
    return monitorInfo.rcWork;
}

POINT CSettingsMenuTracker::SubmenuPoint(_In_ CSettingsMenuLevel* parent, int parentIndex, int submenuWidth, int submenuHeight) const
{
    RECT parentWindow = parent->WindowRect();
    RECT parentRow = parent->ItemScreenRect(parentIndex);
    POINT point = { parentWindow.right - 1, parentRow.top };
    RECT workArea = WorkAreaForPoint({ parentWindow.right, parentRow.top });
    if (point.x + submenuWidth > workArea.right)
    {
        point.x = parentWindow.left - submenuWidth + 1;
    }
    point.x = max(workArea.left, min(point.x, workArea.right - submenuWidth));
    point.y = max(workArea.top, min(point.y, workArea.bottom - submenuHeight));
    return point;
}

HRESULT CSettingsMenuTracker::Track(POINT popupPoint, _Out_ UINT* selectedCommand)
{
    if (selectedCommand == nullptr || !SettingsMenu::ValidateSnapshot(_snapshot))
    {
        return E_INVALIDARG;
    }
    *selectedCommand = 0;
    _previousForegroundWndHandle = GetForegroundWindow();
    _previousFocusWndHandle = GetFocus();

    std::unique_ptr<CSettingsMenuLevel> root = std::make_unique<CSettingsMenuLevel>(this, &_snapshot.items, nullptr, -1);
    if (!root->Create(popupPoint, _ownerWndHandle))
    {
        return HRESULT_FROM_WIN32(GetLastError() == ERROR_SUCCESS ? ERROR_NOT_ENOUGH_MEMORY : GetLastError());
    }
    _levels.push_back(std::move(root));
    _levels.front()->_Show(TRUE);
    SetSettingsMenuCursor();
    NotifyWinEvent(EVENT_SYSTEM_MENUPOPUPSTART, _levels.front()->_GetWnd(), OBJID_CLIENT, CHILDID_SELF);
    _tracking = TRUE;
    SetCapture(_levels.front()->_GetWnd());
    if (GetCapture() != _levels.front()->_GetWnd())
    {
        StopTracking();
        return E_FAIL;
    }

    MSG message = {};
    BOOL repostQuit = FALSE;
    int quitCode = 0;
    while (_tracking)
    {
        BOOL result = GetMessageW(&message, nullptr, 0, 0);
        if (result <= 0)
        {
            if (result == 0)
            {
                repostQuit = TRUE;
                quitCode = static_cast<int>(message.wParam);
            }
            StopTracking();
            break;
        }
        if (_previousForegroundWndHandle != nullptr && GetForegroundWindow() != _previousForegroundWndHandle)
        {
            StopTracking();
            break;
        }
        if ((_ownerWndHandle != nullptr && !IsWindow(_ownerWndHandle)) || (message.message == WM_ACTIVATEAPP && message.wParam == FALSE))
        {
            StopTracking();
            break;
        }
        if (message.message == WM_KEYDOWN || message.message == WM_SYSKEYDOWN)
        {
            HandleKeyboard(static_cast<UINT>(message.wParam));
            continue;
        }
        if (message.message == WM_MOUSEWHEEL || message.message == WM_MOUSEHWHEEL)
        {
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    UINT command = _command;
    StopTracking();
    DestroyLevels();
    if (repostQuit)
    {
        PostQuitMessage(quitCode);
    }
    if (_previousForegroundWndHandle == GetForegroundWindow() && _previousFocusWndHandle != nullptr && IsWindow(_previousFocusWndHandle) && GetFocus() != _previousFocusWndHandle)
    {
        SetFocus(_previousFocusWndHandle);
    }
    *selectedCommand = command;
    return S_OK;
}

void CSettingsMenuTracker::StopTracking(UINT command)
{
    if (command != 0)
    {
        _command = command;
    }
    _tracking = FALSE;
    if (!_levels.empty())
    {
        _levels.front()->StopSubmenuTimer();
    }
    if (!_levels.empty() && GetCapture() == _levels.front()->_GetWnd())
    {
        ReleaseCapture();
    }
    for (const std::unique_ptr<CSettingsMenuLevel>& level : _levels)
    {
        level->_Show(FALSE);
    }
    _pendingLevel = nullptr;
    _pendingIndex = -1;
}

void CSettingsMenuTracker::DestroyLevels()
{
    for (auto level = _levels.rbegin(); level != _levels.rend(); ++level)
    {
        (*level)->Destroy();
    }
    _levels.clear();
}

int CSettingsMenuTracker::LevelIndex(_In_ const CSettingsMenuLevel* level) const
{
    for (size_t index = 0; index < _levels.size(); index++)
    {
        if (_levels[index].get() == level)
        {
            return static_cast<int>(index);
        }
    }
    return -1;
}

CSettingsMenuLevel* CSettingsMenuTracker::LevelAtPoint(POINT screenPoint) const
{
    for (auto level = _levels.rbegin(); level != _levels.rend(); ++level)
    {
        RECT rect = (*level)->WindowRect();
        if (PtInRect(&rect, screenPoint))
        {
            return level->get();
        }
    }
    return nullptr;
}

void CSettingsMenuTracker::Select(_In_ CSettingsMenuLevel* level, int index)
{
    const SettingsMenu::Item* item = level->ItemAt(index);
    if (item == nullptr || item->kind == SettingsMenu::ItemKind::Separator || !item->enabled)
    {
        CancelPendingSubmenu();
        level->SetSelection(-1);
        CloseLevelsAfter(LevelIndex(level));
        return;
    }
    level->SetSelection(index);
    int levelIndex = LevelIndex(level);
    if (item->kind == SettingsMenu::ItemKind::Submenu)
    {
        ScheduleSubmenu(level, index);
    }
    else
    {
        CancelPendingSubmenu();
        CloseLevelsAfter(levelIndex);
    }
}

void CSettingsMenuTracker::ScheduleSubmenu(_In_ CSettingsMenuLevel* level, int index)
{
    int levelIndex = LevelIndex(level);
    if (levelIndex >= 0 && static_cast<size_t>(levelIndex + 1) < _levels.size() && _levels[levelIndex + 1]->ParentItemIndex() == index)
    {
        CancelPendingSubmenu();
        return;
    }
    if (_pendingLevel == level && _pendingIndex == index)
    {
        return;
    }
    _pendingLevel = level;
    _pendingIndex = index;
    UINT delay = DefaultSubmenuDelay;
    SystemParametersInfoW(SPI_GETMENUSHOWDELAY, 0, &delay, 0);
    if (!_levels.empty())
    {
        _levels.front()->StartSubmenuTimer(max(delay, 1U));
    }
}

void CSettingsMenuTracker::CancelPendingSubmenu()
{
    if (!_levels.empty())
    {
        _levels.front()->StopSubmenuTimer();
    }
    _pendingLevel = nullptr;
    _pendingIndex = -1;
}

void CSettingsMenuTracker::HandleTimer()
{
    CSettingsMenuLevel* level = _pendingLevel;
    int index = _pendingIndex;
    CancelPendingSubmenu();
    if (level != nullptr && level->Selection() == index)
    {
        OpenSubmenu(level, index);
    }
}

void CSettingsMenuTracker::OpenSubmenu(_In_ CSettingsMenuLevel* level, int index)
{
    const SettingsMenu::Item* item = level->ItemAt(index);
    int levelIndex = LevelIndex(level);
    if (item == nullptr || item->kind != SettingsMenu::ItemKind::Submenu || levelIndex < 0)
    {
        return;
    }
    CancelPendingSubmenu();
    if (static_cast<size_t>(levelIndex + 1) < _levels.size() && _levels[levelIndex + 1]->ParentItemIndex() == index)
    {
        return;
    }
    CloseLevelsAfter(levelIndex);
    std::unique_ptr<CSettingsMenuLevel> submenu = std::make_unique<CSettingsMenuLevel>(this, &item->children, level, index);
    RECT parentRow = level->ItemScreenRect(index);
    if (!submenu->Create({ parentRow.right, parentRow.top }, _ownerWndHandle))
    {
        StopTracking();
        return;
    }
    RECT measured = submenu->WindowRect();
    POINT point = SubmenuPoint(level, index, measured.right - measured.left, measured.bottom - measured.top);
    submenu->Relayout(point);
    submenu->_Show(TRUE);
    NotifyWinEvent(EVENT_SYSTEM_MENUPOPUPSTART, submenu->_GetWnd(), OBJID_CLIENT, CHILDID_SELF);
    _levels.push_back(std::move(submenu));
    NotifyWinEvent(EVENT_OBJECT_STATECHANGE, level->_GetWnd(), OBJID_CLIENT, index + 1);
}

void CSettingsMenuTracker::CloseLevelsAfter(int levelIndex)
{
    while (static_cast<int>(_levels.size()) > levelIndex + 1)
    {
        _levels.back()->Destroy();
        _levels.pop_back();
    }
}

void CSettingsMenuTracker::Invoke(_In_ CSettingsMenuLevel* level, int index)
{
    const SettingsMenu::Item* item = level->ItemAt(index);
    if (item == nullptr || !item->enabled || item->kind == SettingsMenu::ItemKind::Separator)
    {
        return;
    }
    level->SetSelection(index);
    if (item->kind == SettingsMenu::ItemKind::Submenu)
    {
        OpenSubmenu(level, index);
        if (_tracking && !_levels.empty())
        {
            _levels.back()->SetSelection(_levels.back()->FirstSelectable());
        }
        return;
    }
    NotifyWinEvent(EVENT_OBJECT_INVOKED, level->_GetWnd(), OBJID_CLIENT, index + 1);
    StopTracking(item->id);
}

void CSettingsMenuTracker::HandleMouseMessage(UINT message, POINT screenPoint)
{
    CSettingsMenuLevel* level = LevelAtPoint(screenPoint);
    if (message == WM_MOUSEMOVE)
    {
        if (level != nullptr)
        {
            Select(level, level->HitTest(screenPoint));
        }
        else
        {
            CancelPendingSubmenu();
        }
        return;
    }
    if (message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN)
    {
        _buttonDown = TRUE;
        if (level == nullptr)
        {
            StopTracking();
            return;
        }
        Select(level, level->HitTest(screenPoint));
        return;
    }
    if (message == WM_LBUTTONUP || message == WM_RBUTTONUP)
    {
        if (!_buttonDown)
        {
            return;
        }
        _buttonDown = FALSE;
        if (level == nullptr)
        {
            StopTracking();
            return;
        }
        Invoke(level, level->HitTest(screenPoint));
    }
}

void CSettingsMenuTracker::HandlePointerMessage(UINT message, POINT screenPoint)
{
    UINT mouseMessage = message == WM_POINTERDOWN ? WM_LBUTTONDOWN : (message == WM_POINTERUP ? WM_LBUTTONUP : WM_MOUSEMOVE);
    HandleMouseMessage(mouseMessage, screenPoint);
}

void CSettingsMenuTracker::HandleKeyboard(UINT virtualKey)
{
    if (_levels.empty())
    {
        return;
    }
    CSettingsMenuLevel* level = _levels.back().get();
    int selection = level->Selection();
    switch (virtualKey)
    {
    case VK_UP:
        level->SetSelection(level->NextSelectable(selection, -1));
        break;
    case VK_DOWN:
        level->SetSelection(level->NextSelectable(selection, 1));
        break;
    case VK_HOME:
        level->SetSelection(level->FirstSelectable());
        break;
    case VK_END:
        level->SetSelection(level->LastSelectable());
        break;
    case VK_RIGHT:
        if (selection < 0)
        {
            level->SetSelection(level->FirstSelectable());
        }
        else if (level->ItemAt(selection)->kind == SettingsMenu::ItemKind::Submenu)
        {
            Invoke(level, selection);
        }
        break;
    case VK_LEFT:
        if (_levels.size() > 1)
        {
            int parentIndex = level->ParentItemIndex();
            CloseLevelsAfter(static_cast<int>(_levels.size()) - 2);
            _levels.back()->SetSelection(parentIndex);
        }
        break;
    case VK_RETURN:
    case VK_SPACE:
        if (selection < 0)
        {
            level->SetSelection(level->FirstSelectable());
        }
        else
        {
            Invoke(level, selection);
        }
        break;
    case VK_ESCAPE:
        StopTracking();
        break;
    }
}

void CSettingsMenuTracker::HandleSystemChange()
{
    Global::UpdateSystemTheme();
    StopTracking();
}

void CSettingsMenuTracker::Cancel()
{
    StopTracking();
}

void CSettingsMenuTracker::InvokeAccessible(_In_ CSettingsMenuLevel* level, int index)
{
    Invoke(level, index);
}

BOOL CSettingsMenuTracker::IsSubmenuOpen(_In_ const CSettingsMenuLevel* level, int index) const
{
    int levelIndex = LevelIndex(level);
    return levelIndex >= 0 && static_cast<size_t>(levelIndex + 1) < _levels.size() && _levels[levelIndex + 1]->ParentItemIndex() == index;
}

CSettingsMenuAccessible::CSettingsMenuAccessible(_In_ CSettingsMenuLevel* level) : _level(level)
{
}

void CSettingsMenuAccessible::Detach() { _level = nullptr; }

STDMETHODIMP CSettingsMenuAccessible::QueryInterface(REFIID riid, _Outptr_ void** object)
{
    if (object == nullptr)
    {
        return E_INVALIDARG;
    }
    *object = nullptr;
    if (riid == IID_IUnknown || riid == IID_IDispatch || riid == IID_IAccessible)
    {
        *object = static_cast<IAccessible*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CSettingsMenuAccessible::AddRef() { return static_cast<ULONG>(InterlockedIncrement(&_refCount)); }
STDMETHODIMP_(ULONG) CSettingsMenuAccessible::Release()
{
    ULONG count = static_cast<ULONG>(InterlockedDecrement(&_refCount));
    if (count == 0)
    {
        delete this;
    }
    return count;
}

STDMETHODIMP CSettingsMenuAccessible::GetTypeInfoCount(_Out_ UINT* count) { if (count == nullptr) return E_INVALIDARG; *count = 0; return S_OK; }
STDMETHODIMP CSettingsMenuAccessible::GetTypeInfo(UINT, LCID, _Outptr_ ITypeInfo**) { return E_NOTIMPL; }
STDMETHODIMP CSettingsMenuAccessible::GetIDsOfNames(REFIID, _In_reads_(nameCount) LPOLESTR*, UINT, LCID, _Out_writes_(nameCount) DISPID*) { return E_NOTIMPL; }
STDMETHODIMP CSettingsMenuAccessible::Invoke(DISPID, REFIID, LCID, WORD, _In_ DISPPARAMS*, _Out_opt_ VARIANT*, _Out_opt_ EXCEPINFO*, _Out_opt_ UINT*) { return E_NOTIMPL; }

HRESULT CSettingsMenuAccessible::ChildIndex(VARIANT child, _Out_ int* index, BOOL allowSelf) const
{
    if (index == nullptr || _level == nullptr || child.vt != VT_I4)
    {
        return E_INVALIDARG;
    }
    if (allowSelf && child.lVal == CHILDID_SELF)
    {
        *index = -1;
        return S_OK;
    }
    if (child.lVal < 1 || static_cast<size_t>(child.lVal) > _level->ItemCount())
    {
        return E_INVALIDARG;
    }
    *index = static_cast<int>(child.lVal - 1);
    return S_OK;
}

STDMETHODIMP CSettingsMenuAccessible::get_accParent(_Outptr_result_maybenull_ IDispatch** parent)
{
    if (parent == nullptr) return E_INVALIDARG;
    *parent = nullptr;
    if (_level == nullptr || _level->Parent() == nullptr) return S_FALSE;
    CSettingsMenuAccessible* accessible = _level->Parent()->AccessibleObject();
    if (accessible == nullptr) return E_OUTOFMEMORY;
    accessible->AddRef();
    *parent = accessible;
    return S_OK;
}

STDMETHODIMP CSettingsMenuAccessible::get_accChildCount(_Out_ LONG* count)
{
    if (count == nullptr) return E_INVALIDARG;
    *count = _level == nullptr ? 0 : static_cast<LONG>(_level->ItemCount());
    return _level == nullptr ? E_FAIL : S_OK;
}

STDMETHODIMP CSettingsMenuAccessible::get_accChild(VARIANT, _Outptr_result_maybenull_ IDispatch** child)
{
    if (child == nullptr) return E_INVALIDARG;
    *child = nullptr;
    return S_FALSE;
}

STDMETHODIMP CSettingsMenuAccessible::get_accName(VARIANT child, _Outptr_result_maybenull_ BSTR* name)
{
    if (name == nullptr) return E_INVALIDARG;
    *name = nullptr;
    int index = -1;
    HRESULT hr = ChildIndex(child, &index, TRUE);
    if (FAILED(hr)) return hr;
    const WCHAR* value = index < 0 ? L"Settings menu" : _level->ItemAt(index)->text.c_str();
    *name = SysAllocString(value);
    return *name == nullptr ? E_OUTOFMEMORY : S_OK;
}

STDMETHODIMP CSettingsMenuAccessible::get_accValue(VARIANT child, _Outptr_result_maybenull_ BSTR* value)
{
    if (value == nullptr) return E_INVALIDARG;
    *value = nullptr;
    int index = -1;
    HRESULT hr = ChildIndex(child, &index, FALSE);
    if (FAILED(hr)) return hr;
    WCHAR commandId[16] = {};
    StringCchPrintfW(commandId, ARRAYSIZE(commandId), L"%u", _level->ItemAt(index)->id);
    *value = SysAllocString(commandId);
    return *value == nullptr ? E_OUTOFMEMORY : S_OK;
}
STDMETHODIMP CSettingsMenuAccessible::get_accDescription(VARIANT, _Outptr_result_maybenull_ BSTR* description) { if (description == nullptr) return E_INVALIDARG; *description = nullptr; return S_FALSE; }

STDMETHODIMP CSettingsMenuAccessible::get_accRole(VARIANT child, _Out_ VARIANT* role)
{
    if (role == nullptr) return E_INVALIDARG;
    int index = -1;
    HRESULT hr = ChildIndex(child, &index, TRUE);
    if (FAILED(hr)) return hr;
    VariantInit(role);
    role->vt = VT_I4;
    role->lVal = index < 0 ? ROLE_SYSTEM_MENUPOPUP : (index >= 0 && _level->ItemAt(index)->kind == SettingsMenu::ItemKind::Separator ? ROLE_SYSTEM_SEPARATOR : ROLE_SYSTEM_MENUITEM);
    return S_OK;
}

STDMETHODIMP CSettingsMenuAccessible::get_accState(VARIANT child, _Out_ VARIANT* state)
{
    if (state == nullptr) return E_INVALIDARG;
    int index = -1;
    HRESULT hr = ChildIndex(child, &index, TRUE);
    if (FAILED(hr)) return hr;
    VariantInit(state);
    state->vt = VT_I4;
    if (index < 0)
    {
        state->lVal = 0;
        return S_OK;
    }
    const SettingsMenu::Item* item = _level->ItemAt(index);
    LONG value = item->enabled ? STATE_SYSTEM_FOCUSABLE | STATE_SYSTEM_SELECTABLE : STATE_SYSTEM_UNAVAILABLE;
    if (_level->Selection() == index) value |= STATE_SYSTEM_FOCUSED | STATE_SYSTEM_SELECTED | STATE_SYSTEM_HOTTRACKED;
    if (item->checked) value |= STATE_SYSTEM_CHECKED;
    if (item->kind == SettingsMenu::ItemKind::Submenu)
    {
        value |= STATE_SYSTEM_HASPOPUP;
        value |= _level->IsSubmenuOpen(index) ? STATE_SYSTEM_EXPANDED : STATE_SYSTEM_COLLAPSED;
    }
    state->lVal = value;
    return S_OK;
}

STDMETHODIMP CSettingsMenuAccessible::get_accHelp(VARIANT, _Outptr_result_maybenull_ BSTR* help) { if (help == nullptr) return E_INVALIDARG; *help = nullptr; return S_FALSE; }
STDMETHODIMP CSettingsMenuAccessible::get_accHelpTopic(_Outptr_result_maybenull_ BSTR* helpFile, VARIANT, _Out_ LONG* topicId) { if (helpFile == nullptr || topicId == nullptr) return E_INVALIDARG; *helpFile = nullptr; *topicId = 0; return S_FALSE; }
STDMETHODIMP CSettingsMenuAccessible::get_accKeyboardShortcut(VARIANT, _Outptr_result_maybenull_ BSTR* shortcut) { if (shortcut == nullptr) return E_INVALIDARG; *shortcut = nullptr; return S_FALSE; }

STDMETHODIMP CSettingsMenuAccessible::get_accFocus(_Out_ VARIANT* focus)
{
    if (focus == nullptr || _level == nullptr) return E_INVALIDARG;
    VariantInit(focus);
    focus->vt = VT_I4;
    focus->lVal = _level->Selection() < 0 ? CHILDID_SELF : _level->Selection() + 1;
    return S_OK;
}

STDMETHODIMP CSettingsMenuAccessible::get_accSelection(_Out_ VARIANT* selection) { return get_accFocus(selection); }

STDMETHODIMP CSettingsMenuAccessible::get_accDefaultAction(VARIANT child, _Outptr_result_maybenull_ BSTR* action)
{
    if (action == nullptr) return E_INVALIDARG;
    *action = nullptr;
    int index = -1;
    HRESULT hr = ChildIndex(child, &index, FALSE);
    if (FAILED(hr)) return hr;
    *action = SysAllocString(_level->ItemAt(index)->kind == SettingsMenu::ItemKind::Submenu ? L"Open" : L"Execute");
    return *action == nullptr ? E_OUTOFMEMORY : S_OK;
}

STDMETHODIMP CSettingsMenuAccessible::accSelect(LONG flags, VARIANT child)
{
    int index = -1;
    HRESULT hr = ChildIndex(child, &index, FALSE);
    if (FAILED(hr)) return hr;
    const SettingsMenu::Item* item = _level->ItemAt(index);
    if (!item->enabled || item->kind == SettingsMenu::ItemKind::Separator) return E_INVALIDARG;
    if ((flags & (SELFLAG_TAKEFOCUS | SELFLAG_TAKESELECTION)) != 0) _level->SetSelection(index);
    return S_OK;
}

STDMETHODIMP CSettingsMenuAccessible::accLocation(_Out_ LONG* left, _Out_ LONG* top, _Out_ LONG* width, _Out_ LONG* height, VARIANT child)
{
    if (left == nullptr || top == nullptr || width == nullptr || height == nullptr) return E_INVALIDARG;
    int index = -1;
    HRESULT hr = ChildIndex(child, &index, TRUE);
    if (FAILED(hr)) return hr;
    RECT rect = index < 0 ? _level->WindowRect() : _level->ItemScreenRect(index);
    *left = rect.left; *top = rect.top; *width = rect.right - rect.left; *height = rect.bottom - rect.top;
    return S_OK;
}

STDMETHODIMP CSettingsMenuAccessible::accNavigate(LONG direction, VARIANT start, _Out_ VARIANT* destination)
{
    if (destination == nullptr) return E_INVALIDARG;
    VariantInit(destination);
    int index = -1;
    HRESULT hr = ChildIndex(start, &index, TRUE);
    if (FAILED(hr)) return hr;
    int target = -1;
    if (direction == NAVDIR_FIRSTCHILD && index < 0 && _level->ItemCount() > 0) target = 0;
    else if (direction == NAVDIR_LASTCHILD && index < 0 && _level->ItemCount() > 0) target = static_cast<int>(_level->ItemCount()) - 1;
    else if (direction == NAVDIR_NEXT && index >= 0 && static_cast<size_t>(index + 1) < _level->ItemCount()) target = index + 1;
    else if (direction == NAVDIR_PREVIOUS && index > 0) target = index - 1;
    if (target < 0) return S_FALSE;
    destination->vt = VT_I4;
    destination->lVal = target + 1;
    return S_OK;
}

STDMETHODIMP CSettingsMenuAccessible::accHitTest(LONG x, LONG y, _Out_ VARIANT* child)
{
    if (child == nullptr || _level == nullptr) return E_INVALIDARG;
    VariantInit(child);
    int index = _level->HitTest({ x, y });
    child->vt = VT_I4;
    child->lVal = index < 0 ? CHILDID_SELF : index + 1;
    return S_OK;
}

STDMETHODIMP CSettingsMenuAccessible::accDoDefaultAction(VARIANT child)
{
    int index = -1;
    HRESULT hr = ChildIndex(child, &index, FALSE);
    if (FAILED(hr)) return hr;
    _level->InvokeAccessibleItem(index);
    return S_OK;
}

STDMETHODIMP CSettingsMenuAccessible::put_accName(VARIANT, _In_ BSTR) { return E_NOTIMPL; }
STDMETHODIMP CSettingsMenuAccessible::put_accValue(VARIANT, _In_ BSTR) { return E_NOTIMPL; }

}

HRESULT TrackSettingsMenu(_In_ const SettingsMenu::Snapshot& snapshot, POINT popupPoint, _In_opt_ HWND ownerWndHandle, _Out_ UINT* selectedCommand)
{
    CSettingsMenuTracker tracker(snapshot, ownerWndHandle);
    return tracker.Track(popupPoint, selectedCommand);
}
