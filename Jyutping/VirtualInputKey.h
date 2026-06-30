#pragma once

#include "stdafx.h"
#include "sal.h"

#include <cstddef>
#include <cstdint>

struct VirtualInputKey
{
    WCHAR character;
    LPCWSTR text;
    int code;
    UINT keyCode;
    int id;

    constexpr VirtualInputKey() :
        character(0),
        text(L""),
        code(0),
        keyCode(0),
        id(0)
    {
    }

    constexpr VirtualInputKey(WCHAR inputCharacter, LPCWSTR inputText, int inputCode, UINT inputKeyCode) :
        character(inputCharacter),
        text(inputText),
        code(inputCode),
        keyCode(inputKeyCode),
        id(inputCode)
    {
    }

    bool operator==(const VirtualInputKey& other) const;
    bool operator!=(const VirtualInputKey& other) const;
    bool operator<(const VirtualInputKey& other) const;

    BOOL IsNumber() const;
    BOOL IsToneNumber() const;
    BOOL IsLetter() const;
    BOOL IsSyllableLetter() const;
    BOOL IsToneLetter() const;
    BOOL IsToneInputKey() const;
    BOOL IsReverseLookupTrigger() const;
    BOOL IsYLetterY() const;
    BOOL IsMLetterM() const;
    BOOL IsApostrophe() const;
    BOOL IsGrave() const;
    int Digit() const;

    static BOOL IsMatchedNumber(UINT keyCode);
    static BOOL IsMatchedLetter(UINT keyCode);
    static BOOL MatchInputKeyForKeyCode(UINT keyCode, _Out_ VirtualInputKey* pInputKey);
    static BOOL MatchInputKeyForCode(int code, _Out_ VirtualInputKey* pInputKey);
    static BOOL MatchInputKeyForCharacter(WCHAR character, _Out_ VirtualInputKey* pInputKey);

    static int64_t CombinedCode(_In_reads_(count) const VirtualInputKey* pInputKeys, size_t count);
    static int64_t AnchorsCode(_In_reads_(count) const VirtualInputKey* pInputKeys, size_t count);

    static const VirtualInputKey* DigitSet();
    static const VirtualInputKey* ToneSet();
    static const VirtualInputKey* AlphabetSet();

    static constexpr size_t digitSetCount = 10;
    static constexpr size_t toneSetCount = 6;
    static constexpr size_t alphabetSetCount = 26;

    static const VirtualInputKey number0;
    static const VirtualInputKey number1;
    static const VirtualInputKey number2;
    static const VirtualInputKey number3;
    static const VirtualInputKey number4;
    static const VirtualInputKey number5;
    static const VirtualInputKey number6;
    static const VirtualInputKey number7;
    static const VirtualInputKey number8;
    static const VirtualInputKey number9;

    static const VirtualInputKey letterA;
    static const VirtualInputKey letterB;
    static const VirtualInputKey letterC;
    static const VirtualInputKey letterD;
    static const VirtualInputKey letterE;
    static const VirtualInputKey letterF;
    static const VirtualInputKey letterG;
    static const VirtualInputKey letterH;
    static const VirtualInputKey letterI;
    static const VirtualInputKey letterJ;
    static const VirtualInputKey letterK;
    static const VirtualInputKey letterL;
    static const VirtualInputKey letterM;
    static const VirtualInputKey letterN;
    static const VirtualInputKey letterO;
    static const VirtualInputKey letterP;
    static const VirtualInputKey letterQ;
    static const VirtualInputKey letterR;
    static const VirtualInputKey letterS;
    static const VirtualInputKey letterT;
    static const VirtualInputKey letterU;
    static const VirtualInputKey letterV;
    static const VirtualInputKey letterW;
    static const VirtualInputKey letterX;
    static const VirtualInputKey letterY;
    static const VirtualInputKey letterZ;

    static const VirtualInputKey apostrophe;
    static const VirtualInputKey grave;
};
