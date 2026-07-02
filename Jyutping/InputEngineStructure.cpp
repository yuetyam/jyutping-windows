#include "InputEngine.h"

#include <algorithm>

namespace {

bool StartsWith(std::wstring_view text, std::wstring_view prefix)
{
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

bool EndsWith(std::wstring_view text, std::wstring_view suffix)
{
    return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
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

bool Equal(const std::vector<std::wstring>& left, const std::vector<std::wstring>& right)
{
    return left.size() == right.size() && std::equal(left.begin(), left.end(), right.begin());
}

bool ContainsApostrophe(const std::vector<VirtualInputKey>& keys)
{
    return std::find(keys.begin(), keys.end(), VirtualInputKey::apostrophe) != keys.end();
}

bool ContainsToneLetter(const std::vector<VirtualInputKey>& keys)
{
    return std::find_if(keys.begin(), keys.end(), [](const VirtualInputKey& key)
    {
        return key.IsToneLetter();
    }) != keys.end();
}

bool ContainsNonSyllableLetter(const std::vector<VirtualInputKey>& keys)
{
    return std::find_if(keys.begin(), keys.end(), [](const VirtualInputKey& key)
    {
        return !key.IsSyllableLetter();
    }) != keys.end();
}

void Append(std::vector<ImeDatabase::StructureRow>& target, const std::vector<ImeDatabase::StructureRow>& source)
{
    target.insert(target.end(), source.begin(), source.end());
}

std::vector<ImeDatabase::StructureRow> QueryStructureRows(const ImeDatabase& database, const std::wstring& text)
{
    if (text.empty())
    {
        return std::vector<ImeDatabase::StructureRow>();
    }
    return database.QueryStructureBySpell(Ime::HashCode(text));
}

std::vector<ImeDatabase::StructureRow> SearchStructureRows(
    const ImeDatabase& database,
    const std::wstring& text,
    const Ime::Segmentation& segmentation)
{
    std::vector<ImeDatabase::StructureRow> result = QueryStructureRows(database, text);
    size_t textLength = text.size();
    for (const Ime::Scheme& scheme : segmentation)
    {
        if (Ime::SchemeLength(scheme) == textLength)
        {
            Append(result, QueryStructureRows(database, Ime::SchemeOriginText(scheme)));
        }
    }
    return result;
}

std::vector<ImeDatabase::StructureRow> StructureDistinctWords(const std::vector<ImeDatabase::StructureRow>& rows)
{
    std::vector<ImeDatabase::StructureRow> result;
    result.reserve(rows.size());
    for (const ImeDatabase::StructureRow& row : rows)
    {
        auto found = std::find_if(result.begin(), result.end(), [&row](const ImeDatabase::StructureRow& item)
        {
            return item.word == row.word;
        });
        if (found == result.end())
        {
            result.push_back(row);
        }
    }
    return result;
}

std::vector<ImeDatabase::StructureRow> FilterApostropheAndTone(
    const std::vector<ImeDatabase::StructureRow>& rows,
    const std::wstring& convertedText)
{
    std::wstring textTones = Ime::ToneDigitOnly(convertedText);
    if (textTones.size() != 1)
    {
        return std::vector<ImeDatabase::StructureRow>();
    }

    bool isToneInTail = !convertedText.empty() && Ime::IsCantoneseToneDigit(convertedText.back());
    std::vector<ImeDatabase::StructureRow> result;
    for (const ImeDatabase::StructureRow& row : rows)
    {
        std::wstring tones = Ime::ToneDigitOnly(row.romanization);
        if (isToneInTail ? EndsWith(tones, textTones) : StartsWith(tones, textTones))
        {
            result.push_back(row);
        }
    }
    return result;
}

std::vector<ImeDatabase::StructureRow> FilterToneOnly(
    const std::vector<ImeDatabase::StructureRow>& rows,
    const std::wstring& convertedText)
{
    std::wstring textTones = Ime::ToneDigitOnly(convertedText);
    std::vector<ImeDatabase::StructureRow> result;

    switch (textTones.size())
    {
    case 1:
        {
            bool isToneInTail = !convertedText.empty() && Ime::IsCantoneseToneDigit(convertedText.back());
            for (const ImeDatabase::StructureRow& row : rows)
            {
                if (StartsWith(Ime::StrippedSpaces(row.romanization), convertedText))
                {
                    result.push_back(row);
                    continue;
                }

                std::wstring tones = Ime::ToneDigitOnly(row.romanization);
                if (isToneInTail ? EndsWith(tones, textTones) : StartsWith(tones, textTones))
                {
                    result.push_back(row);
                }
            }
        }
        break;
    case 2:
        for (const ImeDatabase::StructureRow& row : rows)
        {
            if (StartsWith(Ime::StrippedSpaces(row.romanization), convertedText) ||
                textTones == Ime::ToneDigitOnly(row.romanization))
            {
                result.push_back(row);
            }
        }
        break;
    default:
        for (const ImeDatabase::StructureRow& row : rows)
        {
            if (StartsWith(Ime::StrippedSpaces(row.romanization), convertedText))
            {
                result.push_back(row);
            }
        }
        break;
    }
    return result;
}

std::vector<ImeDatabase::StructureRow> FilterApostropheOnly(
    const std::vector<ImeDatabase::StructureRow>& rows,
    const std::wstring& inputText)
{
    std::vector<std::wstring> textParts = Split(inputText, VirtualInputKey::apostrophe.character);
    std::vector<ImeDatabase::StructureRow> result;
    for (const ImeDatabase::StructureRow& row : rows)
    {
        std::vector<std::wstring> syllables = Split(Ime::StrippedTones(row.romanization), L' ');
        if (Equal(syllables, textParts))
        {
            result.push_back(row);
        }
    }
    return result;
}

std::vector<ImeDatabase::StructureRow> FilterStructureRows(
    const std::vector<VirtualInputKey>& keys,
    const std::wstring& inputText,
    const std::vector<ImeDatabase::StructureRow>& rows)
{
    bool hasApostrophe = ContainsApostrophe(keys);
    bool hasToneLetter = ContainsToneLetter(keys);
    if (hasApostrophe && hasToneLetter)
    {
        return FilterApostropheAndTone(rows, Ime::ToneConverted(inputText));
    }
    if (hasToneLetter)
    {
        return FilterToneOnly(rows, Ime::ToneConverted(inputText));
    }
    if (hasApostrophe)
    {
        return FilterApostropheOnly(rows, inputText);
    }
    return rows;
}

std::wstring StructureMark(
    const std::vector<VirtualInputKey>& keys,
    const std::wstring& inputText,
    const Ime::Segmentation& segmentation)
{
    if (ContainsNonSyllableLetter(keys))
    {
        return Ime::MarkFormatted(Ime::ToneConverted(inputText));
    }

    if (segmentation.empty())
    {
        return inputText;
    }

    const Ime::Scheme& bestScheme = segmentation.front();
    size_t leadingLength = Ime::SchemeLength(bestScheme);
    if (leadingLength < keys.size())
    {
        return Ime::SchemeMark(bestScheme) + L" " + inputText.substr(leadingLength);
    }
    return Ime::SchemeMark(bestScheme);
}

} // namespace

namespace Ime {

std::vector<Lexicon> InputEngine::StructureReverseLookup(const std::vector<VirtualInputKey>& keys) const
{
    if (!IsPrepared() || keys.empty())
    {
        return std::vector<Lexicon>();
    }

    std::wstring inputText = TextFromKeys(keys);
    std::wstring markFreeText = TextFromKeys(SyllableKeys(keys));
    Segmentation segmentation = _segmenter.Segment(keys);

    std::vector<ImeDatabase::StructureRow> rows = SearchStructureRows(_database, markFreeText, segmentation);
    if (rows.empty())
    {
        return std::vector<Lexicon>();
    }

    rows = StructureDistinctWords(FilterStructureRows(keys, inputText, rows));
    if (rows.empty())
    {
        return std::vector<Lexicon>();
    }

    std::wstring mark = StructureMark(keys, inputText, segmentation);
    std::vector<Lexicon> result;
    for (const ImeDatabase::StructureRow& row : rows)
    {
        std::vector<Lexicon> lexicons = ReverseLookupWord(row.word, inputText, mark);
        result.insert(result.end(), lexicons.begin(), lexicons.end());
    }
    return result;
}

} // namespace Ime
