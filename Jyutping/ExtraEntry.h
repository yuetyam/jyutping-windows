#pragma once

#include "ImeTypes.h"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace Ime {

struct ExtraEntry
{
    std::wstring_view word;
    std::wstring_view romanization;
    size_t complex;
    int64_t spell;

    static std::vector<Lexicon> Search(const std::vector<VirtualInputKey>& keys);
};

} // namespace Ime
