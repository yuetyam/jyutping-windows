#include "PunctuationKey.h"

namespace {

constexpr PunctuationSymbol commaSymbols[] = {
    {L"，", nullptr, nullptr},
};
constexpr PunctuationSymbol commaShiftingSymbols[] = {
    {L"《", nullptr, nullptr},
    {L"〈", nullptr, nullptr},
    {L"<", L"小於號", nullptr},
    {L"＜", L"全寬小於號", nullptr},
    {L",", L"半寬逗號", nullptr},
};

constexpr PunctuationSymbol periodSymbols[] = {
    {L"。", nullptr, nullptr},
};
constexpr PunctuationSymbol periodShiftingSymbols[] = {
    {L"》", nullptr, nullptr},
    {L"〉", nullptr, nullptr},
    {L">", L"大於號", nullptr},
    {L"＞", L"全寬大於號", nullptr},
    {L"｡", L"半寬句號", nullptr},
    {L".", L"英文句號", nullptr},
    {L"．", L"全寬英文句號", nullptr},
};

constexpr PunctuationSymbol slashSymbols[] = {
    {L"/", nullptr, nullptr},
    {L"／", L"全寬", nullptr},
    {L"÷", nullptr, nullptr},
    {L"≠", nullptr, nullptr},
    {L"?", L"半寬", nullptr},
    {L"!", L"半寬", nullptr},
    {L"　", L"全寬空格", nullptr},
};
constexpr PunctuationSymbol slashShiftingSymbols[] = {
    {L"？", nullptr, nullptr},
};

constexpr PunctuationSymbol semicolonSymbols[] = {
    {L"；", nullptr, nullptr},
};
constexpr PunctuationSymbol semicolonShiftingSymbols[] = {
    {L"：", nullptr, nullptr},
};

constexpr PunctuationSymbol quoteSymbols[] = {
    {L"'", L"U+0027", nullptr},
    {L"＇", L"全寬", L"U+FF07"},
    {L"‘", L"左", L"U+2018"},
    {L"’", L"右", L"U+2019"},
    {L";", L"半寬分號", nullptr},
};
constexpr PunctuationSymbol quoteShiftingSymbols[] = {
    {L"\"", L"U+0022", nullptr},
    {L"＂", L"全寬", L"U+FF02"},
    {L"“", L"左", L"U+201C"},
    {L"”", L"右", L"U+201D"},
    {L":", L"半寬冒號", nullptr},
};

constexpr PunctuationSymbol bracketLeftSymbols[] = {
    {L"「", nullptr, nullptr},
};
constexpr PunctuationSymbol bracketLeftShiftingSymbols[] = {
    {L"『", nullptr, nullptr},
    {L"【", nullptr, nullptr},
    {L"〖", nullptr, nullptr},
    {L"〔", nullptr, nullptr},
    {L"[", L"半寬", nullptr},
    {L"［", L"全寬", nullptr},
    {L"{", L"半寬", nullptr},
    {L"｛", L"全寬", nullptr},
};

constexpr PunctuationSymbol bracketRightSymbols[] = {
    {L"」", nullptr, nullptr},
};
constexpr PunctuationSymbol bracketRightShiftingSymbols[] = {
    {L"』", nullptr, nullptr},
    {L"】", nullptr, nullptr},
    {L"〗", nullptr, nullptr},
    {L"〕", nullptr, nullptr},
    {L"]", L"半寬", nullptr},
    {L"］", L"全寬", nullptr},
    {L"}", L"半寬", nullptr},
    {L"｝", L"全寬", nullptr},
};

constexpr PunctuationSymbol backslashSymbols[] = {
    {L"、", nullptr, nullptr},
};
constexpr PunctuationSymbol backslashShiftingSymbols[] = {
    {L"|", nullptr, nullptr},
    {L"｜", L"全寬", nullptr},
    {L"\\", nullptr, nullptr},
    {L"＼", L"全寬", nullptr},
    {L"､", L"半寬頓號", nullptr},
};

constexpr PunctuationSymbol graveSymbols[] = {
    {L"`", L"重音符", L"U+0060"},
    {L"｀", L"全寬重音符", L"U+FF40"},
    {L"·", L"間隔號", L"U+00B7"},
    {L"•", L"項目符號", L"U+2022"},
    {L"‧", L"連字點", L"U+2027"},
    {L"･", L"半寬中點", L"U+FF65"},
    {L"・", L"全寬中點", L"U+30FB"},
    {L"◉", L"魚眼", L"U+25C9"},
};
constexpr PunctuationSymbol graveShiftingSymbols[] = {
    {L"~", nullptr, nullptr},
    {L"～", L"全寬", nullptr},
    {L"≈", nullptr, nullptr},
};

constexpr PunctuationSymbol minusSymbols[] = {
    {L"-", nullptr, nullptr},
};
constexpr PunctuationSymbol minusShiftingSymbols[] = {
    {L"——", nullptr, nullptr},
};
constexpr PunctuationSymbol equalSymbols[] = {
    {L"=", nullptr, nullptr},
};
constexpr PunctuationSymbol equalShiftingSymbols[] = {
    {L"+", nullptr, nullptr},
};

constexpr PunctuationSymbol number0Symbols[] = {
    {L"0", nullptr, nullptr},
};
constexpr PunctuationSymbol number0ShiftingSymbols[] = {
    {L"）", nullptr, nullptr},
};
constexpr PunctuationSymbol number1Symbols[] = {
    {L"1", nullptr, nullptr},
};
constexpr PunctuationSymbol number1ShiftingSymbols[] = {
    {L"！", nullptr, nullptr},
};
constexpr PunctuationSymbol number2Symbols[] = {
    {L"2", nullptr, nullptr},
};
constexpr PunctuationSymbol number2ShiftingSymbols[] = {
    {L"@", nullptr, nullptr},
    {L"＠", L"全寬", nullptr},
    {L"©", nullptr, nullptr},
    {L"®", nullptr, nullptr},
};
constexpr PunctuationSymbol number3Symbols[] = {
    {L"3", nullptr, nullptr},
};
constexpr PunctuationSymbol number3ShiftingSymbols[] = {
    {L"#", nullptr, nullptr},
    {L"＃", L"全寬", nullptr},
};
constexpr PunctuationSymbol number4Symbols[] = {
    {L"4", nullptr, nullptr},
};
constexpr PunctuationSymbol number4ShiftingSymbols[] = {
    {L"$", nullptr, nullptr},
    {L"＄", L"全寬", nullptr},
    {L"€", nullptr, nullptr},
    {L"£", nullptr, nullptr},
    {L"¥", nullptr, nullptr},
    {L"￥", L"全寬", nullptr},
    {L"₩", nullptr, nullptr},
    {L"₽", nullptr, nullptr},
    {L"¢", nullptr, nullptr},
};
constexpr PunctuationSymbol number5Symbols[] = {
    {L"5", nullptr, nullptr},
};
constexpr PunctuationSymbol number5ShiftingSymbols[] = {
    {L"%", nullptr, nullptr},
    {L"％", L"全寬", nullptr},
    {L"‰", nullptr, nullptr},
    {L"‱", nullptr, nullptr},
    {L"°", nullptr, nullptr},
    {L"℃", nullptr, nullptr},
    {L"℉", nullptr, nullptr},
    {L"∞", nullptr, nullptr},
};
constexpr PunctuationSymbol number6Symbols[] = {
    {L"6", nullptr, nullptr},
};
constexpr PunctuationSymbol number6ShiftingSymbols[] = {
    {L"^", nullptr, nullptr},
    {L"＾", L"全寬", nullptr},
    {L"……", nullptr, nullptr},
    {L"…", nullptr, nullptr},
};
constexpr PunctuationSymbol number7Symbols[] = {
    {L"7", nullptr, nullptr},
};
constexpr PunctuationSymbol number7ShiftingSymbols[] = {
    {L"&", nullptr, nullptr},
    {L"＆", L"全寬", nullptr},
    {L"§", nullptr, nullptr},
};
constexpr PunctuationSymbol number8Symbols[] = {
    {L"8", nullptr, nullptr},
};
constexpr PunctuationSymbol number8ShiftingSymbols[] = {
    {L"*", nullptr, nullptr},
    {L"＊", L"全寬", nullptr},
    {L"×", L"乘號", nullptr},
    {L"·", L"間隔號", nullptr},
    {L"•", L"項目符號", nullptr},
    {L"※", L"參攷號", nullptr},
    {L"◉", L"魚眼", nullptr},
};
constexpr PunctuationSymbol number9Symbols[] = {
    {L"9", nullptr, nullptr},
};
constexpr PunctuationSymbol number9ShiftingSymbols[] = {
    {L"（", nullptr, nullptr},
};

constexpr PunctuationKey comma = {
    VK_OEM_COMMA, L",", L"<", L"，", nullptr,
    {commaSymbols, ARRAYSIZE(commaSymbols)},
    {commaShiftingSymbols, ARRAYSIZE(commaShiftingSymbols)},
};
constexpr PunctuationKey period = {
    VK_OEM_PERIOD, L".", L">", L"。", nullptr,
    {periodSymbols, ARRAYSIZE(periodSymbols)},
    {periodShiftingSymbols, ARRAYSIZE(periodShiftingSymbols)},
};
constexpr PunctuationKey slash = {
    VK_OEM_2, L"/", L"?", nullptr, L"？",
    {slashSymbols, ARRAYSIZE(slashSymbols)},
    {slashShiftingSymbols, ARRAYSIZE(slashShiftingSymbols)},
};
constexpr PunctuationKey semicolon = {
    VK_OEM_1, L";", L":", L"；", L"：",
    {semicolonSymbols, ARRAYSIZE(semicolonSymbols)},
    {semicolonShiftingSymbols, ARRAYSIZE(semicolonShiftingSymbols)},
};
constexpr PunctuationKey quote = {
    VK_OEM_7, L"'", L"\"", nullptr, nullptr,
    {quoteSymbols, ARRAYSIZE(quoteSymbols)},
    {quoteShiftingSymbols, ARRAYSIZE(quoteShiftingSymbols)},
};
constexpr PunctuationKey bracketLeft = {
    VK_OEM_4, L"[", L"{", L"「", nullptr,
    {bracketLeftSymbols, ARRAYSIZE(bracketLeftSymbols)},
    {bracketLeftShiftingSymbols, ARRAYSIZE(bracketLeftShiftingSymbols)},
};
constexpr PunctuationKey bracketRight = {
    VK_OEM_6, L"]", L"}", L"」", nullptr,
    {bracketRightSymbols, ARRAYSIZE(bracketRightSymbols)},
    {bracketRightShiftingSymbols, ARRAYSIZE(bracketRightShiftingSymbols)},
};
constexpr PunctuationKey backslash = {
    VK_OEM_5, L"\\", L"|", L"、", nullptr,
    {backslashSymbols, ARRAYSIZE(backslashSymbols)},
    {backslashShiftingSymbols, ARRAYSIZE(backslashShiftingSymbols)},
};
constexpr PunctuationKey grave = {
    VK_OEM_3, L"`", L"~", nullptr, nullptr,
    {graveSymbols, ARRAYSIZE(graveSymbols)},
    {graveShiftingSymbols, ARRAYSIZE(graveShiftingSymbols)},
};
constexpr PunctuationKey minus = {
    VK_OEM_MINUS, L"-", L"_", L"-", L"——",
    {minusSymbols, ARRAYSIZE(minusSymbols)},
    {minusShiftingSymbols, ARRAYSIZE(minusShiftingSymbols)},
};
constexpr PunctuationKey equal = {
    VK_OEM_PLUS, L"=", L"+", L"=", L"+",
    {equalSymbols, ARRAYSIZE(equalSymbols)},
    {equalShiftingSymbols, ARRAYSIZE(equalShiftingSymbols)},
};
constexpr PunctuationKey number0 = {
    L'0', L"0", L")", L"0", L"）",
    {number0Symbols, ARRAYSIZE(number0Symbols)},
    {number0ShiftingSymbols, ARRAYSIZE(number0ShiftingSymbols)},
};
constexpr PunctuationKey number1 = {
    L'1', L"1", L"!", L"1", L"！",
    {number1Symbols, ARRAYSIZE(number1Symbols)},
    {number1ShiftingSymbols, ARRAYSIZE(number1ShiftingSymbols)},
};
constexpr PunctuationKey number2 = {
    L'2', L"2", L"@", L"2", nullptr,
    {number2Symbols, ARRAYSIZE(number2Symbols)},
    {number2ShiftingSymbols, ARRAYSIZE(number2ShiftingSymbols)},
};
constexpr PunctuationKey number3 = {
    L'3', L"3", L"#", L"3", nullptr,
    {number3Symbols, ARRAYSIZE(number3Symbols)},
    {number3ShiftingSymbols, ARRAYSIZE(number3ShiftingSymbols)},
};
constexpr PunctuationKey number4 = {
    L'4', L"4", L"$", L"4", nullptr,
    {number4Symbols, ARRAYSIZE(number4Symbols)},
    {number4ShiftingSymbols, ARRAYSIZE(number4ShiftingSymbols)},
};
constexpr PunctuationKey number5 = {
    L'5', L"5", L"%", L"5", nullptr,
    {number5Symbols, ARRAYSIZE(number5Symbols)},
    {number5ShiftingSymbols, ARRAYSIZE(number5ShiftingSymbols)},
};
constexpr PunctuationKey number6 = {
    L'6', L"6", L"^", L"6", nullptr,
    {number6Symbols, ARRAYSIZE(number6Symbols)},
    {number6ShiftingSymbols, ARRAYSIZE(number6ShiftingSymbols)},
};
constexpr PunctuationKey number7 = {
    L'7', L"7", L"&", L"7", nullptr,
    {number7Symbols, ARRAYSIZE(number7Symbols)},
    {number7ShiftingSymbols, ARRAYSIZE(number7ShiftingSymbols)},
};
constexpr PunctuationKey number8 = {
    L'8', L"8", L"*", L"8", nullptr,
    {number8Symbols, ARRAYSIZE(number8Symbols)},
    {number8ShiftingSymbols, ARRAYSIZE(number8ShiftingSymbols)},
};
constexpr PunctuationKey number9 = {
    L'9', L"9", L"(", L"9", L"（",
    {number9Symbols, ARRAYSIZE(number9Symbols)},
    {number9ShiftingSymbols, ARRAYSIZE(number9ShiftingSymbols)},
};

} // namespace

BOOL PunctuationKey::IsNumberKey() const
{
    return L'0' <= keyCode && keyCode <= L'9';
}

BOOL PunctuationKey::ShouldHandle(BOOL isShifting) const
{
    return !IsNumberKey() || isShifting;
}

LPCWSTR PunctuationKey::Text(BOOL isShifting) const
{
    return isShifting ? shiftingKeyText : keyText;
}

LPCWSTR PunctuationKey::InstantSymbol(BOOL isShifting) const
{
    return isShifting ? instantShiftingSymbol : instantSymbol;
}

PunctuationSymbolList PunctuationKey::Symbols(BOOL isShifting) const
{
    return isShifting ? shiftingSymbols : symbols;
}

const PunctuationKey* PunctuationKey::ForVirtualKey(UINT inputKeyCode)
{
    switch (inputKeyCode)
    {
    case VK_OEM_COMMA: return &comma;
    case VK_OEM_PERIOD: return &period;
    case VK_OEM_2: return &slash;
    case VK_OEM_1: return &semicolon;
    case VK_OEM_7: return &quote;
    case VK_OEM_4: return &bracketLeft;
    case VK_OEM_6: return &bracketRight;
    case VK_OEM_5: return &backslash;
    case VK_OEM_3: return &grave;
    case VK_OEM_MINUS: return &minus;
    case VK_OEM_PLUS: return &equal;
    case L'0': return &number0;
    case L'1': return &number1;
    case L'2': return &number2;
    case L'3': return &number3;
    case L'4': return &number4;
    case L'5': return &number5;
    case L'6': return &number6;
    case L'7': return &number7;
    case L'8': return &number8;
    case L'9': return &number9;
    default: return nullptr;
    }
}
