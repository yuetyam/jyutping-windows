#include "Private.h"
#include "DictionaryParser.h"
#include "JyutpingBaseStructure.h"

//---------------------------------------------------------------------
//
// ctor
//
//---------------------------------------------------------------------

CDictionaryParser::CDictionaryParser(LCID locale)
{
    _locale = locale;
}

//---------------------------------------------------------------------
//
// dtor
//
//---------------------------------------------------------------------

CDictionaryParser::~CDictionaryParser()
{
}

//---------------------------------------------------------------------
//
// ParseLine
//
// dwBufLen - in character count
//
//---------------------------------------------------------------------

BOOL CDictionaryParser::ParseLine(_In_reads_(dwBufLen) LPCWSTR pwszBuffer, DWORD_PTR dwBufLen, _Out_ CParserStringRange *psrgKeyword, _Inout_opt_ CJyutpingArray<CParserStringRange> *pValue, _Inout_opt_ CJyutpingArray<CParserStringRange> *pComment)
{
    // Validate input
    if (!pwszBuffer || dwBufLen == 0 || !psrgKeyword)
    {
        return FALSE;
    }

    LPCWSTR pwszKeyWordDelimiter = nullptr;
    pwszKeyWordDelimiter = GetToken(pwszBuffer, dwBufLen, Global::KeywordDelimiter, psrgKeyword);
    if (!(pwszKeyWordDelimiter))
    {
        return FALSE;    // No delimiter found, invalid format
    }

    // Validate keyword is not empty
    if (psrgKeyword->GetLength() == 0)
    {
        return FALSE;    // Empty keyword is invalid
    }

    dwBufLen -= (pwszKeyWordDelimiter - pwszBuffer);
    pwszBuffer = pwszKeyWordDelimiter + 1;

    // Safety check for buffer length
    if (dwBufLen == 0)
    {
        return FALSE;    // No value after keyword delimiter
    }
    dwBufLen--;

    // Get value.
    if (pValue)
    {
        CParserStringRange psrgValueTemp;
        LPCWSTR pwszValueDelimiter = nullptr;

        pwszValueDelimiter = GetToken(pwszBuffer, dwBufLen, Global::KeywordDelimiter, &psrgValueTemp);

        // If no delimiter found, treat remaining buffer as the value (no comment field)
        if (!(pwszValueDelimiter))
        {
            // Value is the rest of the line, no comment present
            CParserStringRange* psrgValue = pValue->Append();
            if (!psrgValue)
            {
                return FALSE;
            }
            *psrgValue = psrgValueTemp;
            return TRUE;  // Successfully parsed keyword and value, no comment
        }

        CParserStringRange* psrgValue = pValue->Append();
        if (!psrgValue)
        {
            return FALSE;
        }
        *psrgValue = psrgValueTemp;

        dwBufLen -= (pwszValueDelimiter - pwszBuffer);
        pwszBuffer = pwszValueDelimiter + 1;

        // Safety check for buffer length
        if (dwBufLen == 0)
        {
            return TRUE;  // No comment field, but that's OK
        }
        dwBufLen--;

        // Get Comment
        if (pComment)
        {
            if (dwBufLen > 0)
            {
                CParserStringRange* psrgComment = pComment->Append();
                if (!psrgComment)
                {
                    return FALSE;
                }
                psrgComment->Set(pwszBuffer, dwBufLen);
                RemoveWhiteSpaceFromBegin(psrgComment);
                RemoveWhiteSpaceFromEnd(psrgComment);
                RemoveStringDelimiter(psrgComment);
            }
        }
    }

    return TRUE;
}

//---------------------------------------------------------------------
//
// GetToken
//
// dwBufLen - in character count
//
// return   - pointer of delimiter which specified chDelimiter
//
//---------------------------------------------------------------------
_Ret_maybenull_
LPCWSTR CDictionaryParser::GetToken(_In_reads_(dwBufLen) LPCWSTR pwszBuffer, DWORD_PTR dwBufLen, _In_ const WCHAR chDelimiter, _Out_ CParserStringRange *psrgValue)
{
    WCHAR ch = '\0';

    // Validate input
    if (!pwszBuffer || dwBufLen == 0 || !psrgValue)
    {
        return nullptr;
    }

    psrgValue->Set(pwszBuffer, dwBufLen);

    ch = *pwszBuffer;
    while ((ch) && (ch != chDelimiter) && dwBufLen)
    {
        dwBufLen--;
        pwszBuffer++;

        if (ch == Global::StringDelimiter)
        {
            // Skip to the closing string delimiter
            while (*pwszBuffer && (*pwszBuffer != Global::StringDelimiter) && dwBufLen)
            {
                dwBufLen--;
                pwszBuffer++;
            }
            // Check if we found the closing delimiter
            if (*pwszBuffer == Global::StringDelimiter && dwBufLen)
            {
                dwBufLen--;
                pwszBuffer++;
            }
            else
            {
                // Unmatched quote - malformed input
                return nullptr;
            }
        }
        ch = *pwszBuffer;
    }

    // Check if we found the delimiter
    if (*pwszBuffer == chDelimiter && dwBufLen)
    {
        LPCWSTR pwszStart = psrgValue->Get();

        psrgValue->Set(pwszStart, pwszBuffer - pwszStart);

        RemoveWhiteSpaceFromBegin(psrgValue);
        RemoveWhiteSpaceFromEnd(psrgValue);
        RemoveStringDelimiter(psrgValue);

        return pwszBuffer;
    }

    // No delimiter found - this might be the last token
    // Update the value to include everything up to current position
    LPCWSTR pwszStart = psrgValue->Get();
    psrgValue->Set(pwszStart, pwszBuffer - pwszStart);

    RemoveWhiteSpaceFromBegin(psrgValue);
    RemoveWhiteSpaceFromEnd(psrgValue);
    RemoveStringDelimiter(psrgValue);

    return nullptr;
}

//---------------------------------------------------------------------
//
// RemoveWhiteSpaceFromBegin
// RemoveWhiteSpaceFromEnd
// RemoveStringDelimiter
//
//---------------------------------------------------------------------

BOOL CDictionaryParser::RemoveWhiteSpaceFromBegin(_Inout_opt_ CStringRange *pString)
{
    DWORD_PTR dwIndexTrace = 0;  // in char

    if (pString == nullptr)
    {
        return FALSE;
    }

    if (SkipWhiteSpace(_locale, pString->Get(), pString->GetLength(), &dwIndexTrace) != S_OK)
    {
        return FALSE;
    }

    pString->Set(pString->Get() + dwIndexTrace, pString->GetLength() - dwIndexTrace);
    return TRUE;
}

BOOL CDictionaryParser::RemoveWhiteSpaceFromEnd(_Inout_opt_ CStringRange *pString)
{
    if (pString == nullptr)
    {
        return FALSE;
    }

    DWORD_PTR dwTotalBufLen = pString->GetLength();
    LPCWSTR pwszEnd = pString->Get() + dwTotalBufLen - 1;

    while (dwTotalBufLen && (IsSpace(_locale, *pwszEnd) || *pwszEnd == L'\r' || *pwszEnd == L'\n'))
    {
        pwszEnd--;
        dwTotalBufLen--;
    }

    pString->Set(pString->Get(), dwTotalBufLen);
    return TRUE;
}

BOOL CDictionaryParser::RemoveStringDelimiter(_Inout_opt_ CStringRange *pString)
{
    if (pString == nullptr)
    {
        return FALSE;
    }

    DWORD_PTR length = pString->GetLength();

    // Need at least 2 characters to have quotes on both ends
    if (length >= 2)
    {
        LPCWSTR pStart = pString->Get();
        LPCWSTR pEnd = pStart + length - 1;

        // Check if string is properly quoted on both ends
        if ((*pStart == Global::StringDelimiter) && (*pEnd == Global::StringDelimiter))
        {
            pString->Set(pStart + 1, length - 2);
            return TRUE;
        }
    }

    // String is not quoted or improperly quoted - leave as is
    return FALSE;
}

//---------------------------------------------------------------------
//
// GetOneLine
//
// dwBufLen - in character count
//
//---------------------------------------------------------------------

DWORD_PTR CDictionaryParser::GetOneLine(_In_z_ LPCWSTR pwszBuffer, DWORD_PTR dwBufLen)
{
    DWORD_PTR dwIndexTrace = 0;     // in char

    if (FAILED(FindChar(L'\r', pwszBuffer, dwBufLen, &dwIndexTrace)))
    {
        if (FAILED(FindChar(L'\0', pwszBuffer, dwBufLen, &dwIndexTrace)))
        {
            return dwBufLen;
        }
    }

    return dwIndexTrace;
}
