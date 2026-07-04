#pragma once

#include "stdafx.h"

#include <string>
#include <string_view>

class ImeDatabase;

enum class CharacterStandard : int
{
    Preset = 1,
    Custom = 2,
    Inherited = 3,
    Etymology = 4,
    OpenCC = 5,
    HongKong = 6,
    Taiwan = 7,
    PRCGeneral = 8,
    AncientBooksPublishing = 9,
    Mutilated = 51
};

CharacterStandard CharacterStandardFromRawValue(int value);
bool IsMutilated(CharacterStandard standard);
bool IsTraditional(CharacterStandard standard);
bool IsNoOpCharacterStandard(CharacterStandard standard);

namespace Ime {

std::wstring ConvertText(const ImeDatabase& database, std::wstring_view text, CharacterStandard standard);
bool IsIdeographicCodePoint(uint32_t codePoint);

namespace CharacterStandardConverterDetail {

PCWSTR VariantTableName(CharacterStandard standard);

} // namespace CharacterStandardConverterDetail

} // namespace Ime
