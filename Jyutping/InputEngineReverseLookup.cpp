#include "InputEngine.h"

#include <algorithm>
#include <utility>

namespace {

struct ShapeLexicon
{
    std::wstring text;
    std::wstring input;
    std::wstring mark;
    int complex = 0;
    int64_t number = 0;

    ShapeLexicon() = default;
    ShapeLexicon(std::wstring inputText, std::wstring userInput, std::wstring inputMark, int inputComplex, int64_t inputNumber) :
        text(std::move(inputText)),
        input(std::move(userInput)),
        mark(std::move(inputMark)),
        complex(inputComplex),
        number(inputNumber)
    {
    }
};

bool operator==(const ShapeLexicon& left, const ShapeLexicon& right)
{
    return left.text == right.text;
}

bool operator<(const ShapeLexicon& left, const ShapeLexicon& right)
{
    if (left.complex != right.complex)
    {
        return left.complex < right.complex;
    }
    return left.number < right.number;
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

bool IsQuickVariant(CangjieVariant variant)
{
    return variant == CangjieVariant::Quick5 || variant == CangjieVariant::Quick3;
}

WCHAR CangjieRootCharacter(const VirtualInputKey& key)
{
    static constexpr WCHAR roots[] =
    {
        static_cast<WCHAR>(0x65E5),
        static_cast<WCHAR>(0x6708),
        static_cast<WCHAR>(0x91D1),
        static_cast<WCHAR>(0x6728),
        static_cast<WCHAR>(0x6C34),
        static_cast<WCHAR>(0x706B),
        static_cast<WCHAR>(0x571F),
        static_cast<WCHAR>(0x7AF9),
        static_cast<WCHAR>(0x6208),
        static_cast<WCHAR>(0x5341),
        static_cast<WCHAR>(0x5927),
        static_cast<WCHAR>(0x4E2D),
        static_cast<WCHAR>(0x4E00),
        static_cast<WCHAR>(0x5F13),
        static_cast<WCHAR>(0x4EBA),
        static_cast<WCHAR>(0x5FC3),
        static_cast<WCHAR>(0x624B),
        static_cast<WCHAR>(0x53E3),
        static_cast<WCHAR>(0x5C38),
        static_cast<WCHAR>(0x5EFF),
        static_cast<WCHAR>(0x5C71),
        static_cast<WCHAR>(0x5973),
        static_cast<WCHAR>(0x7530),
        static_cast<WCHAR>(0x96E3),
        static_cast<WCHAR>(0x535C),
        static_cast<WCHAR>(0x91CD)
    };

    if (!key.IsLetter())
    {
        return L'\0';
    }

    size_t index = static_cast<size_t>(key.character - L'a');
    return (index < (sizeof(roots) / sizeof(roots[0]))) ? roots[index] : L'\0';
}

std::optional<std::wstring> CangjieRootMark(const std::vector<VirtualInputKey>& keys)
{
    if (keys.empty())
    {
        return std::nullopt;
    }

    std::wstring result;
    result.reserve(keys.size());
    for (const VirtualInputKey& key : keys)
    {
        WCHAR root = CangjieRootCharacter(key);
        if (root == L'\0')
        {
            return std::nullopt;
        }
        result.push_back(root);
    }
    return result;
}

std::vector<ShapeLexicon> ShapeLexiconsFromRows(
    const std::vector<ImeDatabase::ShapeRow>& rows,
    const std::wstring& input,
    const std::wstring& mark,
    std::optional<int> complexOverride = std::nullopt)
{
    std::vector<ShapeLexicon> result;
    result.reserve(rows.size());
    for (const ImeDatabase::ShapeRow& row : rows)
    {
        result.push_back(ShapeLexicon(row.word, input, mark, complexOverride.value_or(row.complex), row.rowId));
    }
    return result;
}

std::vector<ShapeLexicon> ShapeDistinct(const std::vector<ShapeLexicon>& items)
{
    std::vector<ShapeLexicon> result;
    result.reserve(items.size());
    for (const ShapeLexicon& item : items)
    {
        if (std::find(result.begin(), result.end(), item) == result.end())
        {
            result.push_back(item);
        }
    }
    return result;
}

std::vector<ShapeLexicon> ShapeSorted(std::vector<ShapeLexicon> items)
{
    std::sort(items.begin(), items.end());
    return items;
}

void Append(std::vector<ShapeLexicon>& target, const std::vector<ShapeLexicon>& source)
{
    target.insert(target.end(), source.begin(), source.end());
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
    default:
        return std::vector<Lexicon>();
    }
}

std::vector<Lexicon> InputEngine::CangjieReverseLookup(std::wstring_view input, CangjieVariant variant) const
{
    return CangjieReverseLookup(InputKeysFromText(input), variant);
}

std::vector<Lexicon> InputEngine::CangjieReverseLookup(const std::vector<VirtualInputKey>& keys, CangjieVariant variant) const
{
    if (!IsPrepared() || keys.empty())
    {
        return std::vector<Lexicon>();
    }

    std::optional<std::wstring> mark = CangjieRootMark(keys);
    if (!mark)
    {
        return std::vector<Lexicon>();
    }

    std::wstring text = TextFromKeys(keys);
    std::vector<ShapeLexicon> shapes;
    if (std::optional<int64_t> code = CharCodeFromText(text))
    {
        std::vector<ImeDatabase::ShapeRow> exactRows = IsQuickVariant(variant) ?
            _database.QueryQuickByExactCode(variant, *code) :
            _database.QueryCangjieByExactCode(variant, *code);
        Append(shapes, ShapeLexiconsFromRows(exactRows, text, *mark, static_cast<int>(text.size())));
    }

    std::vector<ImeDatabase::ShapeRow> prefixRows = IsQuickVariant(variant) ?
        _database.QueryQuickByPrefix(variant, text, 100) :
        _database.QueryCangjieByPrefix(variant, text, 100);
    Append(shapes, ShapeLexiconsFromRows(prefixRows, text, *mark));

    shapes = ShapeSorted(ShapeDistinct(shapes));

    std::vector<Lexicon> result;
    for (const ShapeLexicon& shape : shapes)
    {
        std::vector<Lexicon> lexicons = ReverseLookupWord(shape.text, shape.input, shape.mark);
        result.insert(result.end(), lexicons.begin(), lexicons.end());
    }
    return result;
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
