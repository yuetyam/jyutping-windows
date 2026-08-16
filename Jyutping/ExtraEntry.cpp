#include "stdafx.h"
#include "ExtraEntry.h"

namespace {

constexpr Ime::ExtraEntry entries[] = {
    { L"啤", L"bi1", 2, 2128 },
    { L"啤女", L"bi4 neoi2", 6, 212833243428 },
    { L"啤仔", L"bi1 zai2", 5, 2128452028 },
    { L"啤啤", L"bi4 bi1", 4, 21282128 },
    { L"啤啤車", L"bi4 bi1 ce1", 6, 212821282224 },
    { L"啤啤牀", L"bi4 bi1 cong4", 8, 2128212822343326 },
    { L"啤啤女", L"bi4 bi1 neoi2", 8, 2128212833243428 },
    { L"啤啤衫", L"bi4 bi1 saam1", 8, 2128212838202032 },
    { L"啤啤仔", L"bi4 bi1 zai2", 7, 21282128452028 },
    { L"生啤啤", L"saang1 bi4 bi1", 9, 382020332621282128 },
    { L"欸", L"e6", 1, 24 },
    { L"誒", L"e6", 1, 24 },
    { L"欸", L"ei6", 2, 2428 },
    { L"誒", L"ei6", 2, 2428 },
    { L"䊦", L"et3", 2, 2439 },
    { L"籺", L"et3", 2, 2439 },
    { L"覅", L"fiu3", 3, 252840 },
    { L"𡠍", L"fiu3", 3, 252840 },
    { L"𧟰", L"fiu3", 3, 252840 },
    { L"𠺪", L"he3", 2, 2724 },
    { L"嗗", L"gut6", 3, 264039 },
    { L"摑", L"gwaak3", 5, 2642202030 },
    { L"嚕", L"lu1", 2, 3140 },
    { L"𠁣", L"ngi1", 3, 332628 },
    { L"𠃛", L"nget1", 4, 33262439 },
    { L"𠸊", L"tap1", 3, 392035 },
    { L"扤", L"at1", 2, 2039 },
    { L"扤實", L"at1 sat6", 5, 2039382039 },
    { L"扤死貓", L"at1 sei2 maau1", 9, 203938242832202040 },
    { L"嗒", L"dep1", 3, 232435 },
    { L"嗒嘢", L"dep1 je5", 5, 2324352924 },
    { L"嗒糖", L"dep1 tong4", 7, 23243539343326 },
    { L"嗒落有味", L"dep1 lok6 jau5 mei6", 12, 6338101551689960828 },
    { L"嘰咭", L"gi1 gat6", 5, 2628262039 },
    { L"嘰嘰咭咭", L"gi1 gi1 gat6 gat6", 10, 7835884188329710423 },
    { L"嘰哩咕嚕", L"gi1 li1 gu1 lu1", 8, 2628312826403140 },
    { L"喲", L"jo1", 2, 2934 },
    { L"哎喲", L"aai1 jo1", 5, 2020282934 },
    { L"哎喲", L"ai1 jo1", 4, 20282934 },
    { L"𠸉", L"kak1", 3, 302030 },
    { L"嘞𠸉", L"lak1 kak1", 6, 312030302030 },
    { L"嘞嘞𠸉𠸉", L"lak1 lak1 kak1 kak1", 12, 3636023504964717390 },
    { L"哩", L"li1", 2, 3128 },
    { L"哩個", L"li1 go3", 4, 31282634 },
    { L"花哩綠", L"faa1 li1 luk1", 8, 2520203128314030 },
    { L"𡃈", L"kwak1", 4, 30422030 },
    { L"𡃈", L"kwaak1", 5, 3042202030 },
    { L"𡁸", L"kwaak1", 5, 3042202030 },
    { L"𠽤嚦𡃈嘞", L"kik1 lik1 kwak1 lak1", 13, 7661401431458112094 },
    { L"𠽤嚦𡃈嘞", L"kik1 lik1 kwaak1 laak1", 15, 4686176365263980782 },
    { L"𠵇", L"keu4", 3, 302440 },
    { L"𠺫", L"leu1", 3, 312440 },
    { L"𠵇𠺫", L"keu4 leu1", 6, 302440312440 },
    { L"𠮩𠹌", L"liu1 lang1", 7, 31284031203326 },
    { L"啤", L"pe1", 2, 3524 },
    { L"啤牌", L"pe1 paai2", 6, 352435202028 },
    { L"𢚖", L"ti4", 2, 3928 },
    { L"發𢚖騰", L"faat3 ti4 tang4", 10, 6755295319129651710 },
    { L"啫", L"zoe1", 3, 453424 },
    { L"啫啫", L"zoe1 zoe1", 6, 453424453424 },
    { L"啫啫煲", L"zoe1 zoe1 bou1", 9, 453424453424213440 },
};

} // namespace

namespace Ime {

std::vector<Lexicon> ExtraEntry::Search(const std::vector<VirtualInputKey>& keys)
{
    int64_t spell = CombinedCode(keys);
    size_t complex = keys.size();
    std::vector<Lexicon> result;

    for (const ExtraEntry& entry : entries)
    {
        if (entry.spell != spell || entry.complex != complex)
        {
            continue;
        }

        std::wstring input = TextFromKeys(keys);
        std::wstring romanization(entry.romanization);
        result.push_back(Lexicon::Cantonese(
            std::wstring(entry.word),
            romanization,
            std::move(input),
            StrippedTones(romanization)));
    }
    return result;
}

} // namespace Ime
