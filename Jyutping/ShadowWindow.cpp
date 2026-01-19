#include "Private.h"
#include "Globals.h"
#include "BaseWindow.h"
#include "ShadowWindow.h"

#define SHADOW_ALPHANUMBER (2)

//+---------------------------------------------------------------------------
//
// _Create
//
//----------------------------------------------------------------------------

BOOL CShadowWindow::_Create(ATOM atom, DWORD dwExStyle, DWORD dwStyle, _In_opt_ CBaseWindow* pParent, int wndWidth, int wndHeight)
{
    if (!CBaseWindow::_Create(atom, dwExStyle, dwStyle, pParent, wndWidth, wndHeight))
    {
        return FALSE;
    }

    return _Initialize();
}

//+---------------------------------------------------------------------------
//
// _WindowProcCallback
//
// Shadow window proc.
//----------------------------------------------------------------------------

LRESULT CALLBACK CShadowWindow::_WindowProcCallback(_In_ HWND wndHandle, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_PAINT:
    {
        HDC dcHandle;
        PAINTSTRUCT ps;

        dcHandle = BeginPaint(wndHandle, &ps);

        HBRUSH hBrush = CreateSolidBrush(_color);
        FillRect(dcHandle, &ps.rcPaint, hBrush);
        DeleteObject(hBrush);

        EndPaint(wndHandle, &ps);
    }
    return 0;

    case WM_SETTINGCHANGE:
        _OnSettingChange();
        break;
    }

    return DefWindowProc(wndHandle, uMsg, wParam, lParam);
}

//+---------------------------------------------------------------------------
//
// _OnSettingChange
//
//----------------------------------------------------------------------------

void CShadowWindow::_OnSettingChange()
{
    _InitSettings();

    DWORD dwWndStyleEx = GetWindowLong(_GetWnd(), GWL_EXSTYLE);

    SetWindowLong(_GetWnd(), GWL_EXSTYLE, (dwWndStyleEx & ~WS_EX_LAYERED));

    _AdjustWindowPos();
    _InitShadow();
}

//+---------------------------------------------------------------------------
//
// _OnOwnerWndMoved
//
//----------------------------------------------------------------------------

void CShadowWindow::_OnOwnerWndMoved(BOOL isResized)
{
    if (IsWindow(_GetWnd()) && _IsWindowVisible())
    {
        _AdjustWindowPos();
        if (isResized)
        {
            _InitShadow();
        }
    }
}


//+---------------------------------------------------------------------------
//
// _Show
//
//----------------------------------------------------------------------------

void CShadowWindow::_Show(BOOL isShowWnd)
{
    _OnOwnerWndMoved(TRUE);
    CBaseWindow::_Show(isShowWnd);
}

//+---------------------------------------------------------------------------
//
// _Initialize
//
//----------------------------------------------------------------------------

BOOL CShadowWindow::_Initialize()
{
    _InitSettings();

    return TRUE;
}

//+---------------------------------------------------------------------------
//
// _InitSettings
//
//----------------------------------------------------------------------------

void CShadowWindow::_InitSettings()
{
    HDC dcHandle = GetDC(nullptr);

    // device caps
    int cBitsPixelScreen = GetDeviceCaps(dcHandle, BITSPIXEL);

    ReleaseDC(nullptr, dcHandle);

    _color = RGB(224, 224, 224);
}

//+---------------------------------------------------------------------------
//
// _AdjustWindowPos
//
//----------------------------------------------------------------------------

void CShadowWindow::_AdjustWindowPos()
{
    if (!IsWindow(_GetWnd()))
    {
        return;
    }

    HWND hWndOwner = _pWndOwner->_GetWnd();
    RECT rc = { 0, 0, 0, 0 };

    GetWindowRect(hWndOwner, &rc);
    // Modern shadow: centered on all edges
    SetWindowPos(_GetWnd(), hWndOwner,
        rc.left - SHADOW_ALPHANUMBER,
        rc.top - SHADOW_ALPHANUMBER,
        (rc.right - rc.left) + SHADOW_ALPHANUMBER * 2,
        (rc.bottom - rc.top) + SHADOW_ALPHANUMBER * 2,
        SWP_NOOWNERZORDER | SWP_NOACTIVATE);
}

//+---------------------------------------------------------------------------
//
// _InitShadow
//
//----------------------------------------------------------------------------

#define GETRGBALPHA(_x_, _y_) ((RGBALPHA *)pDIBits + (_y_) * size.cx + (_x_))

void CShadowWindow::_InitShadow()
{
    typedef struct _RGBAPLHA {
        BYTE rgbBlue;
        BYTE rgbGreen;
        BYTE rgbRed;
        BYTE rgbAlpha;
    } RGBALPHA;

    HDC dcScreenHandle = nullptr;
    HDC dcLayeredHandle = nullptr;
    RECT rcWindow = { 0, 0, 0, 0 };
    SIZE size = { 0, 0 };
    BITMAPINFO bitmapInfo;
    HBITMAP bitmapMemHandle = nullptr;
    HBITMAP bitmapOldHandle = nullptr;
    void* pDIBits = nullptr;
    POINT ptSrc = { 0, 0 };
    POINT ptDst = { 0, 0 };
    BLENDFUNCTION Blend;

    SetWindowLong(_GetWnd(), GWL_EXSTYLE, (GetWindowLong(_GetWnd(), GWL_EXSTYLE) | WS_EX_LAYERED));

    _GetWindowRect(&rcWindow);
    size.cx = rcWindow.right - rcWindow.left;
    size.cy = rcWindow.bottom - rcWindow.top;

    if (size.cx <= 0 || size.cy <= 0) return;

    dcScreenHandle = GetDC(nullptr);
    if (dcScreenHandle == nullptr) {
        return;
    }

    dcLayeredHandle = CreateCompatibleDC(dcScreenHandle);
    if (dcLayeredHandle == nullptr) {
        ReleaseDC(nullptr, dcScreenHandle);
        return;
    }

    // create bitmap
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = size.cx;
    bitmapInfo.bmiHeader.biHeight = size.cy;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;
    bitmapInfo.bmiHeader.biSizeImage = 0;
    bitmapInfo.bmiHeader.biXPelsPerMeter = 100;
    bitmapInfo.bmiHeader.biYPelsPerMeter = 100;
    bitmapInfo.bmiHeader.biClrUsed = 0;
    bitmapInfo.bmiHeader.biClrImportant = 0;

    bitmapMemHandle = CreateDIBSection(dcScreenHandle, &bitmapInfo, DIB_RGB_COLORS, &pDIBits, nullptr, 0);
    if (pDIBits == nullptr || bitmapMemHandle == nullptr) {
        ReleaseDC(nullptr, dcScreenHandle);
        DeleteDC(dcLayeredHandle);
        if (bitmapMemHandle) DeleteObject(bitmapMemHandle);
        return;
    }

    memset(pDIBits, 0, size.cx * size.cy * 4);

    UINT dpi = GetDpiForWindow(_GetWnd());
    const float scale = (float)dpi / USER_DEFAULT_SCREEN_DPI;
    const float radius = 8 * scale;

    const float cx = size.cx / 2.0f;
    const float cy = size.cy / 2.0f;
    const float inner_w_2 = (float)(size.cx - 2 * SHADOW_ALPHANUMBER) / 2.0f;
    const float inner_h_2 = (float)(size.cy - 2 * SHADOW_ALPHANUMBER) / 2.0f;
    const float r = (float)radius;

    // Compute shadow alpha with robust rounded-rect SDF
    for (int y = 0; y < size.cy; y++) {
        for (int x = 0; x < size.cx; x++) {
            // Distance from center
            float px = abs((float)x - cx);
            float py = abs((float)y - cy);

            // Distance to the corner-rect (the rect subtracted by radius)
            float dx = px - (inner_w_2 - r);
            float dy = py - (inner_h_2 - r);

            float dist = 0;
            if (dx > 0 && dy > 0) {
                // Outside the inner corner-rect
                dist = sqrt(dx * dx + dy * dy) - r;
            } else {
                // Inside or aligned with one edge
                dist = max(dx, dy) - r;
            }

            // dist is now 0 on the rounded border, negative inside, positive outside
            BYTE alpha = 0;
            /*
            if (dist <= 0) {
                alpha = 60; // Increased base alpha for better visibility
            } else if (dist < SHADOW_ALPHANUMBER) {
                float ratio = 1.0f - (dist / SHADOW_ALPHANUMBER);
                // Smooth cubic falloff
                alpha = (BYTE)(60.0f * ratio * ratio * ratio);
            }
            */

            RGBALPHA* ppxl = GETRGBALPHA(x, y);
            ppxl->rgbAlpha = alpha;
        }
    }

    ptSrc.x = 0;
    ptSrc.y = 0;
    ptDst.x = rcWindow.left;
    ptDst.y = rcWindow.top;
    Blend.BlendOp = AC_SRC_OVER;
    Blend.BlendFlags = 0;
    Blend.SourceConstantAlpha = 255;
    Blend.AlphaFormat = AC_SRC_ALPHA;

    bitmapOldHandle = (HBITMAP)SelectObject(dcLayeredHandle, bitmapMemHandle);

    UpdateLayeredWindow(_GetWnd(), dcScreenHandle, nullptr, &size, dcLayeredHandle, &ptSrc, 0, &Blend, ULW_ALPHA);

    SelectObject(dcLayeredHandle, bitmapOldHandle);

    ReleaseDC(nullptr, dcScreenHandle);
    DeleteDC(dcLayeredHandle);
    DeleteObject(bitmapMemHandle);
}
