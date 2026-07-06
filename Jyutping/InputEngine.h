#pragma once

#include "stdafx.h"
#include "ImeDatabase.h"
#include "InputMemory.h"
#include "ImeTypes.h"
#include "PinyinSegmenter.h"
#include "Segmenter.h"
#include "sal.h"

#include <optional>
#include <string_view>
#include <vector>

enum class CharacterStandard : int;

namespace Ime {

enum class ReverseLookupMethod
{
    None,
    Pinyin,
    Cangjie,
    Stroke,
    Structure
};

class InputEngine
{
public:
    bool Prepare();
    bool Prepare(_In_z_ PCWSTR databasePath);
    bool IsPrepared() const;

    std::vector<Lexicon> Suggest(std::wstring_view input) const;
    std::vector<Lexicon> Suggest(const std::vector<VirtualInputKey>& keys) const;
    std::vector<Lexicon> SearchTextMarks(std::wstring_view input) const;
    std::vector<Lexicon> SearchTextMarks(const std::vector<VirtualInputKey>& keys) const;
    std::vector<Lexicon> SearchSymbols(const std::vector<VirtualInputKey>& keys, const Segmentation& segmentation) const;
    std::vector<Lexicon> ReverseLookup(ReverseLookupMethod method, std::wstring_view input) const;
    std::vector<Lexicon> ReverseLookup(ReverseLookupMethod method, const std::vector<VirtualInputKey>& keys) const;
    std::vector<Lexicon> CangjieReverseLookup(std::wstring_view input, CangjieVariant variant = CangjieVariant::Cangjie5) const;
    std::vector<Lexicon> CangjieReverseLookup(const std::vector<VirtualInputKey>& keys, CangjieVariant variant = CangjieVariant::Cangjie5) const;
    Segmentation Segment(const std::vector<VirtualInputKey>& keys) const;
    std::wstring ConvertText(std::wstring_view text, CharacterStandard standard) const;
    bool Remember(const Lexicon& lexicon);
    bool Forget(const Lexicon& lexicon);
    bool DeleteAllMemory();

private:
    std::vector<Lexicon> SuggestFromLexicon(const std::vector<VirtualInputKey>& keys) const;
    std::vector<Lexicon> Dispatch(const std::vector<VirtualInputKey>& keys, const Segmentation& segmentation) const;
    std::vector<Lexicon> Search(const std::vector<VirtualInputKey>& keys, const Segmentation& segmentation, std::optional<int> limit) const;
    std::vector<Lexicon> Query(size_t inputLength, const Segmentation& segmentation, std::optional<int> limit) const;
    std::vector<Lexicon> Perform(const Scheme& scheme, std::optional<int> limit) const;
    std::vector<Lexicon> ProcessSlices(const std::vector<VirtualInputKey>& keys, const std::wstring& text, std::optional<int> limit) const;
    std::vector<Lexicon> FilterApostropheSuggestions(const std::vector<VirtualInputKey>& keys, const std::vector<Lexicon>& lexicons) const;

    std::vector<Lexicon> AnchorsMatch(const std::vector<VirtualInputKey>& keys, std::optional<std::wstring> input = std::nullopt, std::optional<int> limit = std::nullopt) const;
    std::vector<Lexicon> SpellMatch(std::wstring_view text, std::wstring input, std::optional<std::wstring> mark = std::nullopt, std::optional<int> limit = std::nullopt) const;
    std::vector<Lexicon> StrictMatch(int64_t anchors, int64_t spell, std::wstring input, std::optional<std::wstring> mark = std::nullopt, std::optional<int> limit = std::nullopt) const;
    std::vector<Lexicon> ReverseLookupWord(const std::wstring& word, const std::wstring& input, std::optional<std::wstring> mark = std::nullopt) const;
    std::vector<Lexicon> PinyinReverseLookup(const std::vector<VirtualInputKey>& keys) const;
    std::vector<Lexicon> StrokeReverseLookup(const std::vector<VirtualInputKey>& keys) const;
    std::vector<Lexicon> StructureReverseLookup(const std::vector<VirtualInputKey>& keys) const;

    Lexicon Modify(const Lexicon& item, const std::vector<VirtualInputKey>& keys, const std::wstring& text, size_t inputLength) const;

    ImeDatabase _database;
    InputMemory _inputMemory;
    Segmenter _segmenter;
    PinyinSegmenter _pinyinSegmenter;
};

} // namespace Ime
