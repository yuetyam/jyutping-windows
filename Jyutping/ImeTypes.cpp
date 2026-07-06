#include "ImeTypes.h"

#include <algorithm>
#include <cwctype>

namespace {

constexpr int compoundNumberStep = 1000000;

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

std::optional<int> LetterCode(WCHAR character)
{
    if (L'a' <= character && character <= L'z')
    {
        return 20 + static_cast<int>(character - L'a');
    }
    return std::nullopt;
}

std::optional<int> NumberCode(WCHAR character)
{
    if (L'0' <= character && character <= L'9')
    {
        return 10 + static_cast<int>(character - L'0');
    }
    return std::nullopt;
}

std::optional<int> VirtualInputCode(WCHAR character)
{
    if (auto letterCode = LetterCode(character))
    {
        return letterCode;
    }
    return NumberCode(character);
}

WCHAR UppercaseAscii(WCHAR character)
{
    if (L'a' <= character && character <= L'z')
    {
        return static_cast<WCHAR>(character - L'a' + L'A');
    }
    return character;
}

std::optional<uint32_t> HexDigitValue(WCHAR character)
{
    if (L'0' <= character && character <= L'9')
    {
        return static_cast<uint32_t>(character - L'0');
    }
    if (L'a' <= character && character <= L'f')
    {
        return static_cast<uint32_t>(character - L'a' + 10);
    }
    if (L'A' <= character && character <= L'F')
    {
        return static_cast<uint32_t>(character - L'A' + 10);
    }
    return std::nullopt;
}

std::optional<uint32_t> CodePointFromHex(std::wstring_view text)
{
    if (text.empty())
    {
        return std::nullopt;
    }

    uint32_t value = 0;
    for (WCHAR character : text)
    {
        std::optional<uint32_t> digit = HexDigitValue(character);
        if (!digit)
        {
            return std::nullopt;
        }
        if (value > (0x10FFFF - *digit) / 16)
        {
            return std::nullopt;
        }
        value = value * 16 + *digit;
    }

    if (value > 0x10FFFF || (0xD800 <= value && value <= 0xDFFF))
    {
        return std::nullopt;
    }
    return value;
}

void AppendCodePoint(std::wstring& result, uint32_t codePoint)
{
    if (codePoint <= 0xFFFF)
    {
        result.push_back(static_cast<WCHAR>(codePoint));
        return;
    }

    codePoint -= 0x10000;
    result.push_back(static_cast<WCHAR>(0xD800 + (codePoint >> 10)));
    result.push_back(static_cast<WCHAR>(0xDC00 + (codePoint & 0x3FF)));
}

} // namespace

namespace Ime {

BasicInputEvent::BasicInputEvent(const VirtualInputKey& inputKey, KeyboardCase inputCase) :
    key(inputKey),
    keyCase(inputCase)
{
}

BasicInputEvent::BasicInputEvent(const VirtualInputKey& inputKey, bool isCapitalized) :
    key(inputKey),
    keyCase(isCapitalized ? KeyboardCase::Uppercased : KeyboardCase::Lowercased)
{
}

bool BasicInputEvent::IsCapitalized() const
{
    return keyCase != KeyboardCase::Lowercased;
}

Lexicon::Lexicon(
    LexiconType inputType,
    std::wstring inputText,
    std::wstring inputRomanization,
    std::wstring userInput,
    std::optional<std::wstring> inputMark,
    int64_t inputNumber,
    std::optional<std::wstring> inputAttached) :
    type(inputType),
    text(std::move(inputText)),
    romanization(std::move(inputRomanization)),
    input(std::move(userInput)),
    inputCount(input.size()),
    mark(inputMark.value_or(input)),
    number(inputNumber),
    attached(std::move(inputAttached))
{
}

Lexicon Lexicon::Cantonese(
    std::wstring text,
    std::wstring romanization,
    std::wstring input,
    std::optional<std::wstring> mark,
    int64_t number)
{
    return Lexicon(LexiconType::Cantonese, std::move(text), std::move(romanization), std::move(input), std::move(mark), number);
}

Lexicon Lexicon::PlainText(std::wstring input, std::wstring text)
{
    std::wstring romanization = input;
    return Lexicon(LexiconType::Text, std::move(text), std::move(romanization), std::move(input));
}

Lexicon Lexicon::EmojiOrSymbol(
    std::wstring text,
    std::wstring cantonese,
    std::wstring romanization,
    std::wstring input,
    bool isEmoji)
{
    LexiconType type = isEmoji ? LexiconType::Emoji : LexiconType::Symbol;
    return Lexicon(type, std::move(text), std::move(romanization), std::move(input), std::nullopt, 0, std::move(cantonese));
}

bool Lexicon::IsCantonese() const
{
    return type == LexiconType::Cantonese;
}

bool Lexicon::IsNotCantonese() const
{
    return !IsCantonese();
}

bool Lexicon::IsEmojiOrSymbol() const
{
    return type == LexiconType::Emoji || type == LexiconType::Symbol;
}

bool Lexicon::IsCompound() const
{
    return number > compoundNumberStep;
}

bool Lexicon::IsInputMemory() const
{
    return number < 0;
}

bool Lexicon::IsIdealInputMemory() const
{
    return number == -1;
}

bool Lexicon::IsNotIdealInputMemory() const
{
    return number == -2;
}

Lexicon Lexicon::ReplacedInput(std::wstring newInput) const
{
    return Lexicon(type, text, romanization, std::move(newInput), mark, number, attached);
}

bool operator==(const Lexicon& left, const Lexicon& right)
{
    if (left.type != right.type)
    {
        return false;
    }
    if (left.IsCantonese() && right.IsCantonese())
    {
        return left.text == right.text && left.romanization == right.romanization;
    }
    return left.text == right.text;
}

bool operator!=(const Lexicon& left, const Lexicon& right)
{
    return !(left == right);
}

bool operator<(const Lexicon& left, const Lexicon& right)
{
    if (left.inputCount != right.inputCount)
    {
        return left.inputCount > right.inputCount;
    }
    return left.number < right.number;
}

Syllable::Syllable(int64_t inputAliasCode, int64_t inputOriginCode) :
    aliasCode(inputAliasCode),
    originCode(inputOriginCode),
    alias(InputKeysFromCode(inputAliasCode)),
    origin(InputKeysFromCode(inputOriginCode))
{
}

std::wstring Syllable::AliasText() const
{
    return TextFromKeys(alias);
}

std::wstring Syllable::OriginText() const
{
    return TextFromKeys(origin);
}

bool operator==(const Syllable& left, const Syllable& right)
{
    return left.aliasCode == right.aliasCode && left.originCode == right.originCode;
}

bool operator!=(const Syllable& left, const Syllable& right)
{
    return !(left == right);
}

bool operator<(const Syllable& left, const Syllable& right)
{
    if (left.aliasCode != right.aliasCode)
    {
        return left.aliasCode < right.aliasCode;
    }
    return left.originCode < right.originCode;
}

bool IsLowercaseBasicLatinLetter(WCHAR character)
{
    return L'a' <= character && character <= L'z';
}

bool IsUppercaseBasicLatinLetter(WCHAR character)
{
    return L'A' <= character && character <= L'Z';
}

bool IsBasicLatinLetter(WCHAR character)
{
    return IsLowercaseBasicLatinLetter(character) || IsUppercaseBasicLatinLetter(character);
}

bool IsCantoneseToneDigit(WCHAR character)
{
    return L'1' <= character && character <= L'6';
}

int32_t HashCode(std::wstring_view text)
{
    uint32_t result = 0;
    for (WCHAR character : text)
    {
        result = result * 31u + static_cast<uint16_t>(character);
    }
    return static_cast<int32_t>(result);
}

std::wstring ToneConverted(std::wstring_view text)
{
    std::wstring result;
    result.reserve(text.size());

    size_t index = 0;
    while (index < text.size())
    {
        WCHAR character = text[index];
        WCHAR next = (index + 1 < text.size()) ? text[index + 1] : L'\0';
        if (character == L'v' && next == L'v')
        {
            result.push_back(L'4');
            index += 2;
        }
        else if (character == L'x' && next == L'x')
        {
            result.push_back(L'5');
            index += 2;
        }
        else if (character == L'q' && next == L'q')
        {
            result.push_back(L'6');
            index += 2;
        }
        else if (character == L'v')
        {
            result.push_back(L'1');
            index++;
        }
        else if (character == L'x')
        {
            result.push_back(L'2');
            index++;
        }
        else if (character == L'q')
        {
            result.push_back(L'3');
            index++;
        }
        else
        {
            result.push_back(character);
            index++;
        }
    }
    return result;
}

std::wstring MarkFormatted(std::wstring_view text)
{
    std::wstring result;
    result.reserve(text.size() * 2);
    for (WCHAR character : text)
    {
        result.push_back(character);
        if (!IsBasicLatinLetter(character))
        {
            result.push_back(L' ');
        }
    }
    return result;
}

std::wstring StrippedTones(std::wstring_view text)
{
    std::wstring result;
    result.reserve(text.size());
    for (WCHAR character : text)
    {
        if (!IsCantoneseToneDigit(character))
        {
            result.push_back(character);
        }
    }
    return result;
}

std::wstring StrippedSpaces(std::wstring_view text)
{
    std::wstring result;
    result.reserve(text.size());
    for (WCHAR character : text)
    {
        if (character != L' ')
        {
            result.push_back(character);
        }
    }
    return result;
}

std::wstring ToneDigitOnly(std::wstring_view text)
{
    std::wstring result;
    result.reserve(text.size());
    for (WCHAR character : text)
    {
        if (IsCantoneseToneDigit(character))
        {
            result.push_back(character);
        }
    }
    return result;
}

std::wstring LatinLetterOnly(std::wstring_view text)
{
    std::wstring result;
    result.reserve(text.size());
    for (WCHAR character : text)
    {
        if (IsBasicLatinLetter(character))
        {
            result.push_back(character);
        }
    }
    return result;
}

int64_t Radix100Combined(const std::vector<int>& codes)
{
    if (codes.size() >= 10)
    {
        return 0;
    }

    int64_t result = 0;
    for (int code : codes)
    {
        result = result * 100 + code;
    }
    return result;
}

std::optional<int64_t> CharCodeFromText(std::wstring_view text)
{
    if (text.size() >= 10)
    {
        return std::nullopt;
    }

    std::vector<int> codes;
    codes.reserve(text.size());
    for (WCHAR character : text)
    {
        auto code = LetterCode(character);
        if (!code)
        {
            return std::nullopt;
        }
        codes.push_back(*code);
    }
    return Radix100Combined(codes);
}

std::optional<int64_t> AnchorsCodeFromText(std::wstring_view text)
{
    if (text.size() >= 10)
    {
        return std::nullopt;
    }

    std::vector<int> codes;
    codes.reserve(text.size());
    for (WCHAR character : text)
    {
        auto code = LetterCode(character);
        if (!code)
        {
            return std::nullopt;
        }
        codes.push_back((*code == VirtualInputKey::letterY.code) ? VirtualInputKey::letterJ.code : *code);
    }

    int64_t result = Radix100Combined(codes);
    if (result == 0)
    {
        return std::nullopt;
    }
    return result;
}

std::optional<std::wstring> SymbolTextFromCodePoints(std::wstring_view codePoints)
{
    if (codePoints.empty() || codePoints.front() == L'.' || codePoints.back() == L'.')
    {
        return std::nullopt;
    }

    std::wstring result;
    size_t start = 0;
    while (start < codePoints.size())
    {
        size_t separator = codePoints.find(L'.', start);
        size_t end = (separator == std::wstring_view::npos) ? codePoints.size() : separator;
        std::optional<uint32_t> codePoint = CodePointFromHex(codePoints.substr(start, end - start));
        if (!codePoint)
        {
            return std::nullopt;
        }

        AppendCodePoint(result, *codePoint);
        if (separator == std::wstring_view::npos)
        {
            break;
        }
        start = separator + 1;
    }

    return result.empty() ? std::nullopt : std::optional<std::wstring>(result);
}

std::vector<VirtualInputKey> InputKeysFromCode(int64_t code)
{
    std::vector<int> codes;
    int64_t number = code;
    while (number > 0)
    {
        codes.push_back(static_cast<int>(number % 100));
        number /= 100;
    }
    std::reverse(codes.begin(), codes.end());

    std::vector<VirtualInputKey> keys;
    keys.reserve(codes.size());
    for (int inputCode : codes)
    {
        VirtualInputKey key;
        if (VirtualInputKey::MatchInputKeyForCode(inputCode, &key))
        {
            keys.push_back(key);
        }
    }
    return keys;
}

std::vector<VirtualInputKey> InputKeysFromText(std::wstring_view text)
{
    std::vector<VirtualInputKey> keys;
    keys.reserve(text.size());
    for (WCHAR character : text)
    {
        VirtualInputKey key;
        if (!VirtualInputKey::MatchInputKeyForCharacter(character, &key))
        {
            return std::vector<VirtualInputKey>();
        }
        keys.push_back(key);
    }
    return keys;
}

std::wstring TextFromKeys(const std::vector<VirtualInputKey>& keys)
{
    std::wstring result;
    result.reserve(keys.size());
    for (const VirtualInputKey& key : keys)
    {
        result.append(key.text);
    }
    return result;
}

std::wstring TextFromEvents(const std::vector<BasicInputEvent>& events)
{
    std::wstring result;
    result.reserve(events.size());
    for (const BasicInputEvent& event : events)
    {
        result.push_back(event.IsCapitalized() ? UppercaseAscii(event.key.character) : event.key.character);
    }
    return result;
}

std::wstring PreviewMarkNormalized(const std::vector<BasicInputEvent>& events)
{
    std::wstring result;
    result.reserve(events.size() * 2);

    size_t index = 0;
    while (index < events.size())
    {
        const BasicInputEvent& event = events[index];
        bool isPaired = (index + 1 < events.size()) && (event.key == events[index + 1].key);
        if (isPaired)
        {
            const VirtualInputKey* replacement = nullptr;
            if (event.key == VirtualInputKey::letterV)
            {
                replacement = &VirtualInputKey::number4;
            }
            else if (event.key == VirtualInputKey::letterX)
            {
                replacement = &VirtualInputKey::number5;
            }
            else if (event.key == VirtualInputKey::letterQ)
            {
                replacement = &VirtualInputKey::number6;
            }

            if (replacement != nullptr)
            {
                result.push_back(replacement->character);
                index += 2;
                if (index < events.size())
                {
                    result.push_back(L' ');
                }
                continue;
            }
        }

        const VirtualInputKey* matched = nullptr;
        if (event.key == VirtualInputKey::letterV)
        {
            matched = &VirtualInputKey::number1;
        }
        else if (event.key == VirtualInputKey::letterX)
        {
            matched = &VirtualInputKey::number2;
        }
        else if (event.key == VirtualInputKey::letterQ)
        {
            matched = &VirtualInputKey::number3;
        }
        else if (!event.key.IsLetter())
        {
            matched = &event.key;
        }

        index++;
        if (matched != nullptr)
        {
            result.push_back(matched->character);
            if (index < events.size())
            {
                result.push_back(L' ');
            }
        }
        else
        {
            result.push_back(event.IsCapitalized() ? UppercaseAscii(event.key.character) : event.key.character);
        }
    }
    return result;
}

int64_t CombinedCode(const std::vector<VirtualInputKey>& keys)
{
    if (keys.size() >= 10)
    {
        return 0;
    }

    int64_t result = 0;
    for (const VirtualInputKey& key : keys)
    {
        result = result * 100 + key.code;
    }
    return result;
}

int64_t AnchorsCode(const std::vector<VirtualInputKey>& keys)
{
    if (keys.size() >= 10)
    {
        return 0;
    }

    int64_t result = 0;
    for (const VirtualInputKey& key : keys)
    {
        const VirtualInputKey& inputKey = key.IsYLetterY() ? VirtualInputKey::letterJ : key;
        result = result * 100 + inputKey.code;
    }
    return result;
}

std::vector<VirtualInputKey> SyllableKeys(const std::vector<VirtualInputKey>& keys)
{
    std::vector<VirtualInputKey> result;
    result.reserve(keys.size());
    for (const VirtualInputKey& key : keys)
    {
        if (key.IsSyllableLetter())
        {
            result.push_back(key);
        }
    }
    return result;
}

size_t SchemeLength(const Scheme& scheme)
{
    size_t result = 0;
    for (const Syllable& syllable : scheme)
    {
        result += syllable.alias.size();
    }
    return result;
}

std::wstring SchemeAliasText(const Scheme& scheme)
{
    std::wstring result;
    for (const Syllable& syllable : scheme)
    {
        result.append(syllable.AliasText());
    }
    return result;
}

std::wstring SchemeOriginText(const Scheme& scheme)
{
    std::wstring result;
    for (const Syllable& syllable : scheme)
    {
        result.append(syllable.OriginText());
    }
    return result;
}

std::vector<VirtualInputKey> SchemeAliasAnchors(const Scheme& scheme)
{
    std::vector<VirtualInputKey> result;
    result.reserve(scheme.size());
    for (const Syllable& syllable : scheme)
    {
        if (!syllable.alias.empty())
        {
            result.push_back(syllable.alias.front());
        }
    }
    return result;
}

std::vector<VirtualInputKey> SchemeOriginAnchors(const Scheme& scheme)
{
    std::vector<VirtualInputKey> result;
    result.reserve(scheme.size());
    for (const Syllable& syllable : scheme)
    {
        if (!syllable.origin.empty())
        {
            result.push_back(syllable.origin.front());
        }
    }
    return result;
}

std::wstring SchemeAliasAnchorsText(const Scheme& scheme)
{
    return TextFromKeys(SchemeAliasAnchors(scheme));
}

std::wstring SchemeOriginAnchorsText(const Scheme& scheme)
{
    return TextFromKeys(SchemeOriginAnchors(scheme));
}

std::wstring SchemeMark(const Scheme& scheme)
{
    std::vector<std::wstring> texts;
    texts.reserve(scheme.size());
    for (const Syllable& syllable : scheme)
    {
        texts.push_back(syllable.AliasText());
    }
    return JoinTexts(texts, L' ');
}

std::wstring SchemeSyllableText(const Scheme& scheme)
{
    std::vector<std::wstring> texts;
    texts.reserve(scheme.size());
    for (const Syllable& syllable : scheme)
    {
        texts.push_back(syllable.OriginText());
    }
    return JoinTexts(texts, L' ');
}

std::optional<Lexicon> Concatenate(const Lexicon& left, const Lexicon& right)
{
    if (left.IsNotCantonese() || right.IsNotCantonese())
    {
        return std::nullopt;
    }

    return Lexicon::Cantonese(
        left.text + right.text,
        left.romanization + L" " + right.romanization,
        left.input + right.input,
        left.mark + L" " + right.mark,
        (left.number + compoundNumberStep) + (right.number + compoundNumberStep));
}

std::optional<Lexicon> JoinLexicons(const std::vector<Lexicon>& lexicons)
{
    if (lexicons.empty())
    {
        return std::nullopt;
    }

    std::wstring text;
    std::vector<std::wstring> romanizations;
    std::wstring input;
    std::vector<std::wstring> marks;
    int64_t number = 0;

    romanizations.reserve(lexicons.size());
    marks.reserve(lexicons.size());
    for (const Lexicon& lexicon : lexicons)
    {
        if (lexicon.IsNotCantonese())
        {
            return std::nullopt;
        }
        text.append(lexicon.text);
        romanizations.push_back(lexicon.romanization);
        input.append(lexicon.input);
        marks.push_back(lexicon.mark);
        number += lexicon.number + compoundNumberStep;
    }

    return Lexicon::Cantonese(text, JoinTexts(romanizations, L' '), input, JoinTexts(marks, L' '), number);
}

} // namespace Ime
