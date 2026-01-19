#pragma once

#include "File.h"
#include "JyutpingBaseStructure.h"

class CBaseDictionaryEngine
{
public:
    CBaseDictionaryEngine(LCID locale, _In_ CFile *pDictionaryFile);
    virtual ~CBaseDictionaryEngine();

    virtual VOID CollectWord(_In_ CStringRange *psrgKeyCode, _Out_ CJyutpingArray<CStringRange> *pasrgWordString)
    {
        psrgKeyCode;
        pasrgWordString = nullptr;
    }

    virtual VOID CollectWord(_In_ CStringRange *psrgKeyCode, _Out_ CJyutpingArray<CCandidateListItem> *pItemList)
    {
        psrgKeyCode;
        pItemList = nullptr;
    }

    virtual VOID SortListItemByFindKeyCode(_Inout_ CJyutpingArray<CCandidateListItem> *pItemList);

protected:
    CFile* _pDictionaryFile;
    LCID _locale;

private:
    VOID MergeSortByFindKeyCode(_Inout_ CJyutpingArray<CCandidateListItem> *pItemList, int leftRange, int rightRange);
    int CalculateCandidateCount(int leftRange,  int rightRange);
};
