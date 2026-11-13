#pragma once

#include "Settings.h"

class CCompartmentEventSink;

// Shares the live IME settings between every process of the desktop through TSF global compartments.
//
// Each setting has its own global compartment holding a VT_I4 value. A compartment stays empty until the first IME
// instance that can read the real settings publishes them, and keeps its value for the rest of the session, even after
// every publishing process exits. Global compartments notify all advised sinks across processes, including packaged
// and AppContainer apps, so a change made in one client app reaches every other client app immediately.
class CGlobalSettings
{
public:
    using ChangeCallback = void (*)(_In_ void *context, SettingKind kind);

    CGlobalSettings();
    ~CGlobalSettings();

    CGlobalSettings(const CGlobalSettings&) = delete;
    CGlobalSettings& operator=(const CGlobalSettings&) = delete;

    BOOL Initialize(_In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId);
    // Starts receiving change notifications. The callback also fires for values this instance publishes itself.
    void Advise(_In_ ChangeCallback callback, _In_ void *context);
    void Uninitialize();

    // Returns false while no IME instance has published the setting yet.
    bool Read(SettingKind kind, _Out_ DWORD& value) const;
    // Publishes the value when it differs from the shared one, which notifies every IME instance of the desktop.
    bool Publish(SettingKind kind, DWORD value);

private:
    static HRESULT CompartmentCallback(_In_ void *pv, REFGUID guidCompartment);

    ITfCompartmentMgr* _pCompartmentMgr;
    TfClientId _tfClientId;
    CCompartmentEventSink* _eventSinks[SettingKindCount];
    ChangeCallback _changeCallback;
    void* _changeCallbackContext;
};
