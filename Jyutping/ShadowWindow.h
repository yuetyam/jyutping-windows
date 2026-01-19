#pragma once

#include "BaseWindow.h"

class CShadowWindow : public CBaseWindow
{
public:
    CShadowWindow(_In_ CBaseWindow *pWndOwner)
    {
        _pWndOwner = pWndOwner;
        _color = RGB(224, 224, 224);
    }
    virtual ~CShadowWindow()
    {
    }

    BOOL _Create(ATOM atom, DWORD dwExStyle, DWORD dwStyle, _In_opt_ CBaseWindow *pParent = nullptr, int wndWidth = 0, int wndHeight = 0);

    void _Show(BOOL isShowWnd);

    LRESULT CALLBACK _WindowProcCallback(_In_ HWND wndHandle, UINT uMsg, WPARAM wParam, LPARAM lParam);

    void _OnSettingChange();
    void _OnOwnerWndMoved(BOOL isResized);

private:
    BOOL _Initialize();
    void _InitSettings();
    void _AdjustWindowPos();
    void _InitShadow();

private:
    CBaseWindow* _pWndOwner;
    COLORREF _color;
};
