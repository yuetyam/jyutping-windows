#pragma once

namespace WindowAppearance {

D2D1_COLOR_F ColorFromColorRef(COLORREF color, FLOAT alpha = 1.0f);
BOOL IsWindows11OrGreater();
BOOL IsHighContrastEnabled();
BOOL ApplyAcrylic(_In_ HWND wndHandle, COLORREF color, BYTE alpha);
void ApplyRoundedCorners(_In_ HWND wndHandle, BOOL smallCorners = FALSE);
HRESULT CreateTextFormat(
    FLOAT fontSize,
    _In_reads_(fontNamesCount) const LPCWSTR* fontNames,
    size_t fontNamesCount,
    _In_opt_ IDWriteFontFallback* fontFallback,
    _COM_Outptr_ IDWriteTextFormat1** textFormat);

}
