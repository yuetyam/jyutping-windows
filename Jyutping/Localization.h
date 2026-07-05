#pragma once

#include <string>

namespace Localization
{
std::wstring LoadString(UINT resourceId);
std::wstring LoadStringOrFallback(UINT resourceId, PCWSTR fallback);
}
