#include "Private.h"
#include "OptionsView.h"
#include "Globals.h"
#include "Settings.h"
#include "WindowAppearance.h"

#include <d2d1.h>
#include <dwrite.h>

#include <algorithm>

namespace
{
constexpr FLOAT limitedMaxSpace = 2000.0f;
constexpr int OptionsSeparatorHeight = 6;
constexpr wchar_t OptionsCheckmarkText[] = L"\x2713";

// Reuse the candidate-view HStack row layout constants (Define.h):
// LeftPadding | Number | Spacing | Label | Spacing | Checkmark | RightPadding
constexpr int OptionsLeftPadding = static_cast<int>(CANDIDATE_ROW_PADDING_LEFT);
constexpr int OptionsRightPadding = static_cast<int>(CANDIDATE_ROW_PADDING_LEFT);
constexpr int OptionsTextSpacing = static_cast<int>(CANDIDATE_NUMBER_SPACING);

static HFONT CreateOptionsFont(DWORD fontSize, UINT dpi)
{
    int logicalFontHeight = -MulDiv(static_cast<int>(fontSize), static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
    HFONT font = CreateFontW(logicalFontHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    if (font == nullptr)
    {
        font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    }
    return font;
}

// Derive the row height and view width from the measured number, label, and checkmark extents,
// mirroring how CCandidateWindow derives its row height and window width.
static void ComposeMeasuredLayout(FLOAT numberWidth, FLOAT maxLabelWidth, FLOAT checkmarkWidth, FLOAT contentHeight, UINT dpi,
    int& rowHeight, int& viewWidth)
{
    const FLOAT scale = static_cast<FLOAT>(dpi) / USER_DEFAULT_SCREEN_DPI;
    rowHeight = static_cast<int>(ceil(contentHeight + CANDIDATE_ROW_VERTICAL_SPACING * scale));
    // Reserve exactly what the draw paths consume: integer-scaled paddings and spacings plus the measured text widths.
    const int scaledLeftPadding = MulDiv(OptionsLeftPadding, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
    const int scaledRightPadding = MulDiv(OptionsRightPadding, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
    const int scaledTextSpacing = MulDiv(OptionsTextSpacing, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
    viewWidth = scaledLeftPadding + scaledRightPadding + scaledTextSpacing * 2
        + static_cast<int>(ceil(numberWidth + maxLabelWidth + checkmarkWidth));
}
}

COptionsView::COptionsView()
    : _count(0), _selection(0), _hover(static_cast<UINT>(-1)),
      _layoutDpi(USER_DEFAULT_SCREEN_DPI),
      _rowHeight(static_cast<int>(ceil(DefaultCandidateFontSize + CANDIDATE_ROW_VERTICAL_SPACING))),
      _viewWidth(0)
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
    return MulDiv(_rowHeight, static_cast<int>(dpi), static_cast<int>(_layoutDpi));
}

int COptionsView::Width(UINT dpi) const
{
    return MulDiv(_viewWidth, static_cast<int>(dpi), static_cast<int>(_layoutDpi));
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

void COptionsView::UpdateLayout(_In_opt_ IDWriteTextFormat* textFormat, _In_opt_ IDWriteTextFormat* numberFormat, DWORD fontSize, DWORD numberFontSize, UINT dpi)
{
    _layoutDpi = dpi;

    FLOAT numberWidth = 0.0f;
    FLOAT numberHeight = 0.0f;
    FLOAT checkmarkWidth = 0.0f;
    FLOAT checkmarkHeight = 0.0f;
    FLOAT maxLabelWidth = 0.0f;
    FLOAT maxLabelHeight = 0.0f;

    if (textFormat != nullptr && numberFormat != nullptr && Global::pDWriteFactory != nullptr)
    {
        DWRITE_TEXT_METRICS metrics = {};
        ComPtr<IDWriteTextLayout> numberLayout;
        if (SUCCEEDED(Global::pDWriteFactory->CreateTextLayout(L"0", 1, numberFormat, limitedMaxSpace, limitedMaxSpace, &numberLayout))
            && SUCCEEDED(numberLayout->GetMetrics(&metrics)))
        {
            numberWidth = metrics.width;
            numberHeight = metrics.height;
        }
        ComPtr<IDWriteTextLayout> checkmarkLayout;
        if (SUCCEEDED(Global::pDWriteFactory->CreateTextLayout(OptionsCheckmarkText, 1, textFormat, limitedMaxSpace, limitedMaxSpace, &checkmarkLayout))
            && SUCCEEDED(checkmarkLayout->GetMetrics(&metrics)))
        {
            checkmarkWidth = metrics.width;
            checkmarkHeight = metrics.height;
        }
        for (UINT index = 0; index < _count; ++index)
        {
            ComPtr<IDWriteTextLayout> labelLayout;
            if (SUCCEEDED(Global::pDWriteFactory->CreateTextLayout(_rows[index].label.c_str(), static_cast<UINT32>(_rows[index].label.length()), textFormat, limitedMaxSpace, limitedMaxSpace, &labelLayout))
                && SUCCEEDED(labelLayout->GetMetrics(&metrics)))
            {
                maxLabelWidth = (std::max)(maxLabelWidth, metrics.width);
                maxLabelHeight = (std::max)(maxLabelHeight, metrics.height);
            }
        }
    }
    else
    {
        // DirectWrite is unavailable; measure with the same GDI fonts the fallback Draw path uses.
        HDC screenDc = GetDC(nullptr);
        if (screenDc == nullptr)
        {
            return;
        }
        HFONT labelFont = CreateOptionsFont(fontSize, dpi);
        HFONT numberFont = CreateOptionsFont(numberFontSize, dpi);
        HFONT previousFont = static_cast<HFONT>(SelectObject(screenDc, numberFont));
        SIZE extent = {};
        if (GetTextExtentPoint32W(screenDc, L"0", 1, &extent))
        {
            numberWidth = static_cast<FLOAT>(extent.cx);
            numberHeight = static_cast<FLOAT>(extent.cy);
        }
        SelectObject(screenDc, labelFont);
        if (GetTextExtentPoint32W(screenDc, OptionsCheckmarkText, 1, &extent))
        {
            checkmarkWidth = static_cast<FLOAT>(extent.cx);
            checkmarkHeight = static_cast<FLOAT>(extent.cy);
        }
        for (UINT index = 0; index < _count; ++index)
        {
            if (GetTextExtentPoint32W(screenDc, _rows[index].label.c_str(), static_cast<int>(_rows[index].label.length()), &extent))
            {
                maxLabelWidth = (std::max)(maxLabelWidth, static_cast<FLOAT>(extent.cx));
                maxLabelHeight = (std::max)(maxLabelHeight, static_cast<FLOAT>(extent.cy));
            }
        }
        SelectObject(screenDc, previousFont);
        if (labelFont != static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)))
        {
            DeleteObject(labelFont);
        }
        if (numberFont != labelFont && numberFont != static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)))
        {
            DeleteObject(numberFont);
        }
        ReleaseDC(nullptr, screenDc);
    }

    const FLOAT contentHeight = (std::max)((std::max)(numberHeight, checkmarkHeight), maxLabelHeight);
    ComposeMeasuredLayout(numberWidth, maxLabelWidth, checkmarkWidth, contentHeight, dpi, _rowHeight, _viewWidth);
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
    int leftPadding = MulDiv(OptionsLeftPadding, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
    int rightPadding = MulDiv(OptionsRightPadding, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
    int numberSpacing = MulDiv(OptionsTextSpacing, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
    int textSpacing = MulDiv(OptionsTextSpacing, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
    int y = 0;
    HFONT font = CreateOptionsFont(fontSize, dpi);
    HFONT numberFont = CreateOptionsFont(numberFontSize, dpi);
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
        SelectObject(dc, numberFont);
        if (index != _selection && index != _hover)
        {
            SetTextColor(dc, Global::GetNumberLabelColor());
        }
        SIZE numberSize = { 0, 0 };
        GetTextExtentPoint32W(dc, number, 1, &numberSize);
        RECT numberRect = { row.left + leftPadding, row.top, row.left + leftPadding + numberSize.cx, row.bottom };
        DrawTextW(dc, number, 1, &numberRect, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
        SelectObject(dc, font);
        SetTextColor(dc, rowTextColor);
        SIZE checkSize = { 0, 0 };
        GetTextExtentPoint32W(dc, OptionsCheckmarkText, 1, &checkSize);
        RECT labelRect = { numberRect.left + numberSize.cx + numberSpacing, row.top,
            row.right - rightPadding - textSpacing - checkSize.cx, row.bottom };
        DrawTextW(dc, _rows[index].label.c_str(), -1, &labelRect, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
        if (_rows[index].selected)
        {
            RECT checkRect = { row.right - rightPadding - checkSize.cx, row.top, row.right - rightPadding, row.bottom };
            DrawTextW(dc, OptionsCheckmarkText, 1, &checkRect, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);
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

    if (!WindowAppearance::IsWindows11OrGreater())
    {
        HBRUSH borderBrush = CreateSolidBrush(Global::GetCandidateWindowBorderColor());
        if (borderBrush)
        {
            RECT borderRect = clientRect;
            borderRect.bottom = clientRect.top + CANDWND_BORDER_WIDTH;
            FillRect(dc, &borderRect, borderBrush);

            borderRect = clientRect;
            borderRect.top = clientRect.bottom - CANDWND_BORDER_WIDTH;
            FillRect(dc, &borderRect, borderBrush);

            borderRect = clientRect;
            borderRect.top += CANDWND_BORDER_WIDTH;
            borderRect.right = clientRect.left + CANDWND_BORDER_WIDTH;
            borderRect.bottom -= CANDWND_BORDER_WIDTH;
            FillRect(dc, &borderRect, borderBrush);

            borderRect = clientRect;
            borderRect.top += CANDWND_BORDER_WIDTH;
            borderRect.left = clientRect.right - CANDWND_BORDER_WIDTH;
            borderRect.bottom -= CANDWND_BORDER_WIDTH;
            FillRect(dc, &borderRect, borderBrush);

            DeleteObject(borderBrush);
        }
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
    const FLOAT leftPadding = static_cast<FLOAT>(MulDiv(OptionsLeftPadding, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI));
    const FLOAT rightPadding = static_cast<FLOAT>(MulDiv(OptionsRightPadding, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI));
    const FLOAT numberSpacing = static_cast<FLOAT>(MulDiv(OptionsTextSpacing, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI));
    const FLOAT textSpacing = static_cast<FLOAT>(MulDiv(OptionsTextSpacing, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI));

    ComPtr<ID2D1SolidColorBrush> normalBrush;
    ComPtr<ID2D1SolidColorBrush> selectedBrush;
    ComPtr<ID2D1SolidColorBrush> selectedBackgroundBrush;
    renderTarget->CreateSolidColorBrush(WindowAppearance::ColorFromColorRef(Global::GetNormalTextColor()), &normalBrush);
    renderTarget->CreateSolidColorBrush(WindowAppearance::ColorFromColorRef(Global::GetHighlightedTextColor()), &selectedBrush);
    renderTarget->CreateSolidColorBrush(WindowAppearance::ColorFromColorRef(Global::GetHighlightedBackColor()), &selectedBackgroundBrush);
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
        ComPtr<IDWriteTextLayout> checkLayout;
        ComPtr<IDWriteTextLayout> labelLayout;
        Global::pDWriteFactory->CreateTextLayout(number, 1, numberFormat, limitedMaxSpace, rowHeight, &numberLayout);
        Global::pDWriteFactory->CreateTextLayout(OptionsCheckmarkText, 1, textFormat, limitedMaxSpace, rowHeight, &checkLayout);
        FLOAT numberTextWidth = 0.0f;
        FLOAT checkTextWidth = 0.0f;
        DWRITE_TEXT_METRICS textMetrics = {};
        if (numberLayout && SUCCEEDED(numberLayout->GetMetrics(&textMetrics)))
        {
            numberTextWidth = textMetrics.width;
        }
        if (checkLayout && SUCCEEDED(checkLayout->GetMetrics(&textMetrics)))
        {
            checkTextWidth = textMetrics.width;
        }
        const FLOAT labelLeft = static_cast<FLOAT>(paintRect.left) + leftPadding + numberTextWidth + numberSpacing;
        const FLOAT labelWidth = static_cast<FLOAT>(paintRect.right) - rightPadding - textSpacing - checkTextWidth - labelLeft;
        Global::pDWriteFactory->CreateTextLayout(_rows[index].label.c_str(), static_cast<UINT32>(_rows[index].label.length()), textFormat,
            labelWidth, rowHeight, &labelLayout);
        if (labelLayout)
        {
            // Option rows are single-line, like the GDI path's DT_SINGLELINE; never wrap at word breaks.
            labelLayout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
        ID2D1Brush* textBrush = highlighted ? selectedBrush.Get() : normalBrush.Get();
        if (numberLayout)
        {
            renderTarget->DrawTextLayout(D2D1::Point2F(static_cast<FLOAT>(paintRect.left) + leftPadding, y), numberLayout.Get(), textBrush, D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
        }
        if (labelLayout)
        {
            renderTarget->DrawTextLayout(D2D1::Point2F(labelLeft, y), labelLayout.Get(), textBrush, D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
        }
        if (_rows[index].selected && checkLayout)
        {
            renderTarget->DrawTextLayout(D2D1::Point2F(static_cast<FLOAT>(paintRect.right) - rightPadding - checkTextWidth, y), checkLayout.Get(), textBrush, D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
        }
        y += rowHeight;
    }

    if (!WindowAppearance::IsWindows11OrGreater())
    {
        ComPtr<ID2D1SolidColorBrush> borderBrush;
        if (SUCCEEDED(renderTarget->CreateSolidColorBrush(WindowAppearance::ColorFromColorRef(Global::GetCandidateWindowBorderColor()), &borderBrush))
            && borderBrush)
        {
            const FLOAT left = static_cast<FLOAT>(paintRect.left);
            const FLOAT top = static_cast<FLOAT>(paintRect.top);
            const FLOAT right = static_cast<FLOAT>(paintRect.right);
            const FLOAT bottom = static_cast<FLOAT>(paintRect.bottom);
            constexpr FLOAT borderWidth = static_cast<FLOAT>(CANDWND_BORDER_WIDTH);
            renderTarget->FillRectangle(D2D1::RectF(left, top, right, top + borderWidth), borderBrush.Get());
            renderTarget->FillRectangle(D2D1::RectF(left, bottom - borderWidth, right, bottom), borderBrush.Get());
            renderTarget->FillRectangle(D2D1::RectF(left, top + borderWidth, left + borderWidth, bottom - borderWidth), borderBrush.Get());
            renderTarget->FillRectangle(D2D1::RectF(right - borderWidth, top + borderWidth, right, bottom - borderWidth), borderBrush.Get());
        }
    }
}
