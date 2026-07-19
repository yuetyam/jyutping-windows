#pragma once

#include "stdafx.h"

#include <cstddef>

struct PunctuationSymbol
{
    LPCWSTR text;
    LPCWSTR comment;
    LPCWSTR secondaryComment;
};

struct PunctuationSymbolList
{
    const PunctuationSymbol* symbols;
    size_t count;
};

struct PunctuationKey
{
    UINT keyCode;
    LPCWSTR keyText;
    LPCWSTR shiftingKeyText;
    LPCWSTR instantSymbol;
    LPCWSTR instantShiftingSymbol;
    PunctuationSymbolList symbols;
    PunctuationSymbolList shiftingSymbols;

    BOOL IsNumberKey() const;
    BOOL ShouldHandle(BOOL isShifting) const;
    LPCWSTR Text(BOOL isShifting) const;
    LPCWSTR InstantSymbol(BOOL isShifting) const;
    PunctuationSymbolList Symbols(BOOL isShifting) const;

    static const PunctuationKey* ForVirtualKey(UINT keyCode);
};
