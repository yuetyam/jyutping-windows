#include "InputEngine.h"

#include <algorithm>

namespace {

int QueryLimit(std::optional<int> limit, int defaultLimit)
{
    return limit.value_or(defaultLimit);
}

std::vector<VirtualInputKey> KeysFromText(std::wstring_view input)
{
    std::vector<VirtualInputKey> keys;
    keys.reserve(input.size());
    for (WCHAR character : input)
    {
        VirtualInputKey key;
        if (character == VirtualInputKey::apostrophe.character)
        {
            key = VirtualInputKey::apostrophe;
        }
        else if (character == VirtualInputKey::grave.character)
        {
            key = VirtualInputKey::grave;
        }
        else if (!VirtualInputKey::MatchInputKeyForCharacter(character, &key))
        {
            return std::vector<VirtualInputKey>();
        }
        keys.push_back(key);
    }
    return keys;
}

bool StartsWith(std::wstring_view text, std::wstring_view prefix)
{
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

bool Equal(const std::vector<std::wstring>& left, const std::vector<std::wstring>& right)
{
    return left.size() == right.size() && std::equal(left.begin(), left.end(), right.begin());
}

std::vector<VirtualInputKey> Prefix(const std::vector<VirtualInputKey>& keys, size_t count)
{
    size_t length = (std::min)(count, keys.size());
    return std::vector<VirtualInputKey>(keys.begin(), keys.begin() + length);
}

std::vector<VirtualInputKey> DropFirst(const std::vector<VirtualInputKey>& keys, size_t count)
{
    size_t offset = (std::min)(count, keys.size());
    return std::vector<VirtualInputKey>(keys.begin() + offset, keys.end());
}

Ime::Scheme PrefixScheme(const Ime::Scheme& scheme, size_t count)
{
    size_t length = (std::min)(count, scheme.size());
    return Ime::Scheme(scheme.begin(), scheme.begin() + length);
}

std::vector<std::wstring> Split(std::wstring_view text, WCHAR separator)
{
    std::vector<std::wstring> parts;
    size_t start = 0;
    while (start <= text.size())
    {
        size_t index = text.find(separator, start);
        size_t end = (index == std::wstring_view::npos) ? text.size() : index;
        if (end > start)
        {
            parts.push_back(std::wstring(text.substr(start, end - start)));
        }
        if (index == std::wstring_view::npos)
        {
            break;
        }
        start = index + 1;
    }
    return parts;
}

std::vector<std::vector<VirtualInputKey>> SplitKeys(const std::vector<VirtualInputKey>& keys, const VirtualInputKey& separator)
{
    std::vector<std::vector<VirtualInputKey>> parts;
    std::vector<VirtualInputKey> part;
    for (const VirtualInputKey& key : keys)
    {
        if (key == separator)
        {
            if (!part.empty())
            {
                parts.push_back(part);
                part.clear();
            }
            continue;
        }
        part.push_back(key);
    }
    if (!part.empty())
    {
        parts.push_back(part);
    }
    return parts;
}

std::optional<std::wstring> LastToneFreeSyllable(std::wstring_view romanization)
{
    std::vector<std::wstring> syllables = Split(romanization, L' ');
    if (syllables.empty())
    {
        return std::nullopt;
    }
    return Ime::StrippedTones(syllables.back());
}

std::wstring TailAnchorText(const std::vector<VirtualInputKey>& keys)
{
    std::wstring result;
    result.reserve(keys.size());
    for (const VirtualInputKey& key : keys)
    {
        const VirtualInputKey& inputKey = key.IsYLetterY() ? VirtualInputKey::letterJ : key;
        result.push_back(inputKey.character);
    }
    return result;
}

std::wstring SuffixAnchorText(std::wstring_view romanization, size_t prefixLength)
{
    if (prefixLength > romanization.size())
    {
        return std::wstring();
    }

    std::wstring suffix(romanization.substr(prefixLength));
    std::vector<std::wstring> syllables = Split(suffix, L' ');

    std::wstring result;
    result.reserve(syllables.size());
    for (const std::wstring& syllable : syllables)
    {
        if (!syllable.empty())
        {
            result.push_back(syllable.front());
        }
    }
    return result;
}

std::vector<Ime::Lexicon> Distinct(const std::vector<Ime::Lexicon>& items)
{
    std::vector<Ime::Lexicon> result;
    result.reserve(items.size());
    for (const Ime::Lexicon& item : items)
    {
        if (std::find(result.begin(), result.end(), item) == result.end())
        {
            result.push_back(item);
        }
    }
    return result;
}

void Append(std::vector<Ime::Lexicon>& target, const std::vector<Ime::Lexicon>& source)
{
    target.insert(target.end(), source.begin(), source.end());
}

std::vector<Ime::Lexicon> Sorted(std::vector<Ime::Lexicon> items)
{
    std::sort(items.begin(), items.end());
    return items;
}

std::vector<Ime::Lexicon> SortedByNumber(std::vector<Ime::Lexicon> items)
{
    std::sort(items.begin(), items.end(), [](const Ime::Lexicon& left, const Ime::Lexicon& right)
    {
        return left.number < right.number;
    });
    return items;
}

std::vector<Ime::Lexicon> First(std::vector<Ime::Lexicon> items, size_t count)
{
    if (items.size() > count)
    {
        items.resize(count);
    }
    return items;
}

std::vector<Ime::Lexicon> SortedByInputCount(std::vector<Ime::Lexicon> items)
{
    std::stable_sort(items.begin(), items.end(), [](const Ime::Lexicon& left, const Ime::Lexicon& right)
    {
        return left.inputCount > right.inputCount;
    });
    return items;
}

std::vector<Ime::Lexicon> LexiconsFromRows(
    const std::vector<ImeDatabase::LexiconRow>& rows,
    const std::wstring& input,
    const std::optional<std::wstring>& mark)
{
    std::vector<Ime::Lexicon> result;
    result.reserve(rows.size());
    for (const ImeDatabase::LexiconRow& row : rows)
    {
        result.push_back(Ime::Lexicon::Cantonese(row.word, row.romanization, input, mark.value_or(Ime::StrippedTones(row.romanization)), row.rowId));
    }
    return result;
}

size_t FirstAliasCount(const Ime::Segmentation& segmentation)
{
    if (segmentation.empty() || segmentation.front().empty())
    {
        return 0;
    }
    return segmentation.front().front().alias.size();
}

bool ContainsInputCount(const std::vector<Ime::Lexicon>& items, size_t inputCount)
{
    return std::find_if(items.begin(), items.end(), [inputCount](const Ime::Lexicon& item)
    {
        return item.inputCount == inputCount;
    }) != items.end();
}

bool ContainsSchemeLength(const Ime::Segmentation& segmentation, size_t length)
{
    return std::find_if(segmentation.begin(), segmentation.end(), [length](const Ime::Scheme& scheme)
    {
        return Ime::SchemeLength(scheme) == length;
    }) != segmentation.end();
}

std::vector<size_t> DistinctInputCounts(const std::vector<Ime::Lexicon>& items)
{
    std::vector<size_t> result;
    for (const Ime::Lexicon& item : items)
    {
        if (std::find(result.begin(), result.end(), item.inputCount) == result.end())
        {
            result.push_back(item.inputCount);
        }
    }
    return result;
}

size_t CountApostrophes(const std::vector<VirtualInputKey>& keys)
{
    return static_cast<size_t>(std::count(keys.begin(), keys.end(), VirtualInputKey::apostrophe));
}

bool ContainsApostrophe(const std::vector<VirtualInputKey>& keys)
{
    return std::find(keys.begin(), keys.end(), VirtualInputKey::apostrophe) != keys.end();
}

bool ContainsToneInputKey(const std::vector<VirtualInputKey>& keys)
{
    return std::find_if(keys.begin(), keys.end(), [](const VirtualInputKey& key)
    {
        return key.IsToneInputKey();
    }) != keys.end();
}

const Ime::Lexicon* FindWithInputCount(const std::vector<Ime::Lexicon>& items, size_t inputCount)
{
    auto found = std::find_if(items.begin(), items.end(), [inputCount](const Ime::Lexicon& item)
    {
        return item.inputCount == inputCount;
    });
    return (found == items.end()) ? nullptr : &*found;
}

} // namespace

namespace Ime {

bool InputEngine::Prepare()
{
    if (!_database.Open())
    {
        return false;
    }
    return _segmenter.Prepare(_database);
}

bool InputEngine::Prepare(_In_z_ PCWSTR databasePath)
{
    if (!_database.Open(databasePath))
    {
        return false;
    }
    return _segmenter.Prepare(_database);
}

bool InputEngine::IsPrepared() const
{
    return _database.IsOpen() && _segmenter.IsPrepared();
}

std::vector<Lexicon> InputEngine::Suggest(std::wstring_view input, bool deepSearch) const
{
    return Suggest(KeysFromText(input), deepSearch);
}

std::vector<Lexicon> InputEngine::Suggest(const std::vector<VirtualInputKey>& keys, bool deepSearch) const
{
    if (!IsPrepared())
    {
        return std::vector<Lexicon>();
    }

    switch (keys.size())
    {
    case 0:
        return std::vector<Lexicon>();
    case 1:
        if (keys.front() == VirtualInputKey::letterA)
        {
            std::vector<Lexicon> result;
            Append(result, SpellMatch(L"a", L"a", L"a"));
            Append(result, SpellMatch(L"aa", L"a", L"a"));
            Append(result, AnchorsMatch(keys, L"a"));
            return result;
        }
        if (keys.front() == VirtualInputKey::letterO || keys.front() == VirtualInputKey::letterM)
        {
            std::wstring text = TextFromKeys(keys);
            std::vector<Lexicon> result;
            Append(result, SpellMatch(text, text, text));
            Append(result, AnchorsMatch(keys, text));
            return result;
        }
        return AnchorsMatch(keys);
    default:
        return Dispatch(keys, _segmenter.Segment(keys), deepSearch);
    }
}

Segmentation InputEngine::Segment(const std::vector<VirtualInputKey>& keys) const
{
    return _segmenter.Segment(keys);
}

std::vector<Lexicon> InputEngine::Dispatch(const std::vector<VirtualInputKey>& keys, const Segmentation& segmentation, bool deepSearch) const
{
    std::vector<VirtualInputKey> syllableKeys = SyllableKeys(keys);
    std::wstring syllableText = TextFromKeys(syllableKeys);
    std::vector<Lexicon> lexicons;

    size_t aliasCount = FirstAliasCount(segmentation);
    if (aliasCount == 0)
    {
        lexicons = deepSearch ? ProcessSlices(syllableKeys, syllableText, std::nullopt) : AnchorsMatch(syllableKeys, syllableText);
    }
    else if ((aliasCount == 1 && syllableKeys.size() > 1) || syllableKeys.size() != keys.size())
    {
        lexicons = Search(syllableKeys, segmentation, std::nullopt, deepSearch);
        Append(lexicons, ProcessSlices(syllableKeys, syllableText, std::nullopt));
    }
    else
    {
        lexicons = Search(syllableKeys, segmentation, std::nullopt, deepSearch);
    }

    if (ContainsApostrophe(keys) && !ContainsToneInputKey(keys))
    {
        return FilterApostropheSuggestions(keys, lexicons);
    }
    return lexicons;
}

std::vector<Lexicon> InputEngine::Search(const std::vector<VirtualInputKey>& keys, const Segmentation& segmentation, std::optional<int> limit, bool deepSearch) const
{
    size_t inputLength = keys.size();
    std::wstring text = TextFromKeys(keys);

    std::vector<Lexicon> spellMatched = SpellMatch(text, text, std::nullopt, limit);
    std::vector<Lexicon> anchorsMatched = AnchorsMatch(keys, text, limit);
    std::vector<Lexicon> queried = Query(inputLength, segmentation, limit);

    bool shouldMatchPrefixes = false;
    if (deepSearch && inputLength > 2 && inputLength < 25)
    {
        shouldMatchPrefixes = keys.back() == VirtualInputKey::letterM || keys.front() == VirtualInputKey::letterM;
        if (!shouldMatchPrefixes)
        {
            shouldMatchPrefixes = spellMatched.empty() &&
                !ContainsInputCount(queried, inputLength) &&
                !ContainsSchemeLength(segmentation, inputLength);
        }
    }

    std::vector<Lexicon> prefixMatched;
    if (shouldMatchPrefixes)
    {
        int prefixesLimit = limit ? 200 : 500;
        for (const Scheme& scheme : segmentation)
        {
            if (scheme.empty())
            {
                continue;
            }

            std::vector<VirtualInputKey> tail = DropFirst(keys, SchemeLength(scheme));
            if (tail.empty())
            {
                continue;
            }

            std::vector<VirtualInputKey> schemeAnchors = SchemeAliasAnchors(scheme);
            std::vector<VirtualInputKey> conjoined = schemeAnchors;
            conjoined.insert(conjoined.end(), tail.begin(), tail.end());

            std::vector<VirtualInputKey> anchors = schemeAnchors;
            anchors.push_back(tail.front());

            std::wstring schemeSyllableText = SchemeSyllableText(scheme);
            std::wstring mark = SchemeMark(scheme) + L" " + TextFromKeys(tail);
            std::wstring tailAsAnchorText = TailAnchorText(tail);

            for (const Lexicon& item : AnchorsMatch(conjoined, std::nullopt, prefixesLimit))
            {
                std::wstring toneFreeRomanization = StrippedTones(item.romanization);
                if (!StartsWith(toneFreeRomanization, schemeSyllableText))
                {
                    continue;
                }
                if (SuffixAnchorText(toneFreeRomanization, schemeSyllableText.size()) == tailAsAnchorText)
                {
                    prefixMatched.push_back(Lexicon::Cantonese(item.text, item.romanization, text, mark, item.number));
                }
            }

            std::wstring transformedTailText;
            transformedTailText.reserve(tail.size());
            for (size_t index = 0; index < tail.size(); index++)
            {
                const VirtualInputKey& key = (index == 0 && tail[index].IsYLetterY()) ? VirtualInputKey::letterJ : tail[index];
                transformedTailText.push_back(key.character);
            }

            std::wstring syllables = schemeSyllableText + L" " + transformedTailText;
            for (const Lexicon& item : AnchorsMatch(anchors, std::nullopt, prefixesLimit))
            {
                if (StartsWith(StrippedTones(item.romanization), syllables))
                {
                    prefixMatched.push_back(Lexicon::Cantonese(item.text, item.romanization, text, mark, item.number));
                }
            }
        }
    }

    std::vector<Lexicon> gainedMatched;
    if (shouldMatchPrefixes)
    {
        for (size_t number = inputLength - 1; number > 0; number--)
        {
            std::vector<VirtualInputKey> leadingKeys = Prefix(keys, number);
            std::wstring leadingText = TextFromKeys(leadingKeys);
            for (const Lexicon& item : AnchorsMatch(leadingKeys, leadingText, 300))
            {
                size_t tailStart = (item.inputCount > 0) ? item.inputCount - 1 : 0;
                std::vector<VirtualInputKey> tail = DropFirst(keys, tailStart);
                if (tail.size() > 6)
                {
                    continue;
                }

                Lexicon converted = Lexicon::Cantonese(item.text, item.romanization, text, text, item.number);
                if (StartsWith(LatinLetterOnly(item.romanization), text))
                {
                    gainedMatched.push_back(converted);
                    continue;
                }

                std::optional<std::wstring> lastSyllable = LastToneFreeSyllable(item.romanization);
                if (!lastSyllable)
                {
                    continue;
                }

                if (std::optional<std::wstring> tailSyllable = _segmenter.SyllableText(tail))
                {
                    if (*lastSyllable == *tailSyllable)
                    {
                        gainedMatched.push_back(converted);
                    }
                }
                else if (StartsWith(*lastSyllable, TextFromKeys(tail)))
                {
                    gainedMatched.push_back(converted);
                }
            }
        }
    }

    std::vector<Lexicon> idealQueried;
    std::vector<Lexicon> notIdealQueried;
    for (const Lexicon& item : queried)
    {
        if (item.inputCount == inputLength)
        {
            idealQueried.push_back(item);
        }
        else if (item.inputCount < inputLength)
        {
            notIdealQueried.push_back(item);
        }
    }

    idealQueried = Distinct(SortedByNumber(idealQueried));
    notIdealQueried = Distinct(Sorted(notIdealQueried));

    std::vector<Lexicon> fullInput;
    Append(fullInput, spellMatched);
    Append(fullInput, idealQueried);
    Append(fullInput, anchorsMatched);
    Append(fullInput, prefixMatched);
    Append(fullInput, gainedMatched);
    fullInput = Distinct(fullInput);

    std::vector<Lexicon> fetched;
    Append(fetched, First(fullInput, 10));
    Append(fetched, First(Sorted(fullInput), 10));
    Append(fetched, First(notIdealQueried, 10));
    Append(fetched, First(SortedByNumber(notIdealQueried), 10));
    Append(fetched, fullInput);
    Append(fetched, notIdealQueried);
    fetched = Distinct(fetched);

    if (fetched.empty())
    {
        return deepSearch ? ProcessSlices(keys, text, limit) : AnchorsMatch(keys, text, limit);
    }

    size_t firstInputCount = fetched.front().inputCount;
    if (firstInputCount >= inputLength || !deepSearch)
    {
        return fetched;
    }

    std::vector<Lexicon> concatenated;
    for (size_t headLength : DistinctInputCounts(fetched))
    {
        std::vector<VirtualInputKey> tailKeys = DropFirst(keys, headLength);
        std::vector<Lexicon> tailLexicons = Search(tailKeys, _segmenter.Segment(tailKeys), 50, deepSearch);
        const Lexicon* headLexicon = FindWithInputCount(fetched, headLength);
        if (tailLexicons.empty() || headLexicon == nullptr)
        {
            continue;
        }

        if (std::optional<Lexicon> lexicon = Concatenate(*headLexicon, tailLexicons.front()))
        {
            concatenated.push_back(*lexicon);
        }
    }

    concatenated = First(Sorted(Distinct(concatenated)), 1);
    Append(concatenated, fetched);
    return concatenated;
}

std::vector<Lexicon> InputEngine::Query(size_t inputLength, const Segmentation& segmentation, std::optional<int> limit) const
{
    std::vector<Scheme> idealSchemes;
    for (const Scheme& scheme : segmentation)
    {
        if (SchemeLength(scheme) == inputLength)
        {
            idealSchemes.push_back(scheme);
        }
    }

    std::vector<Lexicon> result;
    if (idealSchemes.empty())
    {
        for (const Scheme& scheme : segmentation)
        {
            Append(result, Perform(scheme, limit));
        }
        return result;
    }

    for (const Scheme& scheme : idealSchemes)
    {
        if (scheme.size() == 1)
        {
            Append(result, Perform(scheme, limit));
        }
        else
        {
            for (size_t count = scheme.size(); count > 0; count--)
            {
                Append(result, Perform(PrefixScheme(scheme, count), limit));
            }
        }
    }
    return result;
}

std::vector<Lexicon> InputEngine::Perform(const Scheme& scheme, std::optional<int> limit) const
{
    int64_t anchors = CombinedCode(SchemeOriginAnchors(scheme));
    if (anchors <= 0)
    {
        return std::vector<Lexicon>();
    }

    int64_t spell = HashCode(SchemeOriginText(scheme));
    return StrictMatch(anchors, spell, SchemeAliasText(scheme), SchemeMark(scheme), limit);
}

std::vector<Lexicon> InputEngine::ProcessSlices(const std::vector<VirtualInputKey>& keys, const std::wstring& text, std::optional<int> limit) const
{
    int adjustedLimit = limit ? 100 : 300;
    size_t inputLength = keys.size();
    std::vector<Lexicon> result;

    for (size_t number = 0; number < inputLength; number++)
    {
        size_t leadingLength = inputLength - number;
        std::vector<VirtualInputKey> leadingKeys = Prefix(keys, leadingLength);
        std::wstring leadingText = TextFromKeys(leadingKeys);

        for (const Lexicon& item : SpellMatch(leadingText, leadingText, std::nullopt, limit))
        {
            result.push_back(Modify(item, keys, text, inputLength));
        }

        std::vector<Lexicon> anchorsMatched;
        for (const Lexicon& item : AnchorsMatch(leadingKeys, leadingText, adjustedLimit))
        {
            anchorsMatched.push_back(Modify(item, keys, text, inputLength));
        }
        anchorsMatched = First(Sorted(anchorsMatched), 72);
        for (const Lexicon& item : anchorsMatched)
        {
            result.push_back(item);
        }
    }

    return Sorted(Distinct(result));
}

std::vector<Lexicon> InputEngine::FilterApostropheSuggestions(const std::vector<VirtualInputKey>& keys, const std::vector<Lexicon>& lexicons) const
{
    if (keys.empty() || keys.front().IsApostrophe())
    {
        return std::vector<Lexicon>();
    }

    bool isTrailingSeparator = keys.back().IsApostrophe();
    size_t inputSeparatorCount = CountApostrophes(keys);
    size_t inputLength = keys.size();
    std::wstring text = TextFromKeys(keys);
    std::vector<std::wstring> textParts = Split(text, VirtualInputKey::apostrophe.character);

    std::vector<Lexicon> qualified;
    for (const Lexicon& item : lexicons)
    {
        std::vector<std::wstring> syllables = Split(StrippedTones(item.romanization), L' ');
        if (Equal(syllables, textParts))
        {
            qualified.push_back(item.ReplacedInput(text));
            continue;
        }

        if (inputSeparatorCount == 1 && isTrailingSeparator)
        {
            if (syllables.size() == 1 && item.inputCount == inputLength - 1)
            {
                qualified.push_back(item.ReplacedInput(text));
            }
            continue;
        }

        if (inputSeparatorCount == 1)
        {
            if (syllables.size() == 1)
            {
                if (!textParts.empty() && item.inputCount == textParts.front().size())
                {
                    qualified.push_back(item.ReplacedInput(item.input + L"'"));
                }
            }
            else if (syllables.size() == 2 && textParts.size() >= 2)
            {
                bool isMatched = true;
                if (inputLength != 3 && syllables.front() != textParts.front())
                {
                    isMatched = textParts.front().size() == 1 &&
                        !syllables.front().empty() &&
                        textParts.front().front() == syllables.front().front() &&
                        StartsWith(textParts.back(), syllables.back());
                }
                if (isMatched)
                {
                    qualified.push_back(item.ReplacedInput(item.input + L"'"));
                }
            }
            continue;
        }

        if (inputSeparatorCount == 2 && isTrailingSeparator)
        {
            if (syllables.size() == 1)
            {
                if (!textParts.empty() && item.inputCount == textParts.front().size())
                {
                    qualified.push_back(item.ReplacedInput(item.input + L"'"));
                }
            }
            else if (syllables.size() == 2 && textParts.size() >= 2 && item.inputCount == inputLength - 2)
            {
                bool isMatched = true;
                if (inputLength != 4 && syllables.front() != textParts.front())
                {
                    isMatched = textParts.front().size() == 1 &&
                        !syllables.front().empty() &&
                        textParts.front().front() == syllables.front().front() &&
                        textParts.back() == syllables.back();
                }
                if (isMatched)
                {
                    qualified.push_back(item.ReplacedInput(text));
                }
            }
            continue;
        }

        if (((inputSeparatorCount == 2 && inputLength == 5) || (inputSeparatorCount == 3 && inputLength == 6)) &&
            textParts.size() == 3)
        {
            switch (syllables.size())
            {
            case 1:
                if (item.inputCount == 1)
                {
                    qualified.push_back(item.ReplacedInput(item.input + L"'"));
                }
                break;
            case 2:
                if (item.inputCount == 2)
                {
                    qualified.push_back(item.ReplacedInput(item.input + L"''"));
                }
                break;
            case 3:
                qualified.push_back(item.ReplacedInput(text));
                break;
            default:
                break;
            }
            continue;
        }

        size_t textPartCount = textParts.size();
        size_t syllableCount = syllables.size();
        if (syllableCount < textPartCount && syllableCount > 0)
        {
            bool isMatched = true;
            for (size_t index = 0; index < syllableCount; index++)
            {
                if (syllables[index] != textParts[index])
                {
                    isMatched = false;
                    break;
                }
            }
            if (isMatched)
            {
                qualified.push_back(item.ReplacedInput(item.input + std::wstring(syllableCount - 1, L'i')));
            }
        }
    }

    if (!qualified.empty())
    {
        return SortedByInputCount(Distinct(qualified));
    }

    std::vector<VirtualInputKey> anchorKeys;
    for (const std::vector<VirtualInputKey>& part : SplitKeys(keys, VirtualInputKey::apostrophe))
    {
        if (!part.empty())
        {
            anchorKeys.push_back(part.front());
        }
    }

    size_t anchorCount = anchorKeys.size();
    std::vector<Lexicon> anchorsMatched;
    for (const Lexicon& item : AnchorsMatch(anchorKeys))
    {
        std::vector<std::wstring> syllables = Split(item.romanization, L' ');
        if (syllables.size() != anchorCount || textParts.size() != anchorCount)
        {
            continue;
        }

        bool isMatched = true;
        for (size_t index = 0; index < anchorCount; index++)
        {
            std::wstring syllable = StrippedTones(syllables[index]);
            const std::wstring& part = textParts[index];
            bool isAnchorOnly = part.size() == 1;
            if ((isAnchorOnly && !StartsWith(syllable, part)) || (!isAnchorOnly && syllable != part))
            {
                isMatched = false;
                break;
            }
        }

        if (isMatched)
        {
            anchorsMatched.push_back(item.ReplacedInput(text));
        }
    }
    return anchorsMatched;
}

std::vector<Lexicon> InputEngine::AnchorsMatch(const std::vector<VirtualInputKey>& keys, std::optional<std::wstring> input, std::optional<int> limit) const
{
    int64_t code = AnchorsCode(keys);
    if (code <= 0)
    {
        return std::vector<Lexicon>();
    }

    std::wstring text = input.value_or(TextFromKeys(keys));
    return LexiconsFromRows(_database.QueryLexiconByAnchors(code, QueryLimit(limit, 100)), text, text);
}

std::vector<Lexicon> InputEngine::SpellMatch(std::wstring_view text, std::wstring input, std::optional<std::wstring> mark, std::optional<int> limit) const
{
    std::wstring queryText(text);
    return LexiconsFromRows(_database.QueryLexiconBySpell(HashCode(queryText), QueryLimit(limit, -1)), input, mark);
}

std::vector<Lexicon> InputEngine::StrictMatch(int64_t anchors, int64_t spell, std::wstring input, std::optional<std::wstring> mark, std::optional<int> limit) const
{
    std::vector<ImeDatabase::LexiconRow> rows = _database.QueryLexiconStrict(spell, anchors, QueryLimit(limit, -1));
    return LexiconsFromRows(rows, input, mark);
}

Lexicon InputEngine::Modify(const Lexicon& item, const std::vector<VirtualInputKey>& keys, const std::wstring& text, size_t inputLength) const
{
    if (inputLength <= 1 || item.inputCount == inputLength)
    {
        return item;
    }

    Lexicon converted = Lexicon::Cantonese(item.text, item.romanization, text, text, item.number);
    if (StartsWith(LatinLetterOnly(item.romanization), text))
    {
        return converted;
    }

    std::optional<std::wstring> lastSyllable = LastToneFreeSyllable(item.romanization);
    if (!lastSyllable)
    {
        return item;
    }

    size_t tailStart = (item.inputCount > 0) ? item.inputCount - 1 : 0;
    std::vector<VirtualInputKey> tail = DropFirst(keys, tailStart);
    if (tail.size() > 6)
    {
        return item;
    }

    if (std::optional<std::wstring> tailSyllable = _segmenter.SyllableText(tail))
    {
        return (*lastSyllable == *tailSyllable) ? converted : item;
    }

    return StartsWith(*lastSyllable, TextFromKeys(tail)) ? converted : item;
}

} // namespace Ime
