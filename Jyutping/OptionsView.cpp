#include "Private.h"
#include "OptionsView.h"
#include "Globals.h"

#include <d2d1.h>
#include <dwrite.h>

#include <algorithm>

namespace
{
constexpr int OptionsPadding = 12;
constexpr int OptionsNumberWidth = 24;
constexpr int OptionsSeparatorHeight = 7;
constexpr int OptionsRowHeight = 30;

static D2D1_COLOR_F ColorRefToD2DColor(COLORREF color)
{
    return D2D1::ColorF(GetRValue(color) / 255.0f, GetGValue(color) / 255.0f, GetBValue(color) / 255.0f, 1.0f);
}
}

COptionsView::COptionsView() : _count(0), _selection(0), _hover(static_cast<UINT>(-1))
{
}

void COptionsView::SetRows(_In_reads_(count) const Row* rows, UINT count)
{
    _count = (std::min)(count, static_cast<UINT>(_countof(_rows)));
    for (UINT index = 0; index < _count; ++index)
    {
        _rows[index] = rows[index];
    }
    _selection = _count == 0 ? 0 : (std::min)(_selection, _count - 1);
}

void COptionsView::SetSelection(UINT index)
{
    if (index < _count)
    {
        _selection = index;
    }
}

void COptionsView::SetHover(UINT index)
{
    _hover = index < _count ? index : static_cast<UINT>(-1);
}

int COptionsView::RowHeight(UINT dpi) const
{
    return MulDiv(OptionsRowHeight, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
}

int COptionsView::Width(UINT dpi) const
{
    return MulDiv(260, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
}

int COptionsView::Height(UINT dpi) const
{
    int rowHeight = RowHeight(dpi);
    int height = 0;
    for (UINT index = 0; index < _count; ++index)
    {
        height += rowHeight;
        if (_rows[index].separatorBefore)
        {
            height += MulDiv(OptionsSeparatorHeight, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
        }
    }
    return height;
}

UINT COptionsView::RowFromPoint(POINT point, UINT dpi) const
{
    int rowHeight = RowHeight(dpi);
    if (point.x < 0 || point.y < 0 || rowHeight <= 0)
    {
        return static_cast<UINT>(-1);
    }
    int y = 0;
    int separatorHeight = MulDiv(OptionsSeparatorHeight, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
    for (UINT index = 0; index < _count; ++index)
    {
        if (_rows[index].separatorBefore)
        {
            y += separatorHeight;
        }
        if (point.y >= y && point.y < y + rowHeight)
        {
            return index;
        }
        y += rowHeight;
    }
    return static_cast<UINT>(-1);
}

void COptionsView::Draw(_In_ HDC dc, _In_ const RECT& clientRect, COLORREF textColor, COLORREF backgroundColor, UINT dpi, DWORD fontSize, DWORD numberFontSize) const
{
    if (dc == nullptr)
    {
        return;
    }
    HBRUSH backgroundBrush = CreateSolidBrush(backgroundColor);
    if (backgroundBrush)
    {
        FillRect(dc, &clientRect, backgroundBrush);
        DeleteObject(backgroundBrush);
    }
    SetBkMode(dc, TRANSPARENT);
    int rowHeight = RowHeight(dpi);
    int separatorHeight = MulDiv(OptionsSeparatorHeight, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
    int padding = MulDiv(OptionsPadding, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
    int numberWidth = MulDiv(OptionsNumberWidth, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
    int y = 0;
    int logicalFontHeight = -MulDiv(static_cast<int>(fontSize), static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
    HFONT font = CreateFontW(logicalFontHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    if (font == nullptr)
    {
        font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    }
    int logicalNumberFontHeight = -MulDiv(static_cast<int>(numberFontSize), static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
    HFONT numberFont = CreateFontW(logicalNumberFontHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    if (numberFont == nullptr)
    {
        numberFont = font;
    }
    HFONT oldFont = static_cast<HFONT>(SelectObject(dc, font));

    for (UINT index = 0; index < _count; ++index)
    {
        if (_rows[index].separatorBefore)
        {
            RECT separator = { clientRect.left, y, clientRect.right, y + separatorHeight };
            HBRUSH separatorBrush = CreateSolidBrush(GetSysColor(COLOR_3DLIGHT));
            if (separatorBrush)
            {
                FillRect(dc, &separator, separatorBrush);
                DeleteObject(separatorBrush);
            }
            y += separatorHeight;
        }
        RECT row = { clientRect.left, y, clientRect.right, y + rowHeight };
        if (index == _selection || index == _hover)
        {
            COLORREF accent = Global::GetHighlightedBackColor();
            HBRUSH rowBrush = CreateSolidBrush(accent);
            if (rowBrush)
            {
                FillRect(dc, &row, rowBrush);
                DeleteObject(rowBrush);
            }
        }
        COLORREF rowTextColor = (index == _selection || index == _hover) ? Global::GetHighlightedTextColor() : textColor;
        SetTextColor(dc, rowTextColor);
        WCHAR number[2] = { index == 9 ? L'0' : static_cast<WCHAR>(L'1' + index), L'\0' };
        RECT numberRect = { row.left + padding, row.top, row.left + padding + numberWidth, row.bottom };
        SelectObject(dc, numberFont);
        if (index != _selection && index != _hover)
        {
            SetTextColor(dc, Global::GetNumberLabelColor());
        }
        DrawTextW(dc, number, 1, &numberRect, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
        SelectObject(dc, font);
        SetTextColor(dc, rowTextColor);
        RECT labelRect = { numberRect.right, row.top, row.right - padding, row.bottom };
        DrawTextW(dc, _rows[index].label.c_str(), -1, &labelRect, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
        if (_rows[index].selected)
        {
            RECT checkRect = { row.right - padding - numberWidth, row.top, row.right - padding, row.bottom };
            DrawTextW(dc, L"\x2713", 1, &checkRect, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);
        }
        y += rowHeight;
    }
    SelectObject(dc, oldFont);
    if (font != GetStockObject(DEFAULT_GUI_FONT))
    {
        DeleteObject(font);
    }
    if (numberFont != font && numberFont != GetStockObject(DEFAULT_GUI_FONT))
    {
        DeleteObject(numberFont);
    }

    HPEN borderPen = CreatePen(PS_SOLID, 1, Global::GetCandidateWindowBorderColor());
    if (borderPen)
    {
        HPEN oldPen = static_cast<HPEN>(SelectObject(dc, borderPen));
        HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(dc, GetStockObject(NULL_BRUSH)));
        Rectangle(dc, clientRect.left, clientRect.top, clientRect.right, clientRect.bottom);
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(borderPen);
    }
}

void COptionsView::DrawD2D(_In_ ID2D1DCRenderTarget* renderTarget, _In_ IDWriteTextFormat* textFormat, _In_ IDWriteTextFormat* numberFormat, _In_ const RECT& paintRect, UINT dpi) const
{
    if (renderTarget == nullptr || textFormat == nullptr || numberFormat == nullptr || Global::pDWriteFactory == nullptr)
    {
        return;
    }

    const FLOAT rowHeight = static_cast<FLOAT>(RowHeight(dpi));
    const FLOAT separatorHeight = static_cast<FLOAT>(MulDiv(OptionsSeparatorHeight, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI));
    const FLOAT padding = static_cast<FLOAT>(MulDiv(OptionsPadding, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI));
    const FLOAT numberWidth = static_cast<FLOAT>(MulDiv(OptionsNumberWidth, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI));

    ComPtr<ID2D1SolidColorBrush> normalBrush;
    ComPtr<ID2D1SolidColorBrush> selectedBrush;
    ComPtr<ID2D1SolidColorBrush> selectedBackgroundBrush;
    renderTarget->CreateSolidColorBrush(ColorRefToD2DColor(Global::GetNormalTextColor()), &normalBrush);
    renderTarget->CreateSolidColorBrush(ColorRefToD2DColor(Global::GetHighlightedTextColor()), &selectedBrush);
    renderTarget->CreateSolidColorBrush(ColorRefToD2DColor(Global::GetHighlightedBackColor()), &selectedBackgroundBrush);
    if (!normalBrush || !selectedBrush || !selectedBackgroundBrush)
    {
        return;
    }

    FLOAT y = 0.0f;
    for (UINT index = 0; index < _count; ++index)
    {
        if (_rows[index].separatorBefore)
        {
            y += separatorHeight;
        }
        const bool highlighted = index == _selection || index == _hover;
        D2D1_RECT_F rowRect = D2D1::RectF(static_cast<FLOAT>(paintRect.left), y, static_cast<FLOAT>(paintRect.right), y + rowHeight);
        if (highlighted)
        {
            renderTarget->FillRectangle(rowRect, selectedBackgroundBrush.Get());
        }

        WCHAR number[2] = { index == 9 ? L'0' : static_cast<WCHAR>(L'1' + index), L'\0' };
        ComPtr<IDWriteTextLayout> numberLayout;
        ComPtr<IDWriteTextLayout> labelLayout;
        Global::pDWriteFactory->CreateTextLayout(number, 1, numberFormat, numberWidth, rowHeight, &numberLayout);
        Global::pDWriteFactory->CreateTextLayout(_rows[index].label.c_str(), static_cast<UINT32>(_rows[index].label.length()), textFormat,
            static_cast<FLOAT>(paintRect.right) - padding * 2.0f - numberWidth, rowHeight, &labelLayout);
        ID2D1Brush* textBrush = highlighted ? selectedBrush.Get() : normalBrush.Get();
        if (numberLayout)
        {
            renderTarget->DrawTextLayout(D2D1::Point2F(static_cast<FLOAT>(paintRect.left) + padding, y), numberLayout.Get(), textBrush, D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
        }
        if (labelLayout)
        {
            renderTarget->DrawTextLayout(D2D1::Point2F(static_cast<FLOAT>(paintRect.left) + padding + numberWidth, y), labelLayout.Get(), textBrush, D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
        }
        if (_rows[index].selected)
        {
            ComPtr<IDWriteTextLayout> checkLayout;
            Global::pDWriteFactory->CreateTextLayout(L"\x2713", 1, textFormat, numberWidth, rowHeight, &checkLayout);
            if (checkLayout)
            {
                renderTarget->DrawTextLayout(D2D1::Point2F(static_cast<FLOAT>(paintRect.right) - padding - numberWidth, y), checkLayout.Get(), textBrush, D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
            }
        }
        y += rowHeight;
    }
}
