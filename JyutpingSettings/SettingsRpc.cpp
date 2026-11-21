#include "../Jyutping/BackendRpcClient.h"
#include <algorithm>

namespace {
BackendRpcClient client;
BackendSettingsSnapshot snapshot;
}

extern "C" __declspec(dllexport) DWORD JyutpingReadSettings(uint32_t* values, uint32_t count)
{
    if (count != snapshot.values.size())
    {
        return ERROR_INVALID_PARAMETER;
    }
    try
    {
        const DWORD status = client.GetSettings(snapshot);
        if (status == ERROR_SUCCESS)
        {
            std::copy(snapshot.values.begin(), snapshot.values.end(), values);
        }
        return status;
    }
    catch (const std::bad_alloc&)
    {
        return ERROR_NOT_ENOUGH_MEMORY;
    }
}

extern "C" __declspec(dllexport) DWORD JyutpingWriteSetting(uint32_t field, uint32_t value)
{
    return client.SetSetting(field, value, snapshot.instance);
}

extern "C" __declspec(dllexport) BOOL JyutpingHasInputMemory()
{
    return client.HasInputMemory();
}

extern "C" __declspec(dllexport) BOOL JyutpingHasMemoryTransfer()
{
    return client.HasMemoryTransfer();
}

extern "C" __declspec(dllexport) DWORD JyutpingClearInputMemory()
{
    return client.MutateMemory(BackendMemoryOperation::Clear, {}, {}, 1, 1, 1, snapshot.instance, 0);
}
