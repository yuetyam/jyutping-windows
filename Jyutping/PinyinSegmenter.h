#pragma once

#include "stdafx.h"
#include "ImeDatabase.h"
#include "ImeTypes.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace Ime {

struct PinyinSyllable
{
    int64_t code = 0;
    std::vector<VirtualInputKey> keys;
    std::wstring text;

    PinyinSyllable() = default;
    PinyinSyllable(int64_t inputCode, std::wstring inputText);
};

bool operator==(const PinyinSyllable& left, const PinyinSyllable& right);
bool operator!=(const PinyinSyllable& left, const PinyinSyllable& right);

using PinyinScheme = std::vector<PinyinSyllable>;
using PinyinSegmentation = std::vector<PinyinScheme>;

size_t PinyinSchemeLength(const PinyinScheme& scheme);
std::wstring PinyinSchemeText(const PinyinScheme& scheme);

class PinyinSegmenter
{
public:
    bool Prepare();
    bool Prepare(const ImeDatabase& database);
    bool IsPrepared() const;

    PinyinSegmentation Segment(const std::vector<VirtualInputKey>& keys) const;

private:
    const PinyinSyllable* Lookup(int64_t code) const;

    std::unordered_map<int64_t, PinyinSyllable> _syllables;
};

} // namespace Ime
