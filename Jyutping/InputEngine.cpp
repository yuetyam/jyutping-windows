#include "InputEngine.h"

#include <algorithm>

namespace {

constexpr int compoundNumberStep = 1000000;

struct PinyinLexicon
{
    std::wstring text;
    std::wstring pinyin;
    std::wstring input;
    size_t inputCount = 0;
    std::wstring mark;
    int64_t number = 0;

    PinyinLexicon() = default;
    PinyinLexicon(
        std::wstring inputText,
        std::wstring inputPinyin,
        std::wstring userInput,
        std::optional<std::wstring> inputMark = std::nullopt,
        int64_t inputNumber = 0) :
        text(std::move(inputText)),
        pinyin(std::move(inputPinyin)),
        input(std::move(userInput)),
        inputCount(input.size()),
        mark(inputMark.value_or(input)),
        number(inputNumber)
    {
    }

    PinyinLexicon ReplacedInput(std::wstring newInput) const
    {
        return PinyinLexicon(text, pinyin, std::move(newInput), mark, number);
    }
};

bool operator==(const PinyinLexicon& left, const PinyinLexicon& right)
{
    return left.text == right.text && left.pinyin == right.pinyin;
}

bool operator!=(const PinyinLexicon& left, const PinyinLexicon& right)
{
    return !(left == right);
}

bool operator<(const PinyinLexicon& left, const PinyinLexicon& right)
{
    if (left.inputCount != right.inputCount)
    {
        return left.inputCount > right.inputCount;
    }
    return left.number < right.number;
}

int QueryLimit(std::optional<int> limit, int defaultLimit)
{
    return limit.value_or(defaultLimit);
}

bool StartsWith(std::wstring_view text, std::wstring_view prefix)
{
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

bool EndsWith(std::wstring_view text, std::wstring_view suffix)
{
    return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
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

std::vector<VirtualInputKey> DropLast(const std::vector<VirtualInputKey>& keys, size_t count)
{
    size_t length = keys.size() - (std::min)(count, keys.size());
    return std::vector<VirtualInputKey>(keys.begin(), keys.begin() + length);
}

Ime::Scheme PrefixScheme(const Ime::Scheme& scheme, size_t count)
{
    size_t length = (std::min)(count, scheme.size());
    return Ime::Scheme(scheme.begin(), scheme.begin() + length);
}

Ime::PinyinScheme PrefixPinyinScheme(const Ime::PinyinScheme& scheme, size_t count)
{
    size_t length = (std::min)(count, scheme.size());
    return Ime::PinyinScheme(scheme.begin(), scheme.begin() + length);
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

std::wstring JoinTexts(const std::vector<std::wstring>& texts, WCHAR separator)
{
    std::wstring result;
    bool shouldAppendSeparator = false;
    for (const std::wstring& text : texts)
    {
        if (shouldAppendSeparator)
        {
            result.push_back(separator);
        }
        result.append(text);
        shouldAppendSeparator = true;
    }
    return result;
}

std::vector<VirtualInputKey> LetterKeys(const std::vector<VirtualInputKey>& keys)
{
    std::vector<VirtualInputKey> result;
    result.reserve(keys.size());
    for (const VirtualInputKey& key : keys)
    {
        if (key.IsLetter())
        {
            result.push_back(key);
        }
    }
    return result;
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

std::vector<PinyinLexicon> PinyinDistinct(const std::vector<PinyinLexicon>& items)
{
    std::vector<PinyinLexicon> result;
    result.reserve(items.size());
    for (const PinyinLexicon& item : items)
    {
        if (std::find(result.begin(), result.end(), item) == result.end())
        {
            result.push_back(item);
        }
    }
    return result;
}

void Append(std::vector<PinyinLexicon>& target, const std::vector<PinyinLexicon>& source)
{
    target.insert(target.end(), source.begin(), source.end());
}

std::vector<PinyinLexicon> PinyinSorted(std::vector<PinyinLexicon> items)
{
    std::sort(items.begin(), items.end());
    return items;
}

std::vector<PinyinLexicon> PinyinSortedByNumber(std::vector<PinyinLexicon> items)
{
    std::sort(items.begin(), items.end(), [](const PinyinLexicon& left, const PinyinLexicon& right)
    {
        return left.number < right.number;
    });
    return items;
}

std::vector<PinyinLexicon> PinyinFirst(std::vector<PinyinLexicon> items, size_t count)
{
    if (items.size() > count)
    {
        items.resize(count);
    }
    return items;
}

std::vector<PinyinLexicon> PinyinSortedByInputCount(std::vector<PinyinLexicon> items)
{
    std::stable_sort(items.begin(), items.end(), [](const PinyinLexicon& left, const PinyinLexicon& right)
    {
        return left.inputCount > right.inputCount;
    });
    return items;
}

std::optional<PinyinLexicon> Concatenate(const PinyinLexicon& left, const PinyinLexicon& right)
{
    return PinyinLexicon(
        left.text + right.text,
        left.pinyin + L" " + right.pinyin,
        left.input + right.input,
        left.mark + L" " + right.mark,
        (left.number + compoundNumberStep) + (right.number + compoundNumberStep));
}

std::vector<PinyinLexicon> PinyinLexiconsFromRows(
    const std::vector<ImeDatabase::PinyinLexiconRow>& rows,
    const std::wstring& input,
    const std::optional<std::wstring>& mark)
{
    std::vector<PinyinLexicon> result;
    result.reserve(rows.size());
    for (const ImeDatabase::PinyinLexiconRow& row : rows)
    {
        result.push_back(PinyinLexicon(row.word, row.romanization, input, mark.value_or(row.romanization), row.rowId));
    }
    return result;
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

std::vector<Ime::Lexicon> LexiconsFromRomanizations(
    const std::wstring& word,
    const std::vector<std::wstring>& romanizations,
    const std::wstring& input,
    const std::optional<std::wstring>& mark)
{
    std::vector<Ime::Lexicon> result;
    result.reserve(romanizations.size());
    for (const std::wstring& romanization : romanizations)
    {
        result.push_back(Ime::Lexicon::Cantonese(word, romanization, input, mark));
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

bool ContainsSchemeLength(const Ime::PinyinSegmentation& segmentation, size_t length)
{
    return std::find_if(segmentation.begin(), segmentation.end(), [length](const Ime::PinyinScheme& scheme)
    {
        return Ime::PinyinSchemeLength(scheme) == length;
    }) != segmentation.end();
}

bool PinyinContainsInputCount(const std::vector<PinyinLexicon>& items, size_t inputCount)
{
    return std::find_if(items.begin(), items.end(), [inputCount](const PinyinLexicon& item)
    {
        return item.inputCount == inputCount;
    }) != items.end();
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

std::vector<size_t> PinyinDistinctInputCounts(const std::vector<PinyinLexicon>& items)
{
    std::vector<size_t> result;
    for (const PinyinLexicon& item : items)
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

const PinyinLexicon* PinyinFindWithInputCount(const std::vector<PinyinLexicon>& items, size_t inputCount)
{
    auto found = std::find_if(items.begin(), items.end(), [inputCount](const PinyinLexicon& item)
    {
        return item.inputCount == inputCount;
    });
    return (found == items.end()) ? nullptr : &*found;
}

std::vector<PinyinLexicon> PinyinRowsBySpell(
    const ImeDatabase& database,
    std::wstring_view text,
    std::optional<int> limit)
{
    std::wstring queryText(text);
    return PinyinLexiconsFromRows(database.QueryPinyinBySpell(Ime::HashCode(queryText), QueryLimit(limit, -1)), queryText, std::nullopt);
}

std::vector<PinyinLexicon> PinyinRowsByAnchors(
    const ImeDatabase& database,
    const std::vector<VirtualInputKey>& keys,
    std::optional<std::wstring> input = std::nullopt,
    std::optional<int> limit = std::nullopt)
{
    int64_t code = Ime::CombinedCode(keys);
    if (code <= 0)
    {
        return std::vector<PinyinLexicon>();
    }

    std::wstring text = input.value_or(Ime::TextFromKeys(keys));
    return PinyinLexiconsFromRows(database.QueryPinyinByAnchors(code, QueryLimit(limit, 100)), text, text);
}

std::wstring TailAnchorsText(const std::vector<VirtualInputKey>& keys)
{
    std::wstring result;
    result.reserve(keys.size());
    for (const VirtualInputKey& key : keys)
    {
        result.append(key.text);
    }
    return result;
}

std::wstring PinyinSuffixAnchorsText(std::wstring_view pinyin, size_t prefixLength)
{
    if (prefixLength > pinyin.size())
    {
        return std::wstring();
    }

    std::wstring suffix(pinyin.substr(prefixLength));
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

PinyinLexicon Modify(const PinyinLexicon& item, const std::wstring& text, size_t inputLength)
{
    if (item.inputCount == inputLength)
    {
        return item;
    }

    if (StartsWith(Ime::StrippedSpaces(item.pinyin), text))
    {
        return PinyinLexicon(item.text, item.pinyin, text, text, item.number);
    }

    std::vector<std::wstring> syllables = Split(item.pinyin, L' ');
    if (syllables.empty() || !EndsWith(text, syllables.back()))
    {
        return item;
    }

    bool isMatched = (syllables.size() - 1 + syllables.back().size()) == inputLength;
    return isMatched ? PinyinLexicon(item.text, item.pinyin, text, text, item.number) : item;
}

std::vector<PinyinLexicon> PinyinQuery(
    const ImeDatabase& database,
    size_t inputLength,
    const Ime::PinyinSegmentation& segmentation,
    std::optional<int> limit)
{
    std::vector<Ime::PinyinScheme> idealSchemes;
    for (const Ime::PinyinScheme& scheme : segmentation)
    {
        if (Ime::PinyinSchemeLength(scheme) == inputLength)
        {
            idealSchemes.push_back(scheme);
        }
    }

    std::vector<PinyinLexicon> result;
    if (idealSchemes.empty())
    {
        for (const Ime::PinyinScheme& scheme : segmentation)
        {
            Append(result, PinyinRowsBySpell(database, Ime::PinyinSchemeText(scheme), limit));
        }
        return result;
    }

    for (const Ime::PinyinScheme& scheme : idealSchemes)
    {
        if (scheme.size() == 1)
        {
            Append(result, PinyinRowsBySpell(database, Ime::PinyinSchemeText(scheme), limit));
        }
        else
        {
            for (size_t count = scheme.size(); count > 0; count--)
            {
                Append(result, PinyinRowsBySpell(database, Ime::PinyinSchemeText(PrefixPinyinScheme(scheme, count)), limit));
            }
        }
    }
    return result;
}

std::vector<PinyinLexicon> ProcessPinyinSlices(
    const ImeDatabase& database,
    const std::vector<VirtualInputKey>& keys,
    const std::wstring& text,
    std::optional<int> limit)
{
    int adjustedLimit = limit ? 100 : 300;
    size_t inputLength = keys.size();
    std::vector<PinyinLexicon> result;

    for (size_t number = 0; number < inputLength; number++)
    {
        std::vector<VirtualInputKey> leadingKeys = DropLast(keys, number);
        std::wstring leadingText = Ime::TextFromKeys(leadingKeys);

        for (const PinyinLexicon& item : PinyinRowsBySpell(database, leadingText, limit))
        {
            result.push_back(Modify(item, text, inputLength));
        }

        std::vector<PinyinLexicon> anchorsMatched;
        for (const PinyinLexicon& item : PinyinRowsByAnchors(database, leadingKeys, leadingText, adjustedLimit))
        {
            anchorsMatched.push_back(Modify(item, text, inputLength));
        }
        anchorsMatched = PinyinFirst(PinyinSorted(anchorsMatched), 72);
        Append(result, anchorsMatched);
    }

    return PinyinSorted(PinyinDistinct(result));
}

std::vector<PinyinLexicon> PinyinSearch(
    const ImeDatabase& database,
    const Ime::PinyinSegmenter& segmenter,
    const std::vector<VirtualInputKey>& keys,
    const Ime::PinyinSegmentation& segmentation,
    std::optional<int> limit)
{
    size_t inputLength = keys.size();
    std::wstring text = Ime::TextFromKeys(keys);

    std::vector<PinyinLexicon> spellMatched = PinyinRowsBySpell(database, text, limit);
    std::vector<PinyinLexicon> anchorsMatched = PinyinRowsByAnchors(database, keys, text, limit);
    std::vector<PinyinLexicon> queried = PinyinQuery(database, inputLength, segmentation, limit);

    bool shouldMatchPrefixes = spellMatched.empty() &&
        !PinyinContainsInputCount(queried, inputLength) &&
        !ContainsSchemeLength(segmentation, inputLength);

    std::vector<PinyinLexicon> prefixMatched;
    if (shouldMatchPrefixes)
    {
        int prefixesLimit = limit ? 200 : 500;
        for (const Ime::PinyinScheme& scheme : segmentation)
        {
            size_t schemeLength = Ime::PinyinSchemeLength(scheme);
            std::vector<VirtualInputKey> tail = DropFirst(keys, schemeLength);
            if (tail.empty())
            {
                continue;
            }

            std::vector<VirtualInputKey> schemeAnchors;
            schemeAnchors.reserve(scheme.size());
            std::vector<std::wstring> schemeTexts;
            schemeTexts.reserve(scheme.size());
            for (const Ime::PinyinSyllable& syllable : scheme)
            {
                if (!syllable.keys.empty())
                {
                    schemeAnchors.push_back(syllable.keys.front());
                }
                schemeTexts.push_back(syllable.text);
            }

            std::vector<VirtualInputKey> conjoined = schemeAnchors;
            conjoined.insert(conjoined.end(), tail.begin(), tail.end());

            std::vector<VirtualInputKey> anchors = schemeAnchors;
            anchors.push_back(tail.front());

            std::wstring schemeMark = JoinTexts(schemeTexts, L' ');
            std::wstring mark = schemeMark + L" " + Ime::TextFromKeys(tail);
            std::wstring tailAnchorsText = TailAnchorsText(tail);

            for (const PinyinLexicon& item : PinyinRowsByAnchors(database, conjoined, std::nullopt, prefixesLimit))
            {
                if (!StartsWith(item.pinyin, schemeMark))
                {
                    continue;
                }
                if (PinyinSuffixAnchorsText(item.pinyin, schemeMark.size()) == tailAnchorsText)
                {
                    prefixMatched.push_back(PinyinLexicon(item.text, item.pinyin, text, mark, item.number));
                }
            }

            for (const PinyinLexicon& item : PinyinRowsByAnchors(database, anchors, std::nullopt, prefixesLimit))
            {
                if (StartsWith(item.pinyin, mark))
                {
                    prefixMatched.push_back(PinyinLexicon(item.text, item.pinyin, text, mark, item.number));
                }
            }
        }
    }

    std::vector<PinyinLexicon> gainedMatched;
    if (shouldMatchPrefixes)
    {
        for (size_t number = 1; number < inputLength; number++)
        {
            std::vector<VirtualInputKey> leadingKeys = DropLast(keys, number);
            std::wstring leadingText = Ime::TextFromKeys(leadingKeys);
            for (const PinyinLexicon& item : PinyinRowsByAnchors(database, leadingKeys, leadingText, 300))
            {
                if (StartsWith(Ime::StrippedSpaces(item.pinyin), text))
                {
                    gainedMatched.push_back(PinyinLexicon(item.text, item.pinyin, text, text, item.number));
                    continue;
                }

                std::vector<std::wstring> syllables = Split(item.pinyin, L' ');
                if (syllables.empty() || !EndsWith(text, syllables.back()))
                {
                    continue;
                }

                bool isMatched = (syllables.size() - 1 + syllables.back().size()) == inputLength;
                if (isMatched)
                {
                    gainedMatched.push_back(PinyinLexicon(item.text, item.pinyin, text, text, item.number));
                }
            }
        }
    }

    std::vector<PinyinLexicon> idealQueried;
    std::vector<PinyinLexicon> notIdealQueried;
    for (const PinyinLexicon& item : queried)
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

    idealQueried = PinyinDistinct(PinyinSortedByNumber(idealQueried));
    notIdealQueried = PinyinDistinct(PinyinSorted(notIdealQueried));

    std::vector<PinyinLexicon> fullInput;
    Append(fullInput, spellMatched);
    Append(fullInput, idealQueried);
    Append(fullInput, anchorsMatched);
    Append(fullInput, prefixMatched);
    Append(fullInput, gainedMatched);
    fullInput = PinyinDistinct(fullInput);

    std::vector<PinyinLexicon> fetched;
    Append(fetched, PinyinFirst(fullInput, 10));
    Append(fetched, PinyinFirst(PinyinSorted(fullInput), 10));
    Append(fetched, PinyinFirst(notIdealQueried, 10));
    Append(fetched, PinyinFirst(PinyinSortedByNumber(notIdealQueried), 10));
    Append(fetched, fullInput);
    Append(fetched, notIdealQueried);
    fetched = PinyinDistinct(fetched);

    if (fetched.empty())
    {
        return ProcessPinyinSlices(database, keys, text, limit);
    }

    size_t firstInputCount = fetched.front().inputCount;
    if (firstInputCount >= inputLength)
    {
        return fetched;
    }

    std::vector<PinyinLexicon> concatenated;
    for (size_t headLength : PinyinDistinctInputCounts(fetched))
    {
        std::vector<VirtualInputKey> tailKeys = DropFirst(keys, headLength);
        std::vector<PinyinLexicon> tailLexicons = PinyinSearch(database, segmenter, tailKeys, segmenter.Segment(tailKeys), 50);
        const PinyinLexicon* headLexicon = PinyinFindWithInputCount(fetched, headLength);
        if (tailLexicons.empty() || headLexicon == nullptr)
        {
            continue;
        }

        if (std::optional<PinyinLexicon> lexicon = Concatenate(*headLexicon, tailLexicons.front()))
        {
            concatenated.push_back(*lexicon);
        }
    }

    concatenated = PinyinFirst(PinyinSorted(PinyinDistinct(concatenated)), 1);
    Append(concatenated, fetched);
    return concatenated;
}

std::vector<PinyinLexicon> FilterPinyinSyllableSeparators(
    const std::vector<VirtualInputKey>& keys,
    const std::vector<PinyinLexicon>& lexicons)
{
    if (keys.empty() || keys.front().IsApostrophe())
    {
        return std::vector<PinyinLexicon>();
    }

    bool isTrailingSeparator = keys.back().IsApostrophe();
    size_t inputSeparatorCount = CountApostrophes(keys);
    size_t inputLength = keys.size();
    std::wstring text = Ime::TextFromKeys(keys);
    std::vector<std::wstring> textParts = Split(text, VirtualInputKey::apostrophe.character);

    std::vector<PinyinLexicon> filtered;
    for (const PinyinLexicon& item : lexicons)
    {
        std::vector<std::wstring> syllables = Split(item.pinyin, L' ');
        if (Equal(syllables, textParts))
        {
            filtered.push_back(item.ReplacedInput(text));
            continue;
        }

        if (inputSeparatorCount == 1 && isTrailingSeparator)
        {
            if (syllables.size() == 1 && item.inputCount == inputLength - 1)
            {
                filtered.push_back(item.ReplacedInput(text));
            }
            continue;
        }

        if (inputSeparatorCount == 1)
        {
            if (syllables.size() == 1)
            {
                if (!textParts.empty() && item.inputCount == textParts.front().size())
                {
                    filtered.push_back(item.ReplacedInput(item.input + L"'"));
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
                    filtered.push_back(item.ReplacedInput(item.input + L"'"));
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
                    filtered.push_back(item.ReplacedInput(item.input + L"'"));
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
                    filtered.push_back(item.ReplacedInput(text));
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
                    filtered.push_back(item.ReplacedInput(item.input + L"'"));
                }
                break;
            case 2:
                if (item.inputCount == 2)
                {
                    filtered.push_back(item.ReplacedInput(item.input + L"''"));
                }
                break;
            case 3:
                filtered.push_back(item.ReplacedInput(text));
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
                filtered.push_back(item.ReplacedInput(item.input + std::wstring(syllableCount - 1, L'i')));
            }
        }
    }

    return PinyinSorted(filtered);
}

} // namespace

namespace Ime {

bool InputEngine::Prepare()
{
    if (!_database.Open())
    {
        return false;
    }
    if (!_database.VerifySchema())
    {
        return false;
    }
    return _segmenter.Prepare(_database) && _pinyinSegmenter.Prepare(_database);
}

bool InputEngine::Prepare(_In_z_ PCWSTR databasePath)
{
    if (!_database.Open(databasePath))
    {
        return false;
    }
    if (!_database.VerifySchema())
    {
        return false;
    }
    return _segmenter.Prepare(_database) && _pinyinSegmenter.Prepare(_database);
}

bool InputEngine::IsPrepared() const
{
    return _database.IsOpen() && _segmenter.IsPrepared() && _pinyinSegmenter.IsPrepared();
}

std::vector<Lexicon> InputEngine::Suggest(std::wstring_view input, bool deepSearch) const
{
    return Suggest(InputKeysFromText(input), deepSearch);
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

std::vector<Lexicon> InputEngine::ReverseLookup(ReverseLookupMethod method, std::wstring_view input) const
{
    return ReverseLookup(method, InputKeysFromText(input));
}

std::vector<Lexicon> InputEngine::ReverseLookup(ReverseLookupMethod method, const std::vector<VirtualInputKey>& keys) const
{
    if (!IsPrepared())
    {
        return std::vector<Lexicon>();
    }

    switch (method)
    {
    case ReverseLookupMethod::Pinyin:
        return PinyinReverseLookup(keys);
    default:
        return std::vector<Lexicon>();
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

std::vector<Lexicon> InputEngine::ReverseLookupWord(const std::wstring& word, const std::wstring& input, std::optional<std::wstring> mark) const
{
    if (word.empty())
    {
        return std::vector<Lexicon>();
    }

    std::vector<std::wstring> exactRomanizations = _database.LookupRomanizationsForWord(word);
    if (!exactRomanizations.empty())
    {
        return LexiconsFromRomanizations(word, exactRomanizations, input, mark);
    }

    if (word.size() <= 1)
    {
        return std::vector<Lexicon>();
    }

    std::wstring remaining = word;
    std::vector<std::wstring> romanizations;
    while (!remaining.empty())
    {
        std::optional<std::wstring> matchedRomanization;
        size_t matchedLength = 0;
        std::wstring leading = remaining;
        while (!leading.empty())
        {
            std::vector<std::wstring> leadingRomanizations = _database.LookupRomanizationsForWord(leading);
            if (!leadingRomanizations.empty())
            {
                matchedRomanization = leadingRomanizations.front();
                matchedLength = leading.size();
                break;
            }
            leading.pop_back();
        }

        if (!matchedRomanization || matchedLength == 0)
        {
            romanizations.clear();
            break;
        }

        romanizations.push_back(*matchedRomanization);
        remaining.erase(0, matchedLength);
    }

    if (romanizations.empty())
    {
        return std::vector<Lexicon>();
    }

    std::wstring romanization;
    for (const std::wstring& item : romanizations)
    {
        if (!romanization.empty())
        {
            romanization.push_back(L' ');
        }
        romanization.append(item);
    }

    return LexiconsFromRomanizations(word, { romanization }, input, mark);
}

std::vector<Lexicon> InputEngine::PinyinReverseLookup(const std::vector<VirtualInputKey>& keys) const
{
    if (keys.empty())
    {
        return std::vector<Lexicon>();
    }

    bool hasSeparators = ContainsApostrophe(keys);
    std::vector<VirtualInputKey> searchKeys = hasSeparators ? LetterKeys(keys) : keys;
    if (searchKeys.empty())
    {
        return std::vector<Lexicon>();
    }

    std::vector<PinyinLexicon> pinyinLexicons;
    PinyinSegmentation segmentation = _pinyinSegmenter.Segment(searchKeys);
    if (segmentation.empty())
    {
        pinyinLexicons = ProcessPinyinSlices(_database, searchKeys, TextFromKeys(searchKeys), std::nullopt);
    }
    else
    {
        pinyinLexicons = PinyinSearch(_database, _pinyinSegmenter, searchKeys, segmentation, std::nullopt);
    }

    if (hasSeparators)
    {
        pinyinLexicons = FilterPinyinSyllableSeparators(keys, pinyinLexicons);
    }

    std::vector<Lexicon> result;
    for (const PinyinLexicon& item : pinyinLexicons)
    {
        Append(result, ReverseLookupWord(item.text, item.input, item.mark));
    }
    return result;
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
