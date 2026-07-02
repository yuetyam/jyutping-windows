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

size_t CountApostrophes(const std::vector<VirtualInputKey>& keys)
{
    return static_cast<size_t>(std::count(keys.begin(), keys.end(), VirtualInputKey::apostrophe));
}

bool ContainsApostrophe(const std::vector<VirtualInputKey>& keys)
{
    return std::find(keys.begin(), keys.end(), VirtualInputKey::apostrophe) != keys.end();
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
    if (searchKeys.size() < 2)
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
        std::vector<Lexicon> lexicons = ReverseLookupWord(item.text, item.input, item.mark);
        result.insert(result.end(), lexicons.begin(), lexicons.end());
    }
    return result;
}

} // namespace Ime
