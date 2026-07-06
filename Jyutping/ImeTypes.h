#pragma once

#include "stdafx.h"
#include "VirtualInputKey.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Ime {

enum class LexiconType
{
    Cantonese,
    Text,
    Emoji,
    Symbol,
    Composed
};

enum class KeyboardCase
{
    Lowercased = 1,
    Uppercased = 2,
    CapsLocked = 3
};

struct BasicInputEvent
{
    VirtualInputKey key;
    KeyboardCase keyCase = KeyboardCase::Lowercased;

    BasicInputEvent() = default;
    BasicInputEvent(const VirtualInputKey& inputKey, KeyboardCase inputCase);
    BasicInputEvent(const VirtualInputKey& inputKey, bool isCapitalized);

    bool IsCapitalized() const;
};

struct Lexicon
{
    LexiconType type = LexiconType::Cantonese;
    std::wstring text;
    std::wstring romanization;
    std::wstring input;
    size_t inputCount = 0;
    std::wstring mark;
    int64_t number = 0;
    std::optional<std::wstring> attached;

    Lexicon() = default;
    Lexicon(
        LexiconType inputType,
        std::wstring inputText,
        std::wstring inputRomanization,
        std::wstring userInput,
        std::optional<std::wstring> inputMark = std::nullopt,
        int64_t inputNumber = 0,
        std::optional<std::wstring> inputAttached = std::nullopt);

    static Lexicon Cantonese(
        std::wstring text,
        std::wstring romanization,
        std::wstring input,
        std::optional<std::wstring> mark = std::nullopt,
        int64_t number = 0);

    static Lexicon PlainText(std::wstring input, std::wstring text);
    static Lexicon EmojiOrSymbol(
        std::wstring text,
        std::wstring cantonese,
        std::wstring romanization,
        std::wstring input,
        bool isEmoji);

    bool IsCantonese() const;
    bool IsNotCantonese() const;
    bool IsEmojiOrSymbol() const;
    bool IsCompound() const;
    bool IsInputMemory() const;
    bool IsIdealInputMemory() const;
    bool IsNotIdealInputMemory() const;

    Lexicon ReplacedInput(std::wstring newInput) const;
};

bool operator==(const Lexicon& left, const Lexicon& right);
bool operator!=(const Lexicon& left, const Lexicon& right);
bool operator<(const Lexicon& left, const Lexicon& right);

struct Syllable
{
    int64_t aliasCode = 0;
    int64_t originCode = 0;
    std::vector<VirtualInputKey> alias;
    std::vector<VirtualInputKey> origin;

    Syllable() = default;
    Syllable(int64_t inputAliasCode, int64_t inputOriginCode);

    std::wstring AliasText() const;
    std::wstring OriginText() const;
};

bool operator==(const Syllable& left, const Syllable& right);
bool operator!=(const Syllable& left, const Syllable& right);
bool operator<(const Syllable& left, const Syllable& right);

using Scheme = std::vector<Syllable>;
using Segmentation = std::vector<Scheme>;

bool IsLowercaseBasicLatinLetter(WCHAR character);
bool IsUppercaseBasicLatinLetter(WCHAR character);
bool IsBasicLatinLetter(WCHAR character);
bool IsCantoneseToneDigit(WCHAR character);

int32_t HashCode(std::wstring_view text);
std::wstring ToneConverted(std::wstring_view text);
std::wstring MarkFormatted(std::wstring_view text);
std::wstring StrippedTones(std::wstring_view text);
std::wstring StrippedSpaces(std::wstring_view text);
std::wstring ToneDigitOnly(std::wstring_view text);
std::wstring LatinLetterOnly(std::wstring_view text);

int64_t Radix100Combined(const std::vector<int>& codes);
std::optional<int64_t> CharCodeFromText(std::wstring_view text);
std::optional<int64_t> AnchorsCodeFromText(std::wstring_view text);
std::optional<std::wstring> SymbolTextFromCodePoints(std::wstring_view codePoints);

std::vector<VirtualInputKey> InputKeysFromCode(int64_t code);
std::vector<VirtualInputKey> InputKeysFromText(std::wstring_view text);
std::wstring TextFromKeys(const std::vector<VirtualInputKey>& keys);
std::wstring TextFromEvents(const std::vector<BasicInputEvent>& events);
std::wstring PreviewMarkNormalized(const std::vector<BasicInputEvent>& events);

int64_t CombinedCode(const std::vector<VirtualInputKey>& keys);
int64_t AnchorsCode(const std::vector<VirtualInputKey>& keys);
std::vector<VirtualInputKey> SyllableKeys(const std::vector<VirtualInputKey>& keys);

size_t SchemeLength(const Scheme& scheme);
std::wstring SchemeAliasText(const Scheme& scheme);
std::wstring SchemeOriginText(const Scheme& scheme);
std::vector<VirtualInputKey> SchemeAliasAnchors(const Scheme& scheme);
std::vector<VirtualInputKey> SchemeOriginAnchors(const Scheme& scheme);
std::wstring SchemeAliasAnchorsText(const Scheme& scheme);
std::wstring SchemeOriginAnchorsText(const Scheme& scheme);
std::wstring SchemeMark(const Scheme& scheme);
std::wstring SchemeSyllableText(const Scheme& scheme);

std::optional<Lexicon> Concatenate(const Lexicon& left, const Lexicon& right);
std::optional<Lexicon> JoinLexicons(const std::vector<Lexicon>& lexicons);

} // namespace Ime
