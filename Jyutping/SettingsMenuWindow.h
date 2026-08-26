#pragma once

#include "SettingsMenuModel.h"

HRESULT TrackSettingsMenu(
    _In_ const SettingsMenu::Snapshot& snapshot,
    POINT popupPoint,
    _In_opt_ HWND ownerWndHandle,
    _Out_ UINT* selectedCommand);
