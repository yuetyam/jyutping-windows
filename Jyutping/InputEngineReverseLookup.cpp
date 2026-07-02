#include "InputEngine.h"

namespace {

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

} // namespace

namespace Ime {

std::vector<Lexicon> InputEngine::ReverseLookup(ReverseLookupMethod method, std::wstring_view input) const
{
    return ReverseLookup(method, InputKeysFromText(input));
}

std::vector<Lexicon> InputEngine::ReverseLookup(ReverseLookupMethod method, const std::vector<VirtualInputKey>& keys) const
{
    if (!IsPrepared() || keys.empty())
    {
        return std::vector<Lexicon>();
    }

    switch (method)
    {
    case ReverseLookupMethod::Pinyin:
        return PinyinReverseLookup(keys);
    case ReverseLookupMethod::Cangjie:
        return CangjieReverseLookup(keys);
    case ReverseLookupMethod::Stroke:
        return StrokeReverseLookup(keys);
    default:
        return std::vector<Lexicon>();
    }
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

} // namespace Ime
