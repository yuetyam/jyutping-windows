#include "Private.h"
#include "InputMemory.h"
#include "Logger.h"

#include <winsqlite/winsqlite3.h>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace {

class Statement
{
public:
    Statement() : _statement(nullptr)
    {
    }

    ~Statement()
    {
        sqlite3_finalize(_statement);
    }

    sqlite3_stmt** Out()
    {
        return &_statement;
    }

    sqlite3_stmt* Get() const
    {
        return _statement;
    }

private:
    sqlite3_stmt* _statement;
};

struct MemoryLexicon
{
    std::wstring word;
    std::wstring romanization;
    int64_t frequency = 0;
    int64_t latest = 0;
    std::wstring input;
    size_t inputCount = 0;
    std::wstring mark;

    MemoryLexicon() = default;

    MemoryLexicon(
        std::wstring inputWord,
        std::wstring inputRomanization,
        int64_t inputFrequency,
        int64_t inputLatest,
        std::wstring userInput,
        std::wstring inputMark) :
        word(std::move(inputWord)),
        romanization(std::move(inputRomanization)),
        frequency(inputFrequency),
        latest(inputLatest),
        input(std::move(userInput)),
        inputCount(input.size()),
        mark(std::move(inputMark))
    {
    }
};

bool operator==(const MemoryLexicon& left, const MemoryLexicon& right)
{
    return left.word == right.word && left.romanization == right.romanization;
}

std::string WideToUtf8(_In_z_ PCWSTR text)
{
    if (text == nullptr || *text == L'\0')
    {
        return std::string();
    }

    int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1)
    {
        return std::string();
    }

    std::string result(static_cast<size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring ColumnText(sqlite3_stmt* statement, int column)
{
    const void* text = sqlite3_column_text16(statement, column);
    if (text == nullptr)
    {
        return std::wstring();
    }

    int byteCount = sqlite3_column_bytes16(statement, column);
    return std::wstring(static_cast<const wchar_t*>(text), static_cast<size_t>(byteCount) / sizeof(wchar_t));
}

void BindText(sqlite3_stmt* statement, int index, const std::wstring& text)
{
    sqlite3_bind_text16(statement, index, text.c_str(), -1, SQLITE_TRANSIENT);
}

void LogSqliteError(sqlite3* database, _In_z_ PCWSTR operation, int result)
{
    const WCHAR* message = L"";
    if (database != nullptr)
    {
        message = static_cast<const WCHAR*>(sqlite3_errmsg16(database));
    }
    Global::Log(L"InputMemory %s failed (%d): %s", operation, result, message);
}

std::wstring InputMemoryDirectory()
{
    return Global::UserDataDirectory();
}

std::wstring InputMemoryDatabasePath()
{
    std::wstring directory = InputMemoryDirectory();
    if (directory.empty())
    {
        return std::wstring();
    }

    std::wstring path = directory;
    path.push_back(L'\\');
    path.append(L"memory.sqlite3");
    return path;
}

int64_t CurrentTimeMilliseconds()
{
    auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
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

template<typename T>
void Append(std::vector<T>& target, const std::vector<T>& source)
{
    target.insert(target.end(), source.begin(), source.end());
}

template<typename T>
std::vector<T> First(std::vector<T> items, size_t count)
{
    if (items.size() > count)
    {
        items.resize(count);
    }
    return items;
}

template<typename T>
std::vector<T> Distinct(const std::vector<T>& items)
{
    std::vector<T> result;
    result.reserve(items.size());
    for (const T& item : items)
    {
        if (std::find(result.begin(), result.end(), item) == result.end())
        {
            result.push_back(item);
        }
    }
    return result;
}

std::vector<MemoryLexicon> RegularSorted(const std::vector<MemoryLexicon>& items, bool isOrdered = false)
{
    std::vector<MemoryLexicon> frequencyPreferred = items;
    if (!isOrdered)
    {
        std::sort(frequencyPreferred.begin(), frequencyPreferred.end(), [](const MemoryLexicon& left, const MemoryLexicon& right)
        {
            return left.frequency > right.frequency;
        });
    }

    std::vector<MemoryLexicon> datePreferred = items;
    std::sort(datePreferred.begin(), datePreferred.end(), [](const MemoryLexicon& left, const MemoryLexicon& right)
    {
        return left.latest > right.latest;
    });

    std::vector<MemoryLexicon> result;
    Append(result, First(frequencyPreferred, 3));
    Append(result, First(datePreferred, 5));
    Append(result, frequencyPreferred);
    return Distinct(result);
}

std::vector<MemoryLexicon> PeculiarSorted(const std::vector<MemoryLexicon>& items)
{
    std::vector<size_t> inputCounts;
    for (const MemoryLexicon& item : items)
    {
        if (std::find(inputCounts.begin(), inputCounts.end(), item.inputCount) == inputCounts.end())
        {
            inputCounts.push_back(item.inputCount);
        }
    }
    std::sort(inputCounts.begin(), inputCounts.end(), std::greater<size_t>());

    std::vector<MemoryLexicon> result;
    for (size_t inputCount : inputCounts)
    {
        std::vector<MemoryLexicon> group;
        for (const MemoryLexicon& item : items)
        {
            if (item.inputCount == inputCount)
            {
                group.push_back(item);
            }
        }
        Append(result, RegularSorted(group));
    }
    return result;
}

std::vector<Ime::Lexicon> ToLexicons(const std::vector<MemoryLexicon>& items, int64_t number)
{
    std::vector<Ime::Lexicon> result;
    result.reserve(items.size());
    for (const MemoryLexicon& item : items)
    {
        result.push_back(Ime::Lexicon::Cantonese(item.word, item.romanization, item.input, item.mark, number));
    }
    return result;
}

MemoryLexicon ReplacedInputAndMark(const MemoryLexicon& item, std::wstring input, std::wstring mark)
{
    return MemoryLexicon(item.word, item.romanization, item.frequency, item.latest, std::move(input), std::move(mark));
}

MemoryLexicon ReplacedInput(const MemoryLexicon& item, std::wstring input)
{
    return MemoryLexicon(item.word, item.romanization, item.frequency, item.latest, input, item.mark);
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

std::optional<std::wstring> LastToneFreeSyllable(std::wstring_view romanization)
{
    std::vector<std::wstring> syllables = Split(romanization, L' ');
    if (syllables.empty())
    {
        return std::nullopt;
    }
    return Ime::StrippedTones(syllables.back());
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

size_t CountApostrophes(const std::vector<VirtualInputKey>& keys)
{
    return static_cast<size_t>(std::count(keys.begin(), keys.end(), VirtualInputKey::apostrophe));
}

size_t UnicodeScalarCount(std::wstring_view text)
{
    size_t count = 0;
    for (size_t index = 0; index < text.size(); index++)
    {
        WCHAR character = text[index];
        if (0xD800 <= character && character <= 0xDBFF && index + 1 < text.size())
        {
            WCHAR next = text[index + 1];
            if (0xDC00 <= next && next <= 0xDFFF)
            {
                index++;
            }
        }
        count++;
    }
    return count;
}

struct MemorySerialFields
{
    int64_t charCount = 0;
    int64_t letterCount = 0;
    int64_t complexity = 0;
    int64_t anchors = 0;
    int64_t spell = 0;
};

MemorySerialFields DeriveSerialFields(std::wstring_view word, std::wstring_view romanization)
{
    std::wstring toneFreeRomanization;
    std::wstring letters;
    toneFreeRomanization.reserve(romanization.size());
    letters.reserve(romanization.size());
    for (WCHAR character : romanization)
    {
        if (character < L'0' || character > L'9')
        {
            toneFreeRomanization.push_back(character);
        }
        if (Ime::IsLowercaseBasicLatinLetter(character))
        {
            letters.push_back(character);
        }
    }

    std::vector<std::wstring> phones = Split(toneFreeRomanization, L' ');
    uint64_t complexity = 0;
    std::vector<VirtualInputKey> anchorKeys;
    anchorKeys.reserve(phones.size());
    for (const std::wstring& phone : phones)
    {
        complexity = complexity * 10 + static_cast<uint64_t>(phone.size());
        if (!phone.empty())
        {
            VirtualInputKey key = VirtualInputKey::letterA;
            if (VirtualInputKey::MatchInputKeyForCharacter(phone.front(), &key))
            {
                anchorKeys.push_back(key);
            }
        }
    }

    std::vector<VirtualInputKey> spellKeys = Ime::InputKeysFromText(letters);
    return MemorySerialFields{
        static_cast<int64_t>(UnicodeScalarCount(word)),
        static_cast<int64_t>(spellKeys.size()),
        std::bit_cast<int64_t>(complexity),
        Ime::CombinedCode(anchorKeys),
        Ime::CombinedCode(spellKeys)
    };
}

std::vector<MemoryLexicon> ReadMemoryRows(sqlite3_stmt* statement, sqlite3* database, _In_z_ PCWSTR operation, const std::wstring& input, const std::optional<std::wstring>& mark)
{
    std::vector<MemoryLexicon> rows;
    int result = SQLITE_OK;
    while ((result = sqlite3_step(statement)) == SQLITE_ROW)
    {
        std::wstring word = ColumnText(statement, 0);
        std::wstring romanization = ColumnText(statement, 1);
        int64_t frequency = sqlite3_column_int64(statement, 2);
        int64_t latest = sqlite3_column_int64(statement, 3);
        rows.push_back(MemoryLexicon(word, romanization, frequency, latest, input, mark.value_or(Ime::StrippedTones(romanization))));
    }

    if (result != SQLITE_DONE)
    {
        LogSqliteError(database, operation, result);
    }
    return rows;
}

bool PrepareMemoryStatement(sqlite3* database, _In_z_ PCWSTR sql, Statement& statement)
{
    if (database == nullptr)
    {
        return false;
    }

    int result = sqlite3_prepare16_v2(database, sql, -1, statement.Out(), nullptr);
    if (result != SQLITE_OK)
    {
        LogSqliteError(database, L"prepare statement", result);
        return false;
    }
    return true;
}

std::vector<MemoryLexicon> AnchorsMatch(
    sqlite3* database,
    sqlite3_stmt* statement,
    const std::vector<VirtualInputKey>& keys,
    const std::optional<std::wstring>& input = std::nullopt,
    int64_t limit = 100)
{
    if (statement == nullptr)
    {
        return std::vector<MemoryLexicon>();
    }

    sqlite3_reset(statement);
    sqlite3_clear_bindings(statement);
    sqlite3_bind_int64(statement, 1, Ime::AnchorsCode(keys));
    sqlite3_bind_int64(statement, 2, static_cast<int64_t>(keys.size()));
    sqlite3_bind_int64(statement, 3, limit);
    std::wstring userInput = input.value_or(Ime::TextFromKeys(keys));
    return ReadMemoryRows(statement, database, L"query memory anchors", userInput, userInput);
}

std::vector<MemoryLexicon> SpellMatch(
    sqlite3* database,
    sqlite3_stmt* statement,
    const std::vector<VirtualInputKey>& keys,
    int64_t complexity,
    const std::optional<std::wstring>& input = std::nullopt,
    const std::optional<std::wstring>& mark = std::nullopt,
    int64_t limit = 100)
{
    if (statement == nullptr)
    {
        return std::vector<MemoryLexicon>();
    }

    sqlite3_reset(statement);
    sqlite3_clear_bindings(statement);
    sqlite3_bind_int64(statement, 1, Ime::CombinedCode(keys));
    sqlite3_bind_int64(statement, 2, static_cast<int64_t>(keys.size()));
    sqlite3_bind_int64(statement, 3, complexity);
    sqlite3_bind_int64(statement, 4, limit);
    std::wstring userInput = input.value_or(Ime::TextFromKeys(keys));
    return ReadMemoryRows(statement, database, L"query memory spell", userInput, mark);
}

std::vector<MemoryLexicon> Perform(sqlite3* database, sqlite3_stmt* statement, const Ime::Scheme& scheme, int64_t limit = 5)
{
    return SpellMatch(
        database,
        statement,
        Ime::SchemeOriginKeys(scheme),
        Ime::SchemeComplexity(scheme),
        Ime::SchemeAliasText(scheme),
        Ime::SchemeMark(scheme),
        limit);
}

std::vector<MemoryLexicon> Query(
    sqlite3* database,
    sqlite3_stmt* statement,
    const Ime::Segmentation& segmentation,
    const std::vector<Ime::Scheme>& idealSchemes)
{
    std::vector<MemoryLexicon> queried;
    if (idealSchemes.empty())
    {
        for (const Ime::Scheme& scheme : segmentation)
        {
            Append(queried, Perform(database, statement, scheme));
        }
    }
    else
    {
        for (const Ime::Scheme& scheme : idealSchemes)
        {
            for (size_t count = scheme.size(); count > 0; count--)
            {
                int64_t limit = (count == scheme.size()) ? 20 : 5;
                Append(queried, Perform(database, statement, PrefixScheme(scheme, count), limit));
            }
        }
    }
    return queried;
}

std::vector<Ime::Lexicon> Search(sqlite3* database, const std::vector<VirtualInputKey>& keys, const Ime::Segmentation& segmentation, const Ime::Segmenter& segmenter)
{
    static constexpr WCHAR anchorsSql[] =
        L"SELECT word, romanization, frequency, latest FROM memory2608 WHERE anchors = ? AND char_count = ? ORDER BY frequency DESC LIMIT ?;";
    static constexpr WCHAR spellSql[] =
        L"SELECT word, romanization, frequency, latest FROM memory2608 WHERE spell = ? AND letter_count = ? AND complexity = ? ORDER BY frequency DESC LIMIT ?;";

    Statement anchorsStatement;
    Statement spellStatement;
    if (!PrepareMemoryStatement(database, anchorsSql, anchorsStatement) ||
        !PrepareMemoryStatement(database, spellSql, spellStatement))
    {
        return std::vector<Ime::Lexicon>();
    }

    size_t inputLength = keys.size();
    std::wstring text = Ime::TextFromKeys(keys);
    std::vector<Ime::Scheme> idealSchemes;
    for (const Ime::Scheme& scheme : segmentation)
    {
        if (Ime::SchemeLength(scheme) == inputLength)
        {
            idealSchemes.push_back(scheme);
        }
    }

    std::vector<MemoryLexicon> queried = Query(database, spellStatement.Get(), segmentation, idealSchemes);
    std::vector<MemoryLexicon> idealQueried;
    std::vector<MemoryLexicon> notIdealQueried;
    for (const MemoryLexicon& item : queried)
    {
        if (item.inputCount >= inputLength)
        {
            idealQueried.push_back(item);
        }
        else
        {
            notIdealQueried.push_back(item);
        }
    }

    std::vector<Ime::Lexicon> ideal = ToLexicons(RegularSorted(idealQueried), -1);
    std::vector<Ime::Lexicon> notIdeal = ToLexicons(PeculiarSorted(notIdealQueried), -2);
    std::vector<MemoryLexicon> anchorsMatched = AnchorsMatch(
        database,
        anchorsStatement.Get(),
        keys,
        std::nullopt,
        queried.empty() ? 20 : 5);
    std::vector<Ime::Lexicon> anchors = ToLexicons(RegularSorted(anchorsMatched, true), -1);
    if (!ideal.empty() || !anchors.empty())
    {
        Append(ideal, anchors);
        Append(ideal, notIdeal);
        return ideal;
    }

    if (inputLength <= 2 || inputLength >= 25)
    {
        return notIdeal;
    }

    bool shouldPartiallyMatch = idealSchemes.empty() ||
        keys.back() == VirtualInputKey::letterM ||
        keys.front() == VirtualInputKey::letterM;
    if (!shouldPartiallyMatch)
    {
        return notIdeal;
    }

    static constexpr size_t maximumCharCount = 9;
    std::vector<MemoryLexicon> prefixMatched;
    for (const Ime::Scheme& scheme : segmentation)
    {
        if (scheme.empty() || scheme.size() > maximumCharCount)
        {
            continue;
        }

        std::vector<VirtualInputKey> tail = DropFirst(keys, Ime::SchemeLength(scheme));
        if (tail.empty())
        {
            continue;
        }

        std::vector<VirtualInputKey> schemeAnchors = Ime::SchemeAliasAnchors(scheme);
        std::vector<VirtualInputKey> conjoined = schemeAnchors;
        conjoined.insert(conjoined.end(), tail.begin(), tail.end());

        std::wstring schemeSyllableText = Ime::SchemeSyllableText(scheme);
        std::wstring mark = Ime::SchemeMark(scheme) + L" " + Ime::TextFromKeys(tail);
        std::wstring tailAsAnchorText = TailAnchorText(tail);

        for (const MemoryLexicon& item : AnchorsMatch(database, anchorsStatement.Get(), conjoined))
        {
            std::wstring toneFreeRomanization = Ime::StrippedTones(item.romanization);
            if (StartsWith(toneFreeRomanization, schemeSyllableText) &&
                SuffixAnchorText(toneFreeRomanization, schemeSyllableText.size()) == tailAsAnchorText)
            {
                prefixMatched.push_back(ReplacedInputAndMark(item, text, mark));
            }
        }

        std::wstring transformedTailText;
        transformedTailText.reserve(tail.size());
        for (size_t index = 0; index < tail.size(); index++)
        {
            const VirtualInputKey& key = (index == 0 && tail[index].IsYLetterY()) ? VirtualInputKey::letterJ : tail[index];
            transformedTailText.push_back(key.character);
        }

        std::vector<VirtualInputKey> anchorsKeys = schemeAnchors;
        anchorsKeys.push_back(tail.front());
        std::wstring syllableText = schemeSyllableText + L" " + transformedTailText;
        for (const MemoryLexicon& item : AnchorsMatch(database, anchorsStatement.Get(), anchorsKeys))
        {
            if (StartsWith(Ime::StrippedTones(item.romanization), syllableText))
            {
                prefixMatched.push_back(ReplacedInputAndMark(item, text, mark));
            }
        }
    }

    std::vector<MemoryLexicon> gainedMatched;
    for (size_t number = inputLength - 1; number > 0; number--)
    {
        if (number > maximumCharCount)
        {
            continue;
        }

        for (const MemoryLexicon& item : AnchorsMatch(database, anchorsStatement.Get(), Prefix(keys, number)))
        {
            size_t tailStart = (item.inputCount > 0) ? item.inputCount - 1 : 0;
            std::vector<VirtualInputKey> tail = DropFirst(keys, tailStart);
            if (tail.size() > 6)
            {
                continue;
            }

            MemoryLexicon converted = ReplacedInputAndMark(item, text, text);
            if (StartsWith(Ime::LatinLetterOnly(item.romanization), text))
            {
                gainedMatched.push_back(converted);
                continue;
            }

            std::optional<std::wstring> lastSyllable = LastToneFreeSyllable(item.romanization);
            if (!lastSyllable)
            {
                continue;
            }

            if (std::optional<std::wstring> tailSyllable = segmenter.SyllableText(tail))
            {
                if (*lastSyllable == *tailSyllable)
                {
                    gainedMatched.push_back(converted);
                }
            }
            else if (StartsWith(*lastSyllable, Ime::TextFromKeys(tail)))
            {
                gainedMatched.push_back(converted);
            }
        }
    }

    std::vector<MemoryLexicon> partial;
    Append(partial, prefixMatched);
    Append(partial, gainedMatched);

    std::vector<Ime::Lexicon> result = ToLexicons(First(PeculiarSorted(partial), 5), -1);
    Append(result, notIdeal);
    return result;
}

std::vector<Ime::Lexicon> FilterToneSuggestions(const std::vector<VirtualInputKey>& keys, const std::vector<Ime::Lexicon>& candidates)
{
    std::wstring inputText = Ime::TextFromKeys(keys);
    std::wstring text = Ime::ToneConverted(inputText);
    std::wstring textTones = Ime::ToneDigitOnly(text);

    std::vector<Ime::Lexicon> qualified;
    for (const Ime::Lexicon& item : candidates)
    {
        std::wstring syllableText = Ime::StrippedSpaces(item.romanization);
        if (syllableText == text)
        {
            qualified.push_back(item.ReplacedInput(inputText));
            continue;
        }

        std::wstring tones = Ime::ToneDigitOnly(syllableText);
        if (textTones.size() == 1 && tones.size() == 1)
        {
            if (text.size() == item.inputCount + 1 &&
                !text.empty() &&
                Ime::IsCantoneseToneDigit(text.back()) &&
                textTones == tones)
            {
                qualified.push_back(item.ReplacedInput(inputText));
            }
            continue;
        }

        if (textTones.size() == 1 && tones.size() == 2)
        {
            bool isToneLast = !text.empty() && Ime::IsCantoneseToneDigit(text.back());
            if (isToneLast)
            {
                bool hasMatchingTone = !tones.empty() && tones.back() == textTones.front();
                bool isCorrectPosition = item.inputCount < text.size() && Ime::IsCantoneseToneDigit(text[item.inputCount]);
                if (hasMatchingTone && isCorrectPosition)
                {
                    qualified.push_back(item.ReplacedInput(inputText));
                }
            }
            else if (!tones.empty() && tones.front() == textTones.front())
            {
                qualified.push_back(item.ReplacedInput(inputText));
            }
            continue;
        }

        if (textTones.size() == 2 && tones.size() == 2)
        {
            if (!text.empty() &&
                Ime::IsCantoneseToneDigit(text.back()) &&
                textTones == tones &&
                item.inputCount == text.size() - 2)
            {
                qualified.push_back(item.ReplacedInput(inputText));
            }
            continue;
        }

        if (inputText == syllableText)
        {
            qualified.push_back(item.ReplacedInput(inputText));
        }
    }
    return qualified;
}

std::vector<Ime::Lexicon> FilterApostropheSuggestions(const std::vector<VirtualInputKey>& keys, const std::vector<Ime::Lexicon>& candidates)
{
    if (keys.empty() || keys.front().IsApostrophe())
    {
        return std::vector<Ime::Lexicon>();
    }

    bool isTrailingSeparator = keys.back().IsApostrophe();
    size_t inputSeparatorCount = CountApostrophes(keys);
    size_t inputLength = keys.size();
    std::wstring text = Ime::TextFromKeys(keys);
    std::vector<std::wstring> textParts = Split(text, VirtualInputKey::apostrophe.character);

    std::vector<Ime::Lexicon> qualified;
    for (const Ime::Lexicon& item : candidates)
    {
        std::vector<std::wstring> syllables = Split(Ime::StrippedTones(item.romanization), L' ');
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
            if (syllables.size() != 2 || textParts.size() < 2)
            {
                continue;
            }

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
                qualified.push_back(item.ReplacedInput(text));
            }
            continue;
        }

        if (inputSeparatorCount == 2 && isTrailingSeparator)
        {
            if (syllables.size() != 2 || textParts.size() < 2 || item.inputCount != inputLength - 2)
            {
                continue;
            }

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
            continue;
        }

        bool hasThreePartInput =
            ((inputSeparatorCount == 2 && inputLength == 5) || (inputSeparatorCount == 3 && inputLength == 6)) &&
            textParts.size() == 3;
        if (hasThreePartInput && syllables.size() == 3)
        {
            qualified.push_back(item.ReplacedInput(text));
        }
    }
    return qualified;
}

bool FindMemoryEntry(sqlite3* database, const std::wstring& word, const std::wstring& romanization, int64_t& id, int64_t& frequency)
{
    static constexpr WCHAR sql[] = L"SELECT id, frequency FROM memory2608 WHERE word = ? AND romanization = ? LIMIT 1;";

    Statement statement;
    if (!PrepareMemoryStatement(database, sql, statement))
    {
        return false;
    }

    BindText(statement.Get(), 1, word);
    BindText(statement.Get(), 2, romanization);

    int result = sqlite3_step(statement.Get());
    if (result == SQLITE_ROW)
    {
        id = sqlite3_column_int64(statement.Get(), 0);
        frequency = sqlite3_column_int64(statement.Get(), 1);
        return true;
    }

    if (result != SQLITE_DONE)
    {
        LogSqliteError(database, L"find memory entry", result);
    }
    return false;
}

bool UpdateMemoryEntry(sqlite3* database, int64_t id, int64_t frequency)
{
    static constexpr WCHAR sql[] = L"UPDATE memory2608 SET frequency = ?, latest = ? WHERE id = ?;";

    Statement statement;
    if (!PrepareMemoryStatement(database, sql, statement))
    {
        return false;
    }

    sqlite3_bind_int64(statement.Get(), 1, frequency);
    sqlite3_bind_int64(statement.Get(), 2, CurrentTimeMilliseconds());
    sqlite3_bind_int64(statement.Get(), 3, id);

    int result = sqlite3_step(statement.Get());
    if (result != SQLITE_DONE)
    {
        LogSqliteError(database, L"update memory entry", result);
        return false;
    }
    return true;
}

bool InsertMemoryEntry(sqlite3* database, const Ime::Lexicon& lexicon)
{
    static constexpr WCHAR sql[] =
        L"INSERT INTO memory2608 (word, romanization, frequency, latest, char_count, letter_count, complexity, anchors, spell) "
        L"VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);";

    Statement statement;
    if (!PrepareMemoryStatement(database, sql, statement))
    {
        return false;
    }

    MemorySerialFields fields = DeriveSerialFields(lexicon.text, lexicon.romanization);

    BindText(statement.Get(), 1, lexicon.text);
    BindText(statement.Get(), 2, lexicon.romanization);
    sqlite3_bind_int64(statement.Get(), 3, 1);
    sqlite3_bind_int64(statement.Get(), 4, CurrentTimeMilliseconds());
    sqlite3_bind_int64(statement.Get(), 5, fields.charCount);
    sqlite3_bind_int64(statement.Get(), 6, fields.letterCount);
    sqlite3_bind_int64(statement.Get(), 7, fields.complexity);
    sqlite3_bind_int64(statement.Get(), 8, fields.anchors);
    sqlite3_bind_int64(statement.Get(), 9, fields.spell);

    int result = sqlite3_step(statement.Get());
    if (result != SQLITE_DONE)
    {
        LogSqliteError(database, L"insert memory entry", result);
        return false;
    }
    return true;
}

bool ExecuteDatabaseStatement(sqlite3* database, _In_z_ PCWSTR sql, _In_z_ PCWSTR operation)
{
    Statement statement;
    if (!PrepareMemoryStatement(database, sql, statement))
    {
        return false;
    }

    int result = sqlite3_step(statement.Get());
    if (result != SQLITE_DONE)
    {
        LogSqliteError(database, operation, result);
        return false;
    }
    return true;
}

std::optional<int64_t> UserVersion(sqlite3* database)
{
    Statement statement;
    if (!PrepareMemoryStatement(database, L"PRAGMA user_version;", statement))
    {
        return std::nullopt;
    }

    int result = sqlite3_step(statement.Get());
    if (result != SQLITE_ROW)
    {
        LogSqliteError(database, L"read user version", result);
        return std::nullopt;
    }
    return sqlite3_column_int64(statement.Get(), 0);
}

std::optional<bool> LegacyTableExists(sqlite3* database)
{
    static constexpr WCHAR sql[] =
        L"SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = 'core_memory' LIMIT 1;";

    Statement statement;
    if (!PrepareMemoryStatement(database, sql, statement))
    {
        return std::nullopt;
    }

    int result = sqlite3_step(statement.Get());
    if (result == SQLITE_ROW)
    {
        return true;
    }
    if (result == SQLITE_DONE)
    {
        return false;
    }

    LogSqliteError(database, L"check legacy memory table", result);
    return std::nullopt;
}

bool MigrateLegacyMemory(sqlite3* database)
{
    static constexpr WCHAR selectSql[] =
        L"SELECT word, romanization, frequency, latest FROM core_memory ORDER BY rowid;";
    static constexpr WCHAR insertSql[] =
        L"INSERT OR IGNORE INTO memory2608 "
        L"(word, romanization, frequency, latest, char_count, letter_count, complexity, anchors, spell) "
        L"VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);";

    Statement selectStatement;
    Statement insertStatement;
    if (!PrepareMemoryStatement(database, selectSql, selectStatement) ||
        !PrepareMemoryStatement(database, insertSql, insertStatement))
    {
        return false;
    }

    int result = SQLITE_OK;
    while ((result = sqlite3_step(selectStatement.Get())) == SQLITE_ROW)
    {
        std::wstring word = ColumnText(selectStatement.Get(), 0);
        std::wstring romanization = ColumnText(selectStatement.Get(), 1);
        int64_t frequency = sqlite3_column_int64(selectStatement.Get(), 2);
        int64_t latest = sqlite3_column_int64(selectStatement.Get(), 3);
        MemorySerialFields fields = DeriveSerialFields(word, romanization);

        sqlite3_reset(insertStatement.Get());
        sqlite3_clear_bindings(insertStatement.Get());
        BindText(insertStatement.Get(), 1, word);
        BindText(insertStatement.Get(), 2, romanization);
        sqlite3_bind_int64(insertStatement.Get(), 3, frequency);
        sqlite3_bind_int64(insertStatement.Get(), 4, latest);
        sqlite3_bind_int64(insertStatement.Get(), 5, fields.charCount);
        sqlite3_bind_int64(insertStatement.Get(), 6, fields.letterCount);
        sqlite3_bind_int64(insertStatement.Get(), 7, fields.complexity);
        sqlite3_bind_int64(insertStatement.Get(), 8, fields.anchors);
        sqlite3_bind_int64(insertStatement.Get(), 9, fields.spell);

        int insertResult = sqlite3_step(insertStatement.Get());
        if (insertResult != SQLITE_DONE)
        {
            LogSqliteError(database, L"migrate legacy memory row", insertResult);
            return false;
        }
    }

    if (result != SQLITE_DONE)
    {
        LogSqliteError(database, L"read legacy memory rows", result);
        return false;
    }
    return true;
}

bool PrepareMemorySchema(sqlite3* database)
{
    static constexpr PCWSTR schemaStatements[] =
    {
        L"CREATE TABLE IF NOT EXISTS memory2608 (id INTEGER PRIMARY KEY AUTOINCREMENT, word TEXT NOT NULL, romanization TEXT NOT NULL, frequency INTEGER NOT NULL, latest INTEGER NOT NULL, char_count INTEGER NOT NULL, letter_count INTEGER NOT NULL, complexity INTEGER NOT NULL, anchors INTEGER NOT NULL, spell INTEGER NOT NULL, UNIQUE (word, romanization));",
        L"CREATE INDEX IF NOT EXISTS ix2608_frequency ON memory2608 (frequency);",
        L"CREATE INDEX IF NOT EXISTS ix2608_anchors ON memory2608 (anchors, char_count, frequency DESC);",
        L"CREATE INDEX IF NOT EXISTS ix2608_spell ON memory2608 (spell, letter_count, complexity, frequency DESC);",
        L"CREATE INDEX IF NOT EXISTS ix2608_word ON memory2608 (word, frequency DESC);",
        L"CREATE INDEX IF NOT EXISTS ix2608_lexicon ON memory2608 (word, romanization);"
    };

    if (!ExecuteDatabaseStatement(database, L"BEGIN IMMEDIATE;", L"begin memory migration"))
    {
        return false;
    }

    bool succeeded = true;
    for (PCWSTR sql : schemaStatements)
    {
        if (!ExecuteDatabaseStatement(database, sql, L"create refined memory schema"))
        {
            succeeded = false;
            break;
        }
    }

    std::optional<int64_t> version;
    if (succeeded)
    {
        version = UserVersion(database);
        succeeded = version.has_value();
    }

    if (succeeded && *version < 2608)
    {
        std::optional<bool> hasLegacyTable = LegacyTableExists(database);
        succeeded = hasLegacyTable.has_value();
        if (succeeded && *hasLegacyTable)
        {
            succeeded = MigrateLegacyMemory(database);
        }
        if (succeeded)
        {
            succeeded = ExecuteDatabaseStatement(database, L"PRAGMA user_version = 2608;", L"set memory user version");
        }
    }

    if (succeeded)
    {
        succeeded = ExecuteDatabaseStatement(database, L"COMMIT;", L"commit memory migration");
    }
    if (!succeeded)
    {
        ExecuteDatabaseStatement(database, L"ROLLBACK;", L"roll back memory migration");
    }
    return succeeded;
}

} // namespace

namespace Ime {

InputMemory::InputMemory() :
    _database(nullptr),
    _isPrepared(false)
{
}

InputMemory::~InputMemory()
{
    Close();
}

bool InputMemory::Prepare()
{
    Close();

    std::wstring directory = InputMemoryDirectory();
    if (directory.empty())
    {
        Global::Log(L"InputMemory prepare failed: app data directory is unavailable");
        return false;
    }

    if (!CreateDirectoryW(directory.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
    {
        Global::Log(L"InputMemory prepare failed: unable to create directory: %s", directory.c_str());
        return false;
    }

    std::wstring databasePath = InputMemoryDatabasePath();
    Global::Log(L"InputMemory prepare start: path=%s", databasePath.c_str());

    std::string path = WideToUtf8(databasePath.c_str());
    if (path.empty())
    {
        Global::Log(L"InputMemory prepare failed: unable to convert path to UTF-8: %s", databasePath.c_str());
        return false;
    }

    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
    int result = sqlite3_open_v2(path.c_str(), &_database, flags, nullptr);
    if (result != SQLITE_OK)
    {
        LogError(L"open", result);
        Close();
        return false;
    }

    sqlite3_busy_timeout(_database, 250);

    if (!PrepareMemorySchema(_database))
    {
        Close();
        return false;
    }

    _isPrepared = true;
    Global::Log(L"InputMemory prepare success: path=%s", databasePath.c_str());
    return true;
}

bool InputMemory::IsPrepared() const
{
    return _database != nullptr && _isPrepared;
}

void InputMemory::Close()
{
    if (_database != nullptr)
    {
        Global::Log(L"InputMemory close");
        sqlite3_close_v2(_database);
        _database = nullptr;
    }
    _isPrepared = false;
}

bool InputMemory::Handle(const Lexicon& lexicon)
{
    if (!IsPrepared() || lexicon.IsNotCantonese())
    {
        return false;
    }

    int64_t id = 0;
    int64_t frequency = 0;
    if (FindMemoryEntry(_database, lexicon.text, lexicon.romanization, id, frequency))
    {
        return UpdateMemoryEntry(_database, id, frequency + 1);
    }
    return InsertMemoryEntry(_database, lexicon);
}

bool InputMemory::Forget(const Lexicon& lexicon)
{
    if (!IsPrepared() || lexicon.IsNotCantonese())
    {
        return false;
    }

    static constexpr WCHAR sql[] = L"DELETE FROM memory2608 WHERE word = ? AND romanization = ?;";

    Statement statement;
    if (!PrepareMemoryStatement(_database, sql, statement))
    {
        return false;
    }

    BindText(statement.Get(), 1, lexicon.text);
    BindText(statement.Get(), 2, lexicon.romanization);

    int result = sqlite3_step(statement.Get());
    if (result != SQLITE_DONE)
    {
        LogError(L"forget memory entry", result);
        return false;
    }
    return true;
}

bool InputMemory::DeleteAll()
{
    bool result = IsPrepared() && Execute(L"DELETE FROM memory2608;");
    Global::Log(L"InputMemory delete all: result=%d", result);
    return result;
}

std::vector<Lexicon> InputMemory::Suggest(
    const std::vector<VirtualInputKey>& keys,
    const Segmentation& segmentation,
    const Segmenter& segmenter) const
{
    if (!IsPrepared() || keys.empty())
    {
        return std::vector<Lexicon>();
    }

    bool hasApostrophe = ContainsApostrophe(keys);
    bool hasTone = ContainsToneInputKey(keys);
    if (!hasApostrophe && !hasTone)
    {
        return Search(_database, keys, segmentation, segmenter);
    }

    std::vector<VirtualInputKey> syllableKeys = SyllableKeys(keys);
    std::vector<Lexicon> candidates = Search(_database, syllableKeys, segmentation, segmenter);
    if (hasApostrophe && hasTone)
    {
        std::wstring inputText = TextFromKeys(keys);
        std::wstring text = ToneConverted(inputText);
        std::vector<Lexicon> qualified;
        for (const Lexicon& item : candidates)
        {
            if (StartsWith(text, item.romanization))
            {
                qualified.push_back(item.ReplacedInput(inputText));
            }
        }
        return qualified;
    }

    if (hasTone)
    {
        return FilterToneSuggestions(keys, candidates);
    }

    return FilterApostropheSuggestions(keys, candidates);
}

bool InputMemory::Execute(_In_z_ PCWSTR sql) const
{
    Statement statement;
    if (!PrepareStatement(sql, statement.Out()))
    {
        return false;
    }

    int result = sqlite3_step(statement.Get());
    if (result != SQLITE_DONE)
    {
        LogError(L"execute statement", result);
        return false;
    }
    return true;
}

bool InputMemory::PrepareStatement(_In_z_ PCWSTR sql, _Outptr_result_maybenull_ sqlite3_stmt** statement) const
{
    if (statement == nullptr)
    {
        return false;
    }
    *statement = nullptr;

    if (_database == nullptr)
    {
        Global::Log(L"InputMemory prepare failed: database is not open");
        return false;
    }

    int result = sqlite3_prepare16_v2(_database, sql, -1, statement, nullptr);
    if (result != SQLITE_OK)
    {
        LogError(L"prepare statement", result);
        return false;
    }
    return true;
}

void InputMemory::LogError(_In_z_ PCWSTR operation, int result) const
{
    LogSqliteError(_database, operation, result);
}

} // namespace Ime
