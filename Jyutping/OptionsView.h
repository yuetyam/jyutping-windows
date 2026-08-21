#pragma once

#include <windows.h>
#include <string>

struct ID2D1DCRenderTarget;
struct IDWriteTextFormat;

class COptionsView
{
public:
    enum RowId : UINT
    {
        CharacterTraditional,
        CharacterHongKong,
        CharacterTaiwan,
        CharacterSimplified,
        CharacterHalfWidth,
        CharacterFullWidth,
        PunctuationCantonese,
        PunctuationEnglish,
        InputCantonese,
        InputABC
    };

    struct Row
    {
        UINT id;
        std::wstring label;
        BOOL selected;
        BOOL separatorBefore;
    };

    COptionsView();
    void SetRows(_In_reads_(count) const Row* rows, UINT count);
    void SetSelection(UINT index);
    void SetHover(UINT index);
    UINT Selection() const { return _selection; }
    UINT RowFromPoint(POINT point, UINT dpi) const;
    int RowHeight(UINT dpi) const;
    int Width(UINT dpi) const;
    int Height(UINT dpi) const;
    void Draw(_In_ HDC dc, _In_ const RECT& clientRect, COLORREF textColor, COLORREF backgroundColor, UINT dpi, DWORD fontSize, DWORD numberFontSize) const;
    void DrawD2D(_In_ ID2D1DCRenderTarget* renderTarget, _In_ IDWriteTextFormat* textFormat, _In_ IDWriteTextFormat* numberFormat, _In_ const RECT& paintRect, UINT dpi) const;

private:
    Row _rows[10];
    UINT _count;
    UINT _selection;
    UINT _hover;
};
