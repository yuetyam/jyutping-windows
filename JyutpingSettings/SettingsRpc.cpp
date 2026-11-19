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

extern "C" __declspec(dllexport) DWORD JyutpingPreviewCandidates(uint32_t standard, wchar_t* text, uint32_t count)
{
    try
    {
        BackendEngineReply result;
        const DWORD status = client.SuggestCandidates(L"hoenggong", BackendLookupMode::Normal, standard, 1, 5, 1, 1, 1, result);
        if (status != ERROR_SUCCESS)
        {
            return status;
        }
        std::wstring preview;
        for (const auto& candidate : result.candidates)
        {
            if (candidate.kind == 0 && !candidate.comment.empty())
            {
                preview.append(candidate.text).push_back(L'\t');
                preview.append(candidate.comment).push_back(L'\n');
            }
        }
        if (preview.size() >= count)
        {
            return ERROR_INSUFFICIENT_BUFFER;
        }
        std::copy(preview.begin(), preview.end(), text);
        text[preview.size()] = L'\0';
        return ERROR_SUCCESS;
    }
    catch (const std::bad_alloc&)
    {
        return ERROR_NOT_ENOUGH_MEMORY;
    }
}
