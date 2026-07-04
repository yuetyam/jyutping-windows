#include "CharacterStandard.h"
#include "ImeDatabase.h"

#include <optional>
#include <vector>

namespace {

struct ScalarSegment
{
    std::wstring text;
    uint32_t codePoint = 0;
    bool isValid = false;
};

struct PhrasePair
{
    std::wstring_view source;
    std::wstring_view target;
};

constexpr size_t minPhraseLength = 2;

bool IsHighSurrogate(WCHAR character)
{
    return 0xD800 <= character && character <= 0xDBFF;
}

bool IsLowSurrogate(WCHAR character)
{
    return 0xDC00 <= character && character <= 0xDFFF;
}

uint32_t CombineSurrogates(WCHAR high, WCHAR low)
{
    uint32_t highValue = static_cast<uint32_t>(high) - 0xD800;
    uint32_t lowValue = static_cast<uint32_t>(low) - 0xDC00;
    return 0x10000 + ((highValue << 10) | lowValue);
}

std::vector<ScalarSegment> SplitScalars(std::wstring_view text)
{
    std::vector<ScalarSegment> result;
    result.reserve(text.size());

    size_t index = 0;
    while (index < text.size())
    {
        WCHAR character = text[index];
        if (IsHighSurrogate(character) && index + 1 < text.size() && IsLowSurrogate(text[index + 1]))
        {
            result.push_back({
                std::wstring(text.substr(index, 2)),
                CombineSurrogates(character, text[index + 1]),
                true
            });
            index += 2;
        }
        else
        {
            bool isValid = !IsHighSurrogate(character) && !IsLowSurrogate(character);
            result.push_back({
                std::wstring(1, character),
                static_cast<uint32_t>(character),
                isValid
            });
            index++;
        }
    }
    return result;
}

size_t ScalarCount(std::wstring_view text)
{
    size_t count = 0;
    size_t index = 0;
    while (index < text.size())
    {
        if (IsHighSurrogate(text[index]) && index + 1 < text.size() && IsLowSurrogate(text[index + 1]))
        {
            index += 2;
        }
        else
        {
            index++;
        }
        count++;
    }
    return count;
}

std::optional<std::wstring> TextFromCodePoint(uint32_t codePoint)
{
    if (codePoint > 0x10FFFF || (0xD800 <= codePoint && codePoint <= 0xDFFF))
    {
        return std::nullopt;
    }

    if (codePoint <= 0xFFFF)
    {
        return std::wstring(1, static_cast<WCHAR>(codePoint));
    }

    uint32_t value = codePoint - 0x10000;
    std::wstring result;
    result.push_back(static_cast<WCHAR>(0xD800 + (value >> 10)));
    result.push_back(static_cast<WCHAR>(0xDC00 + (value & 0x3FF)));
    return result;
}

std::wstring ConvertSegment(const ScalarSegment& segment, const ImeDatabase::VariantLookup& lookup)
{
    if (!segment.isValid)
    {
        return segment.text;
    }

    std::optional<uint32_t> target = lookup.Query(segment.codePoint);
    if (!target)
    {
        return segment.text;
    }

    std::optional<std::wstring> converted = TextFromCodePoint(*target);
    return converted.value_or(segment.text);
}

std::wstring ConvertSegments(const std::vector<ScalarSegment>& segments, const ImeDatabase::VariantLookup& lookup)
{
    std::wstring result;
    result.reserve(segments.size());
    for (const ScalarSegment& segment : segments)
    {
        result.append(ConvertSegment(segment, lookup));
    }
    return result;
}

std::wstring SegmentText(const std::vector<ScalarSegment>& segments, size_t index, size_t length)
{
    std::wstring result;
    for (size_t offset = 0; offset < length; offset++)
    {
        result.append(segments[index + offset].text);
    }
    return result;
}

template <size_t Count>
std::optional<std::wstring_view> FindPhrase(std::wstring_view text, const PhrasePair (&phrases)[Count])
{
    for (const PhrasePair& phrase : phrases)
    {
        if (phrase.source == text)
        {
            return phrase.target;
        }
    }
    return std::nullopt;
}

template <size_t Count>
size_t MaxPhraseLength(const PhrasePair (&phrases)[Count])
{
    size_t result = minPhraseLength;
    for (const PhrasePair& phrase : phrases)
    {
        size_t length = ScalarCount(phrase.source);
        result = (result < length) ? length : result;
    }
    return result;
}

template <size_t Count>
std::wstring GreedyPhraseConvert(
    const std::vector<ScalarSegment>& segments,
    const ImeDatabase::VariantLookup& lookup,
    const PhrasePair (&phrases)[Count])
{
    std::wstring result;
    result.reserve(segments.size());

    size_t maxPhraseLength = MaxPhraseLength(phrases);
    size_t index = 0;
    while (index < segments.size())
    {
        bool didMatch = false;
        size_t remainingLength = segments.size() - index;
        size_t currentMaxLength = (maxPhraseLength < remainingLength) ? maxPhraseLength : remainingLength;
        for (size_t length = currentMaxLength; length >= minPhraseLength; length--)
        {
            std::wstring candidate = SegmentText(segments, index, length);
            if (auto replacement = FindPhrase(std::wstring_view(candidate), phrases))
            {
                result.append(*replacement);
                index += length;
                didMatch = true;
                break;
            }
        }

        if (!didMatch)
        {
            const ScalarSegment& segment = segments[index];
            if (segment.isValid && Ime::IsIdeographicCodePoint(segment.codePoint))
            {
                result.append(ConvertSegment(segment, lookup));
            }
            else
            {
                result.append(segment.text);
            }
            index++;
        }
    }
    return result;
}

template <size_t Count>
std::wstring PhraseAwareConvert(
    const ImeDatabase& database,
    std::wstring_view text,
    CharacterStandard standard,
    const PhrasePair (&phrases)[Count])
{
    std::vector<ScalarSegment> segments = SplitScalars(text);
    if (segments.empty())
    {
        return std::wstring(text);
    }

    ImeDatabase::VariantLookup lookup = database.CreateVariantLookup(standard);
    if (segments.size() == 1)
    {
        return ConvertSegment(segments.front(), lookup);
    }

    std::wstring wholeText(text);
    if (auto replacement = FindPhrase(std::wstring_view(wholeText), phrases))
    {
        return std::wstring(*replacement);
    }

    if (segments.size() == 2)
    {
        return ConvertSegments(segments, lookup);
    }

    return GreedyPhraseConvert(segments, lookup, phrases);
}

static constexpr PhrasePair prcGeneralPhrases[] =
{
    { L"上鍊", L"上鏈" },
    { L"么麼", L"幺麽" },
    { L"么麽", L"幺麽" },
    { L"以功覆過", L"以功覆過" },
    { L"侔德覆載", L"侔德覆載" },
    { L"傢俱", L"傢具" },
    { L"函覆", L"函復" },
    { L"反反覆覆", L"反反復復" },
    { L"反覆", L"反復" },
    { L"反覆思維", L"反復思維" },
    { L"反覆思量", L"反復思量" },
    { L"反覆性", L"反復性" },
    { L"名覆金甌", L"名復金甌" },
    { L"哪吒", L"哪吒" },
    { L"回覆", L"回復" },
    { L"射覆", L"射覆" },
    { L"彷彿", L"仿佛" },
    { L"彷徨", L"彷徨" },
    { L"手鍊", L"手鏈" },
    { L"拉鍊", L"拉鏈" },
    { L"拉鍊工程", L"拉鏈工程" },
    { L"拜覆", L"拜復" },
    { L"文錦覆阱", L"文錦覆阱" },
    { L"於世成", L"於世成" },
    { L"於乎", L"於乎" },
    { L"於仲完", L"於仲完" },
    { L"於倫", L"於倫" },
    { L"於其一", L"於其一" },
    { L"於則", L"於則" },
    { L"於勇明", L"於勇明" },
    { L"於呼哀哉", L"於呼哀哉" },
    { L"於單", L"於單" },
    { L"於坦", L"於坦" },
    { L"於崇文", L"於崇文" },
    { L"於忠祥", L"於忠祥" },
    { L"於惟一", L"於惟一" },
    { L"於戲", L"於戲" },
    { L"於敖", L"於敖" },
    { L"於梨華", L"於梨華" },
    { L"於清言", L"於清言" },
    { L"於潛", L"於潜" },
    { L"於琳", L"於琳" },
    { L"於穆", L"於穆" },
    { L"於竹屋", L"於竹屋" },
    { L"於菟", L"於菟" },
    { L"於邑", L"於邑" },
    { L"於陵子", L"於陵子" },
    { L"明覆", L"明復" },
    { L"木吒", L"木吒" },
    { L"李澤鉅", L"李澤鉅" },
    { L"李鍊福", L"李鏈福" },
    { L"束脩", L"束脩" },
    { L"樊於期", L"樊於期" },
    { L"沈沒", L"沉没" },
    { L"沈沒成本", L"沉没成本" },
    { L"沈積", L"沉積" },
    { L"沈船", L"沉船" },
    { L"沈默", L"沉默" },
    { L"珍珠項鍊", L"珍珠項鏈" },
    { L"甚鉅", L"甚鉅" },
    { L"申覆", L"申復" },
    { L"畢昇", L"畢昇" },
    { L"發覆", L"發覆" },
    { L"示覆", L"示復" },
    { L"稟覆", L"禀復" },
    { L"答覆", L"答復" },
    { L"肘手鍊足", L"肘手鏈足" },
    { L"脩敬", L"脩敬" },
    { L"脩脯", L"脩脯" },
    { L"脩金", L"脩金" },
    { L"蕩覆", L"蕩覆" },
    { L"覆上", L"覆上" },
    { L"覆住", L"覆住" },
    { L"覆信", L"復信" },
    { L"覆冒", L"覆冒" },
    { L"覆呈", L"復呈" },
    { L"覆命", L"復命" },
    { L"覆墓", L"復墓" },
    { L"覆宗", L"覆宗" },
    { L"覆帳", L"復帳" },
    { L"覆幬", L"覆幬" },
    { L"覆成", L"覆成" },
    { L"覆按", L"復按" },
    { L"覆文", L"復文" },
    { L"覆杯", L"覆杯" },
    { L"覆校", L"復校" },
    { L"覆瓿", L"覆瓿" },
    { L"覆盂", L"覆盂" },
    { L"覆盆", L"覆盆" },
    { L"覆盆子", L"覆盆子" },
    { L"覆盤", L"覆盤" },
    { L"覆育", L"覆育" },
    { L"覆蕉尋鹿", L"覆蕉尋鹿" },
    { L"覆逆", L"覆逆" },
    { L"覆醢", L"覆醢" },
    { L"覆醬瓿", L"覆醬瓿" },
    { L"覆電", L"復電" },
    { L"覆露", L"覆露" },
    { L"覆鹿尋蕉", L"覆鹿尋蕉" },
    { L"覆鹿遺蕉", L"覆鹿遺蕉" },
    { L"覆鼎", L"覆鼎" },
    { L"見覆", L"見復" },
    { L"貂覆額", L"貂覆額" },
    { L"買臣覆水", L"買臣覆水" },
    { L"重覆", L"重復" },
    { L"金吒", L"金吒" },
    { L"金鍊", L"金鏈" },
    { L"鈞覆", L"鈞復" },
    { L"鉅子", L"鉅子" },
    { L"鉅萬", L"鉅萬" },
    { L"鉅防", L"鉅防" },
    { L"鉸鍊", L"鉸鏈" },
    { L"銀鍊", L"銀鏈" },
    { L"鍊墜", L"鏈墜" },
    { L"鍊子", L"鏈子" },
    { L"鍊形", L"鏈形" },
    { L"鍊條", L"鏈條" },
    { L"鍊錘", L"鏈錘" },
    { L"鍊鎖", L"鏈鎖" },
    { L"鎖鍊", L"鎖鏈" },
    { L"鐵鍊", L"鐵鏈" },
    { L"鑽石項鍊", L"鑽石項鏈" },
    { L"雁杳魚沈", L"雁杳魚沉" },
    { L"雖覆能復", L"雖覆能復" },
    { L"電覆", L"電復" },
    { L"露覆", L"露覆" },
    { L"項鍊", L"項鏈" },
    { L"頗覆", L"頗覆" },
    { L"頸鍊", L"頸鏈" },
};

static constexpr PhrasePair mutilatedPhrases[] =
{
    { L"一目瞭然", L"一目了然" },
    { L"上鍊", L"上链" },
    { L"不瞭解", L"不了解" },
    { L"么麼", L"幺麽" },
    { L"么麽", L"幺麽" },
    { L"乾乾淨淨", L"干干净净" },
    { L"乾乾脆脆", L"干干脆脆" },
    { L"乾佑縣", L"乾佑县" },
    { L"乾元", L"乾元" },
    { L"乾卦", L"乾卦" },
    { L"乾嘉", L"乾嘉" },
    { L"乾圖", L"乾图" },
    { L"乾坤", L"乾坤" },
    { L"乾宅", L"乾宅" },
    { L"乾安縣", L"乾安县" },
    { L"乾安鎮", L"乾安镇" },
    { L"乾州", L"乾州" },
    { L"乾斷", L"乾断" },
    { L"乾旦", L"乾旦" },
    { L"乾曜", L"乾曜" },
    { L"乾清宮", L"乾清宫" },
    { L"乾盛世", L"乾盛世" },
    { L"乾紅", L"乾红" },
    { L"乾綱", L"乾纲" },
    { L"乾縣", L"乾县" },
    { L"乾象", L"乾象" },
    { L"乾造", L"乾造" },
    { L"乾道", L"乾道" },
    { L"乾陵", L"乾陵" },
    { L"乾隆", L"乾隆" },
    { L"二噁英", L"二𫫇英" },
    { L"以免藉口", L"以免借口" },
    { L"以功覆過", L"以功复过" },
    { L"侔德覆載", L"侔德复载" },
    { L"傢俱", L"家具" },
    { L"傷亡枕藉", L"伤亡枕藉" },
    { L"八濛山", L"八濛山" },
    { L"凌藉", L"凌借" },
    { L"出醜狼藉", L"出丑狼藉" },
    { L"函覆", L"函复" },
    { L"千鍾", L"千锺" },
    { L"千鍾少", L"千锺少" },
    { L"千鍾粟", L"千锺粟" },
    { L"反反覆覆", L"反反复复" },
    { L"反覆", L"反复" },
    { L"反覆思維", L"反复思维" },
    { L"反覆思量", L"反复思量" },
    { L"反覆性", L"反复性" },
    { L"名覆金甌", L"名复金瓯" },
    { L"哪吒", L"哪吒" },
    { L"回覆", L"回复" },
    { L"壺裏乾坤", L"壶里乾坤" },
    { L"宫商角徵羽", L"宫商角徵羽" },
    { L"尼乾子", L"尼乾子" },
    { L"尼乾陀", L"尼乾陀" },
    { L"幺麼", L"幺麽" },
    { L"幺麼小丑", L"幺麽小丑" },
    { L"幺麼小醜", L"幺麽小丑" },
    { L"康乾", L"康乾" },
    { L"彷彿", L"仿佛" },
    { L"彷徨", L"彷徨" },
    { L"徵弦", L"徵弦" },
    { L"徵絃", L"徵弦" },
    { L"徵羽摩柯", L"徵羽摩柯" },
    { L"徵聲", L"徵声" },
    { L"徵調", L"徵调" },
    { L"徵音", L"徵音" },
    { L"情有獨鍾", L"情有独钟" },
    { L"憑藉", L"凭借" },
    { L"憑藉着", L"凭借着" },
    { L"手鍊", L"手链" },
    { L"扭轉乾坤", L"扭转乾坤" },
    { L"批覆", L"批复" },
    { L"找藉口", L"找借口" },
    { L"拉鍊", L"拉链" },
    { L"拉鍊工程", L"拉链工程" },
    { L"拜覆", L"拜复" },
    { L"據瞭解", L"据了解" },
    { L"文錦覆阱", L"文锦复阱" },
    { L"於呼哀哉", L"於呼哀哉" },
    { L"於菟", L"於菟" },
    { L"於邑", L"於邑" },
    { L"於陵子", L"於陵子" },
    { L"旋乾轉坤", L"旋乾转坤" },
    { L"旋轉乾坤", L"旋转乾坤" },
    { L"明瞭", L"明了" },
    { L"明覆", L"明复" },
    { L"朝乾夕惕", L"朝乾夕惕" },
    { L"木吒", L"木吒" },
    { L"李承乾", L"李承乾" },
    { L"李澤鉅", L"李泽钜" },
    { L"樊於期", L"樊於期" },
    { L"沈沒", L"沉没" },
    { L"沈沒成本", L"沉没成本" },
    { L"沈積", L"沉积" },
    { L"沈船", L"沉船" },
    { L"沈默", L"沉默" },
    { L"流徵", L"流徵" },
    { L"浪蕩乾坤", L"浪荡乾坤" },
    { L"牴牾", L"抵牾" },
    { L"牴觸", L"抵触" },
    { L"狐藉虎威", L"狐借虎威" },
    { L"珍珠項鍊", L"珍珠项链" },
    { L"甚鉅", L"甚钜" },
    { L"申覆", L"申复" },
    { L"畢昇", L"毕昇" },
    { L"發覆", L"发复" },
    { L"瞭如", L"了如" },
    { L"瞭如指掌", L"了如指掌" },
    { L"瞭望", L"瞭望" },
    { L"瞭然", L"了然" },
    { L"瞭然於心", L"了然于心" },
    { L"瞭若指掌", L"了若指掌" },
    { L"瞭解", L"了解" },
    { L"瞭解到", L"了解到" },
    { L"示覆", L"示复" },
    { L"神祇", L"神祇" },
    { L"稟覆", L"禀复" },
    { L"竺乾", L"竺乾" },
    { L"答覆", L"答复" },
    { L"篤麼", L"笃麽" },
    { L"簡單明瞭", L"简单明了" },
    { L"籌畫", L"筹划" },
    { L"素藉", L"素借" },
    { L"老態龍鍾", L"老态龙钟" },
    { L"肘手鍊足", L"肘手链足" },
    { L"萬鍾", L"万锺" },
    { L"蒜薹", L"蒜薹" },
    { L"蕓薹", L"芸薹" },
    { L"蕩覆", L"荡复" },
    { L"蕭乾", L"萧乾" },
    { L"藉代", L"借代" },
    { L"藉以", L"借以" },
    { L"藉助", L"借助" },
    { L"藉助於", L"借助于" },
    { L"藉卉", L"借卉" },
    { L"藉口", L"借口" },
    { L"藉喻", L"借喻" },
    { L"藉寇兵", L"借寇兵" },
    { L"藉手", L"借手" },
    { L"藉據", L"借据" },
    { L"藉故", L"借故" },
    { L"藉故推辭", L"借故推辞" },
    { L"藉方", L"借方" },
    { L"藉條", L"借条" },
    { L"藉槁", L"借槁" },
    { L"藉機", L"借机" },
    { L"藉此", L"借此" },
    { L"藉此機會", L"借此机会" },
    { L"藉甚", L"借甚" },
    { L"藉由", L"借由" },
    { L"藉着", L"借着" },
    { L"藉端", L"借端" },
    { L"藉端生事", L"借端生事" },
    { L"藉箸代籌", L"借箸代筹" },
    { L"藉草枕塊", L"借草枕块" },
    { L"藉藉", L"藉藉" },
    { L"藉藉无名", L"藉藉无名" },
    { L"藉詞", L"借词" },
    { L"藉讀", L"借读" },
    { L"藉資", L"借资" },
    { L"衹得", L"只得" },
    { L"衹見樹木", L"只见树木" },
    { L"袖裏乾坤", L"袖里乾坤" },
    { L"覆上", L"复上" },
    { L"覆住", L"复住" },
    { L"覆信", L"复信" },
    { L"覆冒", L"复冒" },
    { L"覆呈", L"复呈" },
    { L"覆命", L"复命" },
    { L"覆墓", L"复墓" },
    { L"覆宗", L"复宗" },
    { L"覆帳", L"复帐" },
    { L"覆幬", L"复帱" },
    { L"覆成", L"复成" },
    { L"覆按", L"复按" },
    { L"覆文", L"复文" },
    { L"覆杯", L"复杯" },
    { L"覆校", L"复校" },
    { L"覆瓿", L"复瓿" },
    { L"覆盂", L"复盂" },
    { L"覆盆", L"覆盆" },
    { L"覆盆子", L"覆盆子" },
    { L"覆盤", L"覆盘" },
    { L"覆育", L"复育" },
    { L"覆蕉尋鹿", L"复蕉寻鹿" },
    { L"覆逆", L"复逆" },
    { L"覆醢", L"复醢" },
    { L"覆醬瓿", L"复酱瓿" },
    { L"覆電", L"复电" },
    { L"覆露", L"复露" },
    { L"覆鹿尋蕉", L"复鹿寻蕉" },
    { L"覆鹿遺蕉", L"复鹿遗蕉" },
    { L"覆鼎", L"复鼎" },
    { L"見覆", L"见复" },
    { L"角徵", L"角徵" },
    { L"角徵羽", L"角徵羽" },
    { L"計畫", L"计划" },
    { L"變徵", L"变徵" },
    { L"變徵之聲", L"变徵之声" },
    { L"變徵之音", L"变徵之音" },
    { L"貂覆額", L"貂复额" },
    { L"買臣覆水", L"买臣复水" },
    { L"踅門瞭戶", L"踅门了户" },
    { L"躪藉", L"躏借" },
    { L"醞藉", L"酝借" },
    { L"重覆", L"重复" },
    { L"金吒", L"金吒" },
    { L"金鍊", L"金链" },
    { L"鈞覆", L"钧复" },
    { L"鉅子", L"钜子" },
    { L"鉅萬", L"钜万" },
    { L"鉅防", L"钜防" },
    { L"鉸鍊", L"铰链" },
    { L"銀鍊", L"银链" },
    { L"錢鍾書", L"钱锺书" },
    { L"鍊墜", L"链坠" },
    { L"鍊子", L"链子" },
    { L"鍊形", L"链形" },
    { L"鍊條", L"链条" },
    { L"鍊錘", L"链锤" },
    { L"鍊鎖", L"链锁" },
    { L"鍛鍾", L"锻锺" },
    { L"鍾繇", L"钟繇" },
    { L"鍾鍛", L"锺锻" },
    { L"鍾馗", L"锺馗" },
    { L"鎖鍊", L"锁链" },
    { L"鐵鍊", L"铁链" },
    { L"鑽石項鍊", L"钻石项链" },
    { L"雁杳魚沈", L"雁杳鱼沉" },
    { L"雖覆能復", L"虽覆能复" },
    { L"電覆", L"电复" },
    { L"露覆", L"露复" },
    { L"項鍊", L"项链" },
    { L"頗覆", L"颇复" },
    { L"頸鍊", L"颈链" },
    { L"顛乾倒坤", L"颠乾倒坤" },
    { L"顛倒乾坤", L"颠倒乾坤" },
    { L"顧藉", L"顾借" },
    { L"麼些族", L"麽些族" },
    { L"黄鍾公", L"黄锺公" },
    { L"龍鍾", L"龙钟" },
    { L"甚麼", L"什么" },
};

} // namespace

CharacterStandard CharacterStandardFromRawValue(int value)
{
    switch (value)
    {
    case 1:
        return CharacterStandard::Preset;
    case 2:
        return CharacterStandard::Custom;
    case 3:
        return CharacterStandard::Inherited;
    case 4:
        return CharacterStandard::Etymology;
    case 5:
        return CharacterStandard::OpenCC;
    case 6:
        return CharacterStandard::HongKong;
    case 7:
        return CharacterStandard::Taiwan;
    case 8:
        return CharacterStandard::PRCGeneral;
    case 9:
        return CharacterStandard::AncientBooksPublishing;
    case 51:
        return CharacterStandard::Mutilated;
    default:
        return CharacterStandard::Preset;
    }
}

bool IsMutilated(CharacterStandard standard)
{
    return standard == CharacterStandard::Mutilated;
}

bool IsTraditional(CharacterStandard standard)
{
    return !IsMutilated(standard);
}

bool IsNoOpCharacterStandard(CharacterStandard standard)
{
    return standard == CharacterStandard::Preset ||
        standard == CharacterStandard::Custom ||
        standard == CharacterStandard::Etymology ||
        standard == CharacterStandard::OpenCC;
}

namespace Ime {

std::wstring ConvertText(const ImeDatabase& database, std::wstring_view text, CharacterStandard standard)
{
    if (text.empty() || IsNoOpCharacterStandard(standard))
    {
        return std::wstring(text);
    }

    switch (standard)
    {
    case CharacterStandard::Inherited:
    case CharacterStandard::HongKong:
    case CharacterStandard::Taiwan:
    case CharacterStandard::AncientBooksPublishing:
    {
        ImeDatabase::VariantLookup lookup = database.CreateVariantLookup(standard);
        return ConvertSegments(SplitScalars(text), lookup);
    }
    case CharacterStandard::PRCGeneral:
        return PhraseAwareConvert(database, text, standard, prcGeneralPhrases);
    case CharacterStandard::Mutilated:
        return PhraseAwareConvert(database, text, standard, mutilatedPhrases);
    default:
        return std::wstring(text);
    }
}

bool IsIdeographicCodePoint(uint32_t codePoint)
{
    return (0x4E00 <= codePoint && codePoint <= 0x9FFF) ||
        (0x3400 <= codePoint && codePoint <= 0x4DBF) ||
        (0x20000 <= codePoint && codePoint <= 0x2A6DF) ||
        (0x2A700 <= codePoint && codePoint <= 0x2B73F) ||
        (0x2B740 <= codePoint && codePoint <= 0x2B81F) ||
        (0x2B820 <= codePoint && codePoint <= 0x2CEAF) ||
        (0x2CEB0 <= codePoint && codePoint <= 0x2EBEF) ||
        (0x30000 <= codePoint && codePoint <= 0x3134F) ||
        (0x31350 <= codePoint && codePoint <= 0x323AF) ||
        (0x2EBF0 <= codePoint && codePoint <= 0x2EE5F) ||
        (0x323B0 <= codePoint && codePoint <= 0x33479) ||
        codePoint == 0x3007;
}

namespace CharacterStandardConverterDetail {

PCWSTR VariantTableName(CharacterStandard standard)
{
    switch (standard)
    {
    case CharacterStandard::Inherited:
        return L"variant_old";
    case CharacterStandard::HongKong:
        return L"variant_hk";
    case CharacterStandard::Taiwan:
        return L"variant_tw";
    case CharacterStandard::PRCGeneral:
        return L"variant_prc";
    case CharacterStandard::AncientBooksPublishing:
        return L"variant_abp";
    case CharacterStandard::Mutilated:
        return L"variant_sim";
    default:
        return L"";
    }
}

} // namespace CharacterStandardConverterDetail

} // namespace Ime
