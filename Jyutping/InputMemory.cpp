#include "Private.h"
#include "InputMemory.h"
#include "Logger.h"

#include <winsqlite/winsqlite3.h>

#include <algorithm>
#include <chrono>
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

std::vector<MemoryLexicon> SortedMemory(std::vector<MemoryLexicon> items)
{
    std::sort(items.begin(), items.end(), [](const MemoryLexicon& left, const MemoryLexicon& right)
    {
        if (left.inputCount != right.inputCount)
        {
            return left.inputCount > right.inputCount;
        }
        if (left.frequency != right.frequency)
        {
            return left.frequency > right.frequency;
        }
        return left.latest > right.latest;
    });
    return items;
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

std::wstring ReplaceYWithJ(std::wstring text)
{
    std::replace(text.begin(), text.end(), L'y', L'j');
    return text;
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

std::wstring RomanizationAnchorText(std::wstring_view romanization)
{
    std::vector<std::wstring> syllables = Split(romanization, L' ');

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

std::vector<MemoryLexicon> ShortcutMatch(sqlite3* database, std::wstring_view text, const std::wstring& input, int64_t limit = 100)
{
    static constexpr WCHAR sql[] =
        L"SELECT word, romanization, frequency, latest FROM core_memory WHERE shortcut = ? ORDER BY frequency DESC LIMIT ?;";

    Statement statement;
    if (!PrepareMemoryStatement(database, sql, statement))
    {
        return std::vector<MemoryLexicon>();
    }

    std::wstring queryText = ReplaceYWithJ(std::wstring(text));
    sqlite3_bind_int64(statement.Get(), 1, Ime::HashCode(queryText));
    sqlite3_bind_int64(statement.Get(), 2, limit);
    return ReadMemoryRows(statement.Get(), database, L"query memory shortcut", input, input);
}

std::vector<MemoryLexicon> SpellMatch(sqlite3* database, std::wstring_view text, const std::wstring& input, std::optional<std::wstring> mark = std::nullopt, int64_t limit = 100)
{
    static constexpr WCHAR sql[] =
        L"SELECT word, romanization, frequency, latest FROM core_memory WHERE spell = ? ORDER BY frequency DESC LIMIT ?;";

    Statement statement;
    if (!PrepareMemoryStatement(database, sql, statement))
    {
        return std::vector<MemoryLexicon>();
    }

    sqlite3_bind_int64(statement.Get(), 1, Ime::HashCode(text));
    sqlite3_bind_int64(statement.Get(), 2, limit);
    return ReadMemoryRows(statement.Get(), database, L"query memory spell", input, mark);
}

std::vector<MemoryLexicon> StrictMatch(sqlite3* database, int32_t spell, int32_t shortcut, const std::wstring& input, std::optional<std::wstring> mark = std::nullopt, int64_t limit = 100)
{
    static constexpr WCHAR sql[] =
        L"SELECT word, romanization, frequency, latest FROM core_memory WHERE spell = ? AND shortcut = ? ORDER BY frequency DESC LIMIT ?;";

    Statement statement;
    if (!PrepareMemoryStatement(database, sql, statement))
    {
        return std::vector<MemoryLexicon>();
    }

    sqlite3_bind_int64(statement.Get(), 1, spell);
    sqlite3_bind_int64(statement.Get(), 2, shortcut);
    sqlite3_bind_int64(statement.Get(), 3, limit);
    return ReadMemoryRows(statement.Get(), database, L"query memory strict", input, mark);
}

std::vector<MemoryLexicon> Perform(sqlite3* database, const Ime::Scheme& scheme, int64_t limit = 5)
{
    int32_t spell = Ime::HashCode(Ime::SchemeOriginText(scheme));
    int32_t shortcut = Ime::HashCode(Ime::SchemeOriginAnchorsText(scheme));
    return StrictMatch(database, spell, shortcut, Ime::SchemeAliasText(scheme), Ime::SchemeMark(scheme), limit);
}

std::vector<Ime::Lexicon> Query(sqlite3* database, const Ime::Segmentation& segmentation, const std::vector<Ime::Scheme>& idealSchemes)
{
    if (segmentation.empty())
    {
        return std::vector<Ime::Lexicon>();
    }

    std::vector<MemoryLexicon> queried;
    if (idealSchemes.empty())
    {
        for (const Ime::Scheme& scheme : segmentation)
        {
            Append(queried, Perform(database, scheme));
        }
    }
    else
    {
        for (const Ime::Scheme& scheme : idealSchemes)
        {
            if (scheme.size() <= 1)
            {
                continue;
            }
            for (size_t count = scheme.size() - 1; count > 0; count--)
            {
                Append(queried, Perform(database, PrefixScheme(scheme, count)));
            }
        }
    }

    return ToLexicons(First(PeculiarSorted(queried), 6), -2);
}

std::vector<Ime::Lexicon> Search(sqlite3* database, const std::vector<VirtualInputKey>& keys, const Ime::Segmentation& segmentation, const Ime::Segmenter& segmenter)
{
    size_t inputLength = keys.size();
    std::wstring text = Ime::TextFromKeys(keys);

    std::vector<MemoryLexicon> fullMatched = SpellMatch(database, text, text);

    std::vector<Ime::Scheme> idealSchemes;
    for (const Ime::Scheme& scheme : segmentation)
    {
        if (Ime::SchemeLength(scheme) == inputLength)
        {
            idealSchemes.push_back(scheme);
        }
    }

    std::vector<MemoryLexicon> idealQueried;
    for (const Ime::Scheme& scheme : idealSchemes)
    {
        int32_t spell = Ime::HashCode(Ime::SchemeOriginText(scheme));
        int32_t shortcut = Ime::HashCode(Ime::SchemeOriginAnchorsText(scheme));
        Append(idealQueried, StrictMatch(database, spell, shortcut, text, Ime::SchemeMark(scheme)));
    }

    std::vector<Ime::Lexicon> queried = Query(database, segmentation, idealSchemes);
    if (!fullMatched.empty() || !idealQueried.empty())
    {
        std::vector<MemoryLexicon> ideal;
        Append(ideal, fullMatched);
        Append(ideal, idealQueried);

        std::vector<Ime::Lexicon> result = ToLexicons(RegularSorted(ideal), -1);
        Append(result, queried);
        return result;
    }

    int64_t shortcutLimit = (segmentation.empty() || segmentation.front().empty()) ? 100 : 5;
    std::vector<MemoryLexicon> shortcuts = ShortcutMatch(database, text, text, shortcutLimit);
    if (!shortcuts.empty())
    {
        std::vector<Ime::Lexicon> result = ToLexicons(RegularSorted(shortcuts, true), -1);
        Append(result, queried);
        return result;
    }

    if (inputLength <= 2 || inputLength >= 25)
    {
        return queried;
    }

    bool shouldPartiallyMatch = idealSchemes.empty() ||
        keys.back() == VirtualInputKey::letterM ||
        keys.front() == VirtualInputKey::letterM;
    if (!shouldPartiallyMatch)
    {
        return queried;
    }

    std::vector<MemoryLexicon> prefixMatched;
    for (const Ime::Scheme& scheme : segmentation)
    {
        if (scheme.empty())
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

        std::wstring conjoinedText = Ime::TextFromKeys(conjoined);
        std::wstring schemeSyllableText = Ime::SchemeSyllableText(scheme);
        std::wstring mark = Ime::SchemeMark(scheme) + L" " + Ime::TextFromKeys(tail);
        std::wstring tailAsAnchorText = TailAnchorText(tail);

        for (const MemoryLexicon& item : ShortcutMatch(database, conjoinedText, conjoinedText))
        {
            std::wstring toneFreeRomanization = Ime::StrippedTones(item.romanization);
            if (!StartsWith(toneFreeRomanization, schemeSyllableText))
            {
                continue;
            }
            if (SuffixAnchorText(toneFreeRomanization, schemeSyllableText.size()) == tailAsAnchorText)
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

        std::wstring anchorsText = Ime::TextFromKeys(schemeAnchors);
        anchorsText.append(tail.front().text);

        std::wstring syllableText = schemeSyllableText + L" " + transformedTailText;
        for (const MemoryLexicon& item : ShortcutMatch(database, anchorsText, anchorsText))
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
        std::vector<VirtualInputKey> leadingKeys = Prefix(keys, number);
        std::wstring leadingText = Ime::TextFromKeys(leadingKeys);
        for (const MemoryLexicon& item : ShortcutMatch(database, leadingText, leadingText))
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
    Append(result, queried);
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
    static constexpr WCHAR sql[] = L"SELECT id, frequency FROM core_memory WHERE word = ? AND romanization = ? LIMIT 1;";

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
    static constexpr WCHAR sql[] = L"UPDATE core_memory SET frequency = ?, latest = ? WHERE id = ?;";

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
        L"INSERT INTO core_memory (word, romanization, frequency, latest, shortcut, spell) VALUES (?, ?, ?, ?, ?, ?);";

    Statement statement;
    if (!PrepareMemoryStatement(database, sql, statement))
    {
        return false;
    }

    std::wstring anchorText = RomanizationAnchorText(lexicon.romanization);
    std::wstring spellText = Ime::LatinLetterOnly(lexicon.romanization);

    BindText(statement.Get(), 1, lexicon.text);
    BindText(statement.Get(), 2, lexicon.romanization);
    sqlite3_bind_int64(statement.Get(), 3, 1);
    sqlite3_bind_int64(statement.Get(), 4, CurrentTimeMilliseconds());
    sqlite3_bind_int64(statement.Get(), 5, Ime::HashCode(anchorText));
    sqlite3_bind_int64(statement.Get(), 6, Ime::HashCode(spellText));

    int result = sqlite3_step(statement.Get());
    if (result != SQLITE_DONE)
    {
        LogSqliteError(database, L"insert memory entry", result);
        return false;
    }
    return true;
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

    static constexpr PCWSTR statements[] =
    {
        L"CREATE TABLE IF NOT EXISTS core_memory (id INTEGER PRIMARY KEY AUTOINCREMENT, word TEXT NOT NULL, romanization TEXT NOT NULL, frequency INTEGER NOT NULL, latest INTEGER NOT NULL, shortcut INTEGER NOT NULL, spell INTEGER NOT NULL, UNIQUE (word, romanization));",
        L"CREATE INDEX IF NOT EXISTS ix_core_memory_frequency ON core_memory (frequency);",
        L"CREATE INDEX IF NOT EXISTS ix_core_memory_shortcut ON core_memory (shortcut, frequency DESC);",
        L"CREATE INDEX IF NOT EXISTS ix_core_memory_spell ON core_memory (spell, frequency DESC);",
        L"CREATE INDEX IF NOT EXISTS ix_core_memory_strict ON core_memory (spell, shortcut, frequency DESC);"
    };

    for (PCWSTR sql : statements)
    {
        if (!Execute(sql))
        {
            Close();
            return false;
        }
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

    static constexpr WCHAR sql[] = L"DELETE FROM core_memory WHERE word = ? AND romanization = ?;";

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
    bool result = IsPrepared() && Execute(L"DELETE FROM core_memory;");
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
