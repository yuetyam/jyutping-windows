#pragma once

#include "stdafx.h"
#include "ImeDatabase.h"
#include "ImeTypes.h"
#include "Segmenter.h"
#include "sal.h"

#include <optional>
#include <string_view>
#include <vector>

namespace Ime {

class InputEngine
{
public:
    bool Prepare();
    bool Prepare(_In_z_ PCWSTR databasePath);
    bool IsPrepared() const;

    std::vector<Lexicon> Suggest(std::wstring_view input, bool deepSearch = true) const;
    std::vector<Lexicon> Suggest(const std::vector<VirtualInputKey>& keys, bool deepSearch = true) const;
    Segmentation Segment(const std::vector<VirtualInputKey>& keys) const;

private:
    std::vector<Lexicon> Dispatch(const std::vector<VirtualInputKey>& keys, const Segmentation& segmentation, bool deepSearch) const;
    std::vector<Lexicon> Search(const std::vector<VirtualInputKey>& keys, const Segmentation& segmentation, std::optional<int> limit, bool deepSearch) const;
    std::vector<Lexicon> Query(size_t inputLength, const Segmentation& segmentation, std::optional<int> limit) const;
    std::vector<Lexicon> Perform(const Scheme& scheme, std::optional<int> limit) const;
    std::vector<Lexicon> ProcessSlices(const std::vector<VirtualInputKey>& keys, const std::wstring& text, std::optional<int> limit) const;
    std::vector<Lexicon> FilterApostropheSuggestions(const std::vector<VirtualInputKey>& keys, const std::vector<Lexicon>& lexicons) const;

    std::vector<Lexicon> AnchorsMatch(const std::vector<VirtualInputKey>& keys, std::optional<std::wstring> input = std::nullopt, std::optional<int> limit = std::nullopt) const;
    std::vector<Lexicon> SpellMatch(std::wstring_view text, std::wstring input, std::optional<std::wstring> mark = std::nullopt, std::optional<int> limit = std::nullopt) const;
    std::vector<Lexicon> StrictMatch(int64_t anchors, int64_t spell, std::wstring input, std::optional<std::wstring> mark = std::nullopt, std::optional<int> limit = std::nullopt) const;
    std::vector<Lexicon> ReverseLookupWord(const std::wstring& word, const std::wstring& input, std::optional<std::wstring> mark = std::nullopt) const;

    Lexicon Modify(const Lexicon& item, const std::vector<VirtualInputKey>& keys, const std::wstring& text, size_t inputLength) const;

    ImeDatabase _database;
    Segmenter _segmenter;
};

} // namespace Ime
