#pragma once

#include "stdafx.h"
#include "ImeDatabase.h"
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

struct Emoji
{
    int category = 0;
    int64_t uniqueNumber = 0;
    int unicodeVersion = 0;
    std::wstring text;
    std::wstring cantonese;
    std::wstring romanization;
};

class CoreImeEngine
{
public:
    bool Prepare();
    bool Prepare(_In_z_ PCWSTR databasePath);
    bool IsPrepared() const;

    std::vector<Lexicon> Suggest(const std::vector<VirtualInputKey>& keys, bool deepSearch = true) const;
    std::vector<Lexicon> Suggest(
        const std::vector<VirtualInputKey>& keys,
        const Segmentation& segmentation,
        bool deepSearch = true) const;
    std::vector<Lexicon> SearchPlainTexts(std::wstring_view input) const;
    std::vector<Lexicon> SearchPlainTexts(const std::vector<VirtualInputKey>& keys) const;
    std::vector<Lexicon> SearchSymbols(const std::vector<VirtualInputKey>& keys, const Segmentation& segmentation) const;
    std::vector<Lexicon> ReverseLookup(ReverseLookupMethod method, std::wstring_view input) const;
    std::vector<Lexicon> ReverseLookup(ReverseLookupMethod method, const std::vector<VirtualInputKey>& keys) const;
    std::vector<Lexicon> CangjieReverseLookup(
        std::wstring_view input,
        CangjieVariant variant = CangjieVariant::Cangjie5) const;
    std::vector<Lexicon> CangjieReverseLookup(
        const std::vector<VirtualInputKey>& keys,
        CangjieVariant variant = CangjieVariant::Cangjie5) const;
    Segmentation Segment(const std::vector<VirtualInputKey>& keys) const;
    std::wstring ConvertText(std::wstring_view text, CharacterStandard standard) const;
    std::vector<Emoji> FetchEmojiSequence(std::optional<int> category = std::nullopt) const;
    std::vector<Emoji> FetchDefaultFrequentEmojis() const;
    const ImeDatabase& Database() const;
    const Segmenter& SegmenterForMemory() const;

private:
    std::vector<Lexicon> Dispatch(
        const std::vector<VirtualInputKey>& keys,
        const Segmentation& segmentation,
        bool deepSearch,
        const ImeDatabase::LexiconQuery& query) const;
    std::vector<Lexicon> Search(
        const std::vector<VirtualInputKey>& keys,
        const Segmentation& segmentation,
        std::optional<int> limit,
        bool deepSearch,
        const ImeDatabase::LexiconQuery& query) const;
    std::vector<Lexicon> Query(
        size_t inputLength,
        const Segmentation& segmentation,
        std::optional<int> limit,
        const ImeDatabase::LexiconQuery& query) const;
    std::vector<Lexicon> Perform(
        const Scheme& scheme,
        std::optional<int> limit,
        const ImeDatabase::LexiconQuery& query) const;
    std::vector<Lexicon> ProcessSlices(
        const std::vector<VirtualInputKey>& keys,
        const std::wstring& text,
        std::optional<int> limit,
        const ImeDatabase::LexiconQuery& query) const;
    std::vector<Lexicon> FilterToneSuggestions(
        const std::vector<VirtualInputKey>& keys,
        const std::vector<Lexicon>& lexicons) const;
    std::vector<Lexicon> FilterApostropheAndToneSuggestions(
        const std::vector<VirtualInputKey>& keys,
        const std::vector<Lexicon>& lexicons) const;
    std::vector<Lexicon> FilterApostropheSuggestions(
        const std::vector<VirtualInputKey>& keys,
        const std::vector<Lexicon>& lexicons,
        const ImeDatabase::LexiconQuery& query) const;

    std::vector<Lexicon> AnchorsMatch(
        const std::vector<VirtualInputKey>& keys,
        std::optional<std::wstring> input = std::nullopt,
        std::optional<int> limit = std::nullopt,
        const ImeDatabase::LexiconQuery* query = nullptr) const;
    std::vector<Lexicon> SpellMatch(
        const std::vector<VirtualInputKey>& keys,
        int64_t complexity,
        std::wstring input,
        std::optional<std::wstring> mark = std::nullopt,
        std::optional<int> limit = std::nullopt,
        const ImeDatabase::LexiconQuery* query = nullptr) const;
    std::vector<Lexicon> ReverseLookupWord(
        const std::wstring& word,
        const std::wstring& input,
        std::optional<std::wstring> mark = std::nullopt) const;
    std::vector<Lexicon> PinyinReverseLookup(const std::vector<VirtualInputKey>& keys) const;
    std::vector<Lexicon> StrokeReverseLookup(const std::vector<VirtualInputKey>& keys) const;
    std::vector<Lexicon> StructureReverseLookup(const std::vector<VirtualInputKey>& keys) const;

    Lexicon Modify(
        const Lexicon& item,
        const std::vector<VirtualInputKey>& keys,
        const std::wstring& text,
        size_t inputLength) const;

    ImeDatabase _database;
    Segmenter _segmenter;
    PinyinSegmenter _pinyinSegmenter;
};

} // namespace Ime
