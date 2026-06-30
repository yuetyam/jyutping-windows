#pragma once

#include "stdafx.h"
#include "ImeDatabase.h"
#include "ImeTypes.h"

#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Ime {

class Segmenter
{
public:
    bool Prepare();
    bool Prepare(const ImeDatabase& database);
    bool IsPrepared() const;

    Segmentation Segment(const std::vector<VirtualInputKey>& keys) const;
    std::optional<std::wstring> SyllableText(const std::vector<VirtualInputKey>& keys) const;

private:
    const Syllable* Lookup(int64_t code) const;

    std::unordered_map<int64_t, Syllable> _syllables;
    std::unordered_set<int64_t> _prefixes;
};

} // namespace Ime
