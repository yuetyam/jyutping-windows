#include "Private.h"
#include "Globals.h"
#include "WindowAppearance.h"

#include <dwmapi.h>

#pragma comment(lib, "dwmapi.lib")

namespace WindowAppearance {

namespace {

constexpr DWORD Windows11MinimumBuildNumber = 22000;

enum AccentState
{
    AccentDisabled = 0,
    AccentEnableAcrylicBlurBehind = 4
};

struct AccentPolicy
{
    AccentState state;
    DWORD flags;
    DWORD gradientColor;
    DWORD animationId;
};

struct WindowCompositionAttributeData
{
    DWORD attribute;
    PVOID data;
    DWORD dataSize;
};

using SetWindowCompositionAttributeFunction = BOOL(WINAPI*)(HWND, WindowCompositionAttributeData*);

DWORD AccentGradientColor(COLORREF color, BYTE alpha)
{
    return (static_cast<DWORD>(alpha) << 24) | (static_cast<DWORD>(GetBValue(color)) << 16) |
        (static_cast<DWORD>(GetGValue(color)) << 8) | static_cast<DWORD>(GetRValue(color));
}

BOOL IsTransparencyEnabled()
{
    DWORD value = 1;
    DWORD size = sizeof(value);
    LONG result = RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"EnableTransparency", RRF_RT_REG_DWORD, nullptr, &value, &size);
    return result != ERROR_SUCCESS || value != 0;
}

}

D2D1_COLOR_F ColorFromColorRef(COLORREF color, FLOAT alpha)
{
    return D2D1::ColorF(GetRValue(color) / 255.0f, GetGValue(color) / 255.0f, GetBValue(color) / 255.0f, alpha);
}

BOOL IsWindows11OrGreater()
{
    static const BOOL value = []() -> BOOL
    {
        using RtlGetVersionFunction = LONG(WINAPI*)(_Out_ PRTL_OSVERSIONINFOW);
        HMODULE module = GetModuleHandleW(L"ntdll.dll");
        RtlGetVersionFunction getVersion = module == nullptr ? nullptr : reinterpret_cast<RtlGetVersionFunction>(GetProcAddress(module, "RtlGetVersion"));
        if (getVersion == nullptr)
        {
            return FALSE;
        }
        RTL_OSVERSIONINFOW version = {};
        version.dwOSVersionInfoSize = sizeof(version);
        return getVersion(&version) == 0 && (version.dwMajorVersion > 10 || (version.dwMajorVersion == 10 && version.dwBuildNumber >= Windows11MinimumBuildNumber));
    }();

    // return FALSE; // For testing window border on Windows 11
    return value;
}

BOOL IsHighContrastEnabled()
{
    HIGHCONTRASTW highContrast = {};
    highContrast.cbSize = sizeof(highContrast);
    return SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(highContrast), &highContrast, 0) && (highContrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
}

BOOL ApplyAcrylic(_In_ HWND wndHandle, COLORREF color, BYTE alpha)
{
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    SetWindowCompositionAttributeFunction setAttribute = user32 == nullptr ? nullptr : reinterpret_cast<SetWindowCompositionAttributeFunction>(GetProcAddress(user32, "SetWindowCompositionAttribute"));
    if (wndHandle == nullptr || setAttribute == nullptr)
    {
        return FALSE;
    }
    BOOL compositionEnabled = FALSE;
    BOOL canUseAcrylic = !IsHighContrastEnabled() && IsTransparencyEnabled() && SUCCEEDED(DwmIsCompositionEnabled(&compositionEnabled)) && compositionEnabled;
    AccentPolicy policy = { canUseAcrylic ? AccentEnableAcrylicBlurBehind : AccentDisabled, 0, canUseAcrylic ? AccentGradientColor(color, alpha) : 0, 0 };
    WindowCompositionAttributeData data = { 19, &policy, sizeof(policy) };
    return setAttribute(wndHandle, &data) && canUseAcrylic;
}

void ApplyRoundedCorners(_In_ HWND wndHandle, BOOL smallCorners)
{
    if (!IsWindows11OrGreater() || wndHandle == nullptr)
    {
        return;
    }
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
    DWORD preference = smallCorners ? 3 : 2;
    DwmSetWindowAttribute(wndHandle, DWMWA_WINDOW_CORNER_PREFERENCE, &preference, sizeof(preference));
}

HRESULT CreateTextFormat(FLOAT fontSize, _In_reads_(fontNamesCount) const LPCWSTR* fontNames, size_t fontNamesCount, _In_opt_ IDWriteFontFallback* fontFallback, _COM_Outptr_ IDWriteTextFormat1** textFormat)
{
    if (Global::pDWriteFactory == nullptr || textFormat == nullptr || fontNames == nullptr || fontNamesCount == 0)
    {
        return E_INVALIDARG;
    }
    *textFormat = nullptr;
    ComPtr<IDWriteTextFormat> baseFormat;
    HRESULT hr = Global::pDWriteFactory->CreateTextFormat(fontNames[0], nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, fontSize, L"", &baseFormat);
    if (FAILED(hr))
    {
        return hr;
    }
    ComPtr<IDWriteTextFormat1> format;
    hr = baseFormat.As(&format);
    if (FAILED(hr))
    {
        return hr;
    }
    if (fontFallback != nullptr)
    {
        format->SetFontFallback(fontFallback);
    }
    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    *textFormat = format.Detach();
    return S_OK;
}

}
