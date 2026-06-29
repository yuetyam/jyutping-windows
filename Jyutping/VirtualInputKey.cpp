#include "VirtualInputKey.h"

const VirtualInputKey VirtualInputKey::number0(L'0', L"0", 10, '0');
const VirtualInputKey VirtualInputKey::number1(L'1', L"1", 11, '1');
const VirtualInputKey VirtualInputKey::number2(L'2', L"2", 12, '2');
const VirtualInputKey VirtualInputKey::number3(L'3', L"3", 13, '3');
const VirtualInputKey VirtualInputKey::number4(L'4', L"4", 14, '4');
const VirtualInputKey VirtualInputKey::number5(L'5', L"5", 15, '5');
const VirtualInputKey VirtualInputKey::number6(L'6', L"6", 16, '6');
const VirtualInputKey VirtualInputKey::number7(L'7', L"7", 17, '7');
const VirtualInputKey VirtualInputKey::number8(L'8', L"8", 18, '8');
const VirtualInputKey VirtualInputKey::number9(L'9', L"9", 19, '9');

const VirtualInputKey VirtualInputKey::letterA(L'a', L"a", 20, 'A');
const VirtualInputKey VirtualInputKey::letterB(L'b', L"b", 21, 'B');
const VirtualInputKey VirtualInputKey::letterC(L'c', L"c", 22, 'C');
const VirtualInputKey VirtualInputKey::letterD(L'd', L"d", 23, 'D');
const VirtualInputKey VirtualInputKey::letterE(L'e', L"e", 24, 'E');
const VirtualInputKey VirtualInputKey::letterF(L'f', L"f", 25, 'F');
const VirtualInputKey VirtualInputKey::letterG(L'g', L"g", 26, 'G');
const VirtualInputKey VirtualInputKey::letterH(L'h', L"h", 27, 'H');
const VirtualInputKey VirtualInputKey::letterI(L'i', L"i", 28, 'I');
const VirtualInputKey VirtualInputKey::letterJ(L'j', L"j", 29, 'J');
const VirtualInputKey VirtualInputKey::letterK(L'k', L"k", 30, 'K');
const VirtualInputKey VirtualInputKey::letterL(L'l', L"l", 31, 'L');
const VirtualInputKey VirtualInputKey::letterM(L'm', L"m", 32, 'M');
const VirtualInputKey VirtualInputKey::letterN(L'n', L"n", 33, 'N');
const VirtualInputKey VirtualInputKey::letterO(L'o', L"o", 34, 'O');
const VirtualInputKey VirtualInputKey::letterP(L'p', L"p", 35, 'P');
const VirtualInputKey VirtualInputKey::letterQ(L'q', L"q", 36, 'Q');
const VirtualInputKey VirtualInputKey::letterR(L'r', L"r", 37, 'R');
const VirtualInputKey VirtualInputKey::letterS(L's', L"s", 38, 'S');
const VirtualInputKey VirtualInputKey::letterT(L't', L"t", 39, 'T');
const VirtualInputKey VirtualInputKey::letterU(L'u', L"u", 40, 'U');
const VirtualInputKey VirtualInputKey::letterV(L'v', L"v", 41, 'V');
const VirtualInputKey VirtualInputKey::letterW(L'w', L"w", 42, 'W');
const VirtualInputKey VirtualInputKey::letterX(L'x', L"x", 43, 'X');
const VirtualInputKey VirtualInputKey::letterY(L'y', L"y", 44, 'Y');
const VirtualInputKey VirtualInputKey::letterZ(L'z', L"z", 45, 'Z');

const VirtualInputKey VirtualInputKey::apostrophe(L'\'', L"'", 47, VK_OEM_7);
const VirtualInputKey VirtualInputKey::grave(L'`', L"`", 48, VK_OEM_3);

namespace {
const VirtualInputKey digitSet[] = {
    VirtualInputKey::number0,
    VirtualInputKey::number1,
    VirtualInputKey::number2,
    VirtualInputKey::number3,
    VirtualInputKey::number4,
    VirtualInputKey::number5,
    VirtualInputKey::number6,
    VirtualInputKey::number7,
    VirtualInputKey::number8,
    VirtualInputKey::number9
};

const VirtualInputKey toneSet[] = {
    VirtualInputKey::number1,
    VirtualInputKey::number2,
    VirtualInputKey::number3,
    VirtualInputKey::number4,
    VirtualInputKey::number5,
    VirtualInputKey::number6
};

const VirtualInputKey alphabetSet[] = {
    VirtualInputKey::letterA,
    VirtualInputKey::letterB,
    VirtualInputKey::letterC,
    VirtualInputKey::letterD,
    VirtualInputKey::letterE,
    VirtualInputKey::letterF,
    VirtualInputKey::letterG,
    VirtualInputKey::letterH,
    VirtualInputKey::letterI,
    VirtualInputKey::letterJ,
    VirtualInputKey::letterK,
    VirtualInputKey::letterL,
    VirtualInputKey::letterM,
    VirtualInputKey::letterN,
    VirtualInputKey::letterO,
    VirtualInputKey::letterP,
    VirtualInputKey::letterQ,
    VirtualInputKey::letterR,
    VirtualInputKey::letterS,
    VirtualInputKey::letterT,
    VirtualInputKey::letterU,
    VirtualInputKey::letterV,
    VirtualInputKey::letterW,
    VirtualInputKey::letterX,
    VirtualInputKey::letterY,
    VirtualInputKey::letterZ
};

BOOL ContainsInputKey(_In_reads_(count) const VirtualInputKey* pInputKeys, size_t count, const VirtualInputKey& inputKey)
{
    for (size_t index = 0; index < count; index++)
    {
        if (pInputKeys[index] == inputKey)
        {
            return TRUE;
        }
    }
    return FALSE;
}

BOOL MatchInputKeyByKeyCode(_In_reads_(count) const VirtualInputKey* pInputKeys, size_t count, UINT keyCode, _Out_ VirtualInputKey* pInputKey)
{
    for (size_t index = 0; index < count; index++)
    {
        if (pInputKeys[index].keyCode == keyCode)
        {
            *pInputKey = pInputKeys[index];
            return TRUE;
        }
    }
    return FALSE;
}

BOOL MatchInputKeyByCode(_In_reads_(count) const VirtualInputKey* pInputKeys, size_t count, int code, _Out_ VirtualInputKey* pInputKey)
{
    for (size_t index = 0; index < count; index++)
    {
        if (pInputKeys[index].code == code)
        {
            *pInputKey = pInputKeys[index];
            return TRUE;
        }
    }
    return FALSE;
}

BOOL MatchInputKeyByCharacter(_In_reads_(count) const VirtualInputKey* pInputKeys, size_t count, WCHAR character, _Out_ VirtualInputKey* pInputKey)
{
    for (size_t index = 0; index < count; index++)
    {
        if (pInputKeys[index].character == character)
        {
            *pInputKey = pInputKeys[index];
            return TRUE;
        }
    }
    return FALSE;
}

WCHAR NormalizeInputCharacter(WCHAR character)
{
    if (L'A' <= character && character <= L'Z')
    {
        return static_cast<WCHAR>(character - L'A' + L'a');
    }
    return character;
}
}

bool VirtualInputKey::operator==(const VirtualInputKey& other) const
{
    return code == other.code;
}

bool VirtualInputKey::operator!=(const VirtualInputKey& other) const
{
    return !(*this == other);
}

bool VirtualInputKey::operator<(const VirtualInputKey& other) const
{
    return code < other.code;
}

BOOL VirtualInputKey::IsNumber() const
{
    return ContainsInputKey(digitSet, digitSetCount, *this);
}

BOOL VirtualInputKey::IsToneNumber() const
{
    return ContainsInputKey(toneSet, toneSetCount, *this);
}

BOOL VirtualInputKey::IsLetter() const
{
    return ContainsInputKey(alphabetSet, alphabetSetCount, *this);
}

BOOL VirtualInputKey::IsSyllableLetter() const
{
    return IsLetter() && !IsToneLetter();
}

BOOL VirtualInputKey::IsToneLetter() const
{
    return (*this == letterV || *this == letterX || *this == letterQ) ? TRUE : FALSE;
}

BOOL VirtualInputKey::IsToneInputKey() const
{
    return IsToneLetter() || IsToneNumber();
}

BOOL VirtualInputKey::IsReverseLookupTrigger() const
{
    return (*this == letterR || *this == letterV || *this == letterX || *this == letterQ) ? TRUE : FALSE;
}

BOOL VirtualInputKey::IsYLetterY() const
{
    return (*this == letterY) ? TRUE : FALSE;
}

BOOL VirtualInputKey::IsMLetterM() const
{
    return (*this == letterM) ? TRUE : FALSE;
}

BOOL VirtualInputKey::IsApostrophe() const
{
    return (*this == apostrophe) ? TRUE : FALSE;
}

BOOL VirtualInputKey::IsGrave() const
{
    return (*this == grave) ? TRUE : FALSE;
}

int VirtualInputKey::Digit() const
{
    return IsNumber() ? (code - 10) : -1;
}

BOOL VirtualInputKey::IsMatchedNumber(UINT keyCode)
{
    VirtualInputKey inputKey;
    return MatchInputKeyByKeyCode(digitSet, digitSetCount, keyCode, &inputKey);
}

BOOL VirtualInputKey::IsMatchedLetter(UINT keyCode)
{
    VirtualInputKey inputKey;
    return MatchInputKeyByKeyCode(alphabetSet, alphabetSetCount, keyCode, &inputKey);
}

BOOL VirtualInputKey::MatchInputKeyForKeyCode(UINT keyCode, _Out_ VirtualInputKey* pInputKey)
{
    if (pInputKey == nullptr)
    {
        return FALSE;
    }

    switch (keyCode)
    {
    case VK_OEM_7:
        *pInputKey = apostrophe;
        return TRUE;
    case VK_OEM_3:
        *pInputKey = grave;
        return TRUE;
    default:
        return MatchInputKeyByKeyCode(alphabetSet, alphabetSetCount, keyCode, pInputKey) ||
            MatchInputKeyByKeyCode(digitSet, digitSetCount, keyCode, pInputKey);
    }
}

BOOL VirtualInputKey::MatchInputKeyForCode(int code, _Out_ VirtualInputKey* pInputKey)
{
    if (pInputKey == nullptr)
    {
        return FALSE;
    }

    return MatchInputKeyByCode(alphabetSet, alphabetSetCount, code, pInputKey) ||
        MatchInputKeyByCode(digitSet, digitSetCount, code, pInputKey);
}

BOOL VirtualInputKey::MatchInputKeyForCharacter(WCHAR character, _Out_ VirtualInputKey* pInputKey)
{
    if (pInputKey == nullptr)
    {
        return FALSE;
    }

    WCHAR normalizedCharacter = NormalizeInputCharacter(character);
    return MatchInputKeyByCharacter(alphabetSet, alphabetSetCount, normalizedCharacter, pInputKey) ||
        MatchInputKeyByCharacter(digitSet, digitSetCount, normalizedCharacter, pInputKey);
}

int VirtualInputKey::CombinedCode(_In_reads_(count) const VirtualInputKey* pInputKeys, size_t count)
{
    if (pInputKeys == nullptr || count >= 10)
    {
        return 0;
    }

    int combinedCode = 0;
    for (size_t index = 0; index < count; index++)
    {
        combinedCode = combinedCode * 100 + pInputKeys[index].code;
    }
    return combinedCode;
}

int VirtualInputKey::AnchorsCode(_In_reads_(count) const VirtualInputKey* pInputKeys, size_t count)
{
    if (pInputKeys == nullptr || count >= 10)
    {
        return 0;
    }

    int combinedCode = 0;
    for (size_t index = 0; index < count; index++)
    {
        VirtualInputKey inputKey = pInputKeys[index].IsYLetterY() ? letterJ : pInputKeys[index];
        combinedCode = combinedCode * 100 + inputKey.code;
    }
    return combinedCode;
}

const VirtualInputKey* VirtualInputKey::DigitSet()
{
    return digitSet;
}

const VirtualInputKey* VirtualInputKey::ToneSet()
{
    return toneSet;
}

const VirtualInputKey* VirtualInputKey::AlphabetSet()
{
    return alphabetSet;
}
