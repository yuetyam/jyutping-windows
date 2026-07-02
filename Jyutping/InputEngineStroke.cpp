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

struct StrokeKey
{
    int code = 0;
    WCHAR mark = L'\0';
    bool isWildcard = false;
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

std::optional<StrokeKey> StrokeKeyFromInputKey(const VirtualInputKey& key)
{
    switch (key.character)
    {
    case L'w':
    case L'h':
    case L't':
    case L'j':
    case L'1':
        return StrokeKey{ 1, static_cast<WCHAR>(0x2F00), false };
    case L's':
    case L'k':
    case L'2':
        return StrokeKey{ 2, static_cast<WCHAR>(0x2F01), false };
    case L'a':
    case L'p':
    case L'l':
    case L'3':
        return StrokeKey{ 3, static_cast<WCHAR>(0x2F03), false };
    case L'd':
    case L'n':
    case L'u':
    case L'4':
        return StrokeKey{ 4, static_cast<WCHAR>(0x2F02), false };
    case L'z':
    case L'i':
    case L'5':
        return StrokeKey{ 5, static_cast<WCHAR>(0x4E5B), false };
    case L'x':
    case L'o':
    case L'6':
        return StrokeKey{ 6, static_cast<WCHAR>(0xFF0A), true };
    default:
        return std::nullopt;
    }
}

std::optional<std::vector<StrokeKey>> StrokeKeysFromInputKeys(const std::vector<VirtualInputKey>& keys)
{
    if (keys.empty())
    {
        return std::nullopt;
    }

    std::vector<StrokeKey> result;
    result.reserve(keys.size());
    for (const VirtualInputKey& key : keys)
    {
        std::optional<StrokeKey> strokeKey = StrokeKeyFromInputKey(key);
        if (!strokeKey)
        {
            return std::nullopt;
        }
        result.push_back(*strokeKey);
    }
    return result;
}

bool ContainsWildcard(const std::vector<StrokeKey>& strokeKeys)
{
    return std::find_if(strokeKeys.begin(), strokeKeys.end(), [](const StrokeKey& key)
    {
        return key.isWildcard;
    }) != strokeKeys.end();
}

std::wstring StrokeInputText(const std::vector<StrokeKey>& strokeKeys)
{
    std::wstring result;
    result.reserve(strokeKeys.size());
    for (const StrokeKey& key : strokeKeys)
    {
        result.push_back(static_cast<WCHAR>(L'0' + key.code));
    }
    return result;
}

std::wstring StrokeMark(const std::vector<StrokeKey>& strokeKeys)
{
    std::wstring result;
    result.reserve(strokeKeys.size());
    for (const StrokeKey& key : strokeKeys)
    {
        result.push_back(key.mark);
    }
    return result;
}

std::wstring StrokeGlobPattern(const std::vector<StrokeKey>& strokeKeys)
{
    std::wstring result;
    result.reserve(strokeKeys.size());
    for (const StrokeKey& key : strokeKeys)
    {
        if (key.isWildcard)
        {
            result.append(L"[12345]");
        }
        else
        {
            result.push_back(static_cast<WCHAR>(L'0' + key.code));
        }
    }
    return result;
}

int64_t DecimalCombined(const std::vector<StrokeKey>& strokeKeys)
{
    int64_t result = 0;
    for (const StrokeKey& key : strokeKeys)
    {
        result = result * 10 + key.code;
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

std::vector<Lexicon> InputEngine::StrokeReverseLookup(const std::vector<VirtualInputKey>& keys) const
{
    if (!IsPrepared() || keys.empty())
    {
        return std::vector<Lexicon>();
    }

    std::optional<std::vector<StrokeKey>> strokeKeys = StrokeKeysFromInputKeys(keys);
    if (!strokeKeys)
    {
        return std::vector<Lexicon>();
    }

    std::wstring input = StrokeInputText(*strokeKeys);
    std::wstring mark = StrokeMark(*strokeKeys);
    std::wstring pattern = StrokeGlobPattern(*strokeKeys);
    bool hasWildcard = ContainsWildcard(*strokeKeys);

    std::vector<ShapeLexicon> shapes;
    if (hasWildcard)
    {
        Append(shapes, ShapeLexiconsFromRows(_database.QueryStrokeByPattern(pattern, false, 100), input, mark));
    }
    else if (strokeKeys->size() >= 19)
    {
        std::vector<ImeDatabase::ShapeRow> exactRows = _database.QueryStrokeBySpell(HashCode(input));
        Append(shapes, ShapeLexiconsFromRows(exactRows, input, mark, static_cast<int>(strokeKeys->size())));
    }
    else
    {
        std::vector<ImeDatabase::ShapeRow> exactRows = _database.QueryStrokeByCode(DecimalCombined(*strokeKeys));
        Append(shapes, ShapeLexiconsFromRows(exactRows, input, mark, static_cast<int>(strokeKeys->size())));
    }

    Append(shapes, ShapeLexiconsFromRows(_database.QueryStrokeByPattern(pattern + L"*", false, 100), input, mark));
    shapes = ShapeSorted(ShapeDistinct(shapes));

    std::vector<Lexicon> result;
    for (const ShapeLexicon& shape : shapes)
    {
        std::vector<Lexicon> lexicons = ReverseLookupWord(shape.text, shape.input, shape.mark);
        result.insert(result.end(), lexicons.begin(), lexicons.end());
    }
    return result;
}

} // namespace Ime
