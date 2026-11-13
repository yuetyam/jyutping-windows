#include "Private.h"
#include "GlobalSettings.h"
#include "Compartment.h"
#include "Logger.h"

namespace {

// Global compartment for each setting, indexed by SettingKind. These GUIDs are shared by every IME build that
// supports live settings, so they must never change.
constexpr GUID GlobalSettingCompartmentGuids[SettingKindCount] = {
    // {CFFD4697-6A04-4D43-A090-0DF5E2C66D8A}
    { 0xcffd4697, 0x6a04, 0x4d43, { 0xa0, 0x90, 0x0d, 0xf5, 0xe2, 0xc6, 0x6d, 0x8a } },
    // {634EB373-4982-4CFE-8D6C-71E43DE317F5}
    { 0x634eb373, 0x4982, 0x4cfe, { 0x8d, 0x6c, 0x71, 0xe4, 0x3d, 0xe3, 0x17, 0xf5 } },
    // {55610432-C1F7-468C-8558-8F06319553CB}
    { 0x55610432, 0xc1f7, 0x468c, { 0x85, 0x58, 0x8f, 0x06, 0x31, 0x95, 0x53, 0xcb } },
    // {3F0338E8-16C2-4111-B1EC-53A5F29650BB}
    { 0x3f0338e8, 0x16c2, 0x4111, { 0xb1, 0xec, 0x53, 0xa5, 0xf2, 0x96, 0x50, 0xbb } },
    // {59210A07-D750-4A24-9226-92887FC59470}
    { 0x59210a07, 0xd750, 0x4a24, { 0x92, 0x26, 0x92, 0x88, 0x7f, 0xc5, 0x94, 0x70 } },
    // {7A775348-CDAA-42C5-9303-DBBB61841B29}
    { 0x7a775348, 0xcdaa, 0x42c5, { 0x93, 0x03, 0xdb, 0xbb, 0x61, 0x84, 0x1b, 0x29 } },
    // {D497DB45-CD97-4EBA-A2AF-24DCD24F07D2}
    { 0xd497db45, 0xcd97, 0x4eba, { 0xa2, 0xaf, 0x24, 0xdc, 0xd2, 0x4f, 0x07, 0xd2 } },
    // {C8F97B28-1A34-447F-9326-E61C649A2414}
    { 0xc8f97b28, 0x1a34, 0x447f, { 0x93, 0x26, 0xe6, 0x1c, 0x64, 0x9a, 0x24, 0x14 } },
};

} // namespace

CGlobalSettings::CGlobalSettings() :
    _pCompartmentMgr(nullptr),
    _tfClientId(TF_CLIENTID_NULL),
    _eventSinks(),
    _changeCallback(nullptr),
    _changeCallbackContext(nullptr)
{
}

CGlobalSettings::~CGlobalSettings()
{
    Uninitialize();
}

BOOL CGlobalSettings::Initialize(_In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId)
{
    Uninitialize();

    if (pThreadMgr == nullptr)
    {
        return FALSE;
    }

    HRESULT hr = pThreadMgr->GetGlobalCompartment(&_pCompartmentMgr);
    if (FAILED(hr) || _pCompartmentMgr == nullptr)
    {
        Global::Log(L"GlobalSettings: GetGlobalCompartment failed: hr=0x%08X", static_cast<unsigned int>(hr));
        _pCompartmentMgr = nullptr;
        return FALSE;
    }

    _tfClientId = tfClientId;
    return TRUE;
}

void CGlobalSettings::Advise(_In_ ChangeCallback callback, _In_ void *context)
{
    if (_pCompartmentMgr == nullptr)
    {
        return;
    }

    _changeCallback = callback;
    _changeCallbackContext = context;

    for (UINT index = 0; index < SettingKindCount; ++index)
    {
        if (_eventSinks[index] != nullptr)
        {
            continue;
        }

        CCompartmentEventSink* pEventSink = new (std::nothrow) CCompartmentEventSink(CompartmentCallback, this);
        if (pEventSink == nullptr)
        {
            continue;
        }

        HRESULT hr = pEventSink->_Advise(_pCompartmentMgr, GlobalSettingCompartmentGuids[index]);
        if (FAILED(hr))
        {
            // _Unadvise is only valid after a successful _Advise.
            Global::Log(L"GlobalSettings: advise failed: index=%u hr=0x%08X", index, static_cast<unsigned int>(hr));
            pEventSink->Release();
            continue;
        }

        _eventSinks[index] = pEventSink;
    }
}

void CGlobalSettings::Uninitialize()
{
    for (CCompartmentEventSink*& pEventSink : _eventSinks)
    {
        if (pEventSink != nullptr)
        {
            pEventSink->_Unadvise();
            pEventSink->Release();
            pEventSink = nullptr;
        }
    }

    _changeCallback = nullptr;
    _changeCallbackContext = nullptr;

    if (_pCompartmentMgr != nullptr)
    {
        _pCompartmentMgr->Release();
        _pCompartmentMgr = nullptr;
    }
    _tfClientId = TF_CLIENTID_NULL;
}

bool CGlobalSettings::Read(SettingKind kind, _Out_ DWORD& value) const
{
    value = 0;

    UINT index = static_cast<UINT>(kind);
    if (_pCompartmentMgr == nullptr || index >= SettingKindCount)
    {
        return false;
    }

    CCompartment compartment(_pCompartmentMgr, _tfClientId, GlobalSettingCompartmentGuids[index]);
    // S_FALSE means the compartment is still empty.
    return compartment._GetCompartmentDWORD(value) == S_OK;
}

bool CGlobalSettings::Publish(SettingKind kind, DWORD value)
{
    UINT index = static_cast<UINT>(kind);
    if (_pCompartmentMgr == nullptr || index >= SettingKindCount)
    {
        return false;
    }

    // Skip unchanged values, since every write wakes up the IME in every client process.
    DWORD sharedValue = 0;
    if (Read(kind, sharedValue) && sharedValue == value)
    {
        return true;
    }

    CCompartment compartment(_pCompartmentMgr, _tfClientId, GlobalSettingCompartmentGuids[index]);
    HRESULT hr = compartment._SetCompartmentDWORD(value);
    if (FAILED(hr))
    {
        Global::Log(L"GlobalSettings: publish failed: index=%u hr=0x%08X", index, static_cast<unsigned int>(hr));
        return false;
    }
    return true;
}

// static
HRESULT CGlobalSettings::CompartmentCallback(_In_ void *pv, REFGUID guidCompartment)
{
    CGlobalSettings* pGlobalSettings = static_cast<CGlobalSettings*>(pv);
    if (pGlobalSettings == nullptr || pGlobalSettings->_changeCallback == nullptr)
    {
        return S_OK;
    }

    for (UINT index = 0; index < SettingKindCount; ++index)
    {
        if (IsEqualGUID(guidCompartment, GlobalSettingCompartmentGuids[index]))
        {
            pGlobalSettings->_changeCallback(pGlobalSettings->_changeCallbackContext, static_cast<SettingKind>(index));
            break;
        }
    }
    return S_OK;
}
