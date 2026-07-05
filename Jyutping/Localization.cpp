#include "Private.h"
#include "Localization.h"
#include "Globals.h"

namespace Localization
{
std::wstring LoadString(UINT resourceId)
{
    WCHAR buffer[256] = {'\0'};
    int length = LoadStringW(Global::dllInstanceHandle, resourceId, buffer, ARRAYSIZE(buffer));
    if (length <= 0)
    {
        return std::wstring();
    }
    return std::wstring(buffer, static_cast<size_t>(length));
}

std::wstring LoadStringOrFallback(UINT resourceId, PCWSTR fallback)
{
    std::wstring value = LoadString(resourceId);
    if (!value.empty())
    {
        return value;
    }
    return fallback ? std::wstring(fallback) : std::wstring();
}
}
