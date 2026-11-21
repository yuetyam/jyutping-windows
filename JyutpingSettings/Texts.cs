using System.Globalization;

namespace JyutpingSettings;

internal static class Texts
{
    private static readonly string Language = CultureInfo.CurrentUICulture.Name;
    private static readonly bool IsCantonese = Language.StartsWith("yue", StringComparison.OrdinalIgnoreCase);
    private static readonly bool IsWrittenHongKongChinese = Language.Equals("zh-HK", StringComparison.OrdinalIgnoreCase) ||
        Language.Equals("zh-Hant-HK", StringComparison.OrdinalIgnoreCase);

    private static readonly Dictionary<string, (string Cantonese, string WrittenChinese)> Translations = new(StringComparer.Ordinal)
    {
        ["Jyutping Settings"] = ("粵拼設定", "粵拼設定"),
        ["General"] = ("一般", "一般"),
        ["Reverse Lookup"] = ("反查", "反查"),
        ["Tones"] = ("聲調", "聲調"),
        ["Fonts"] = ("字體", "字體"),
        ["Input Memory"] = ("輸入記憶", "輸入記憶"),
        ["Hotkeys"] = ("快捷鍵", "快捷鍵"),
        ["About"] = ("關於", "關於"),
        ["Character width"] = ("字元闊度", "字元寬度"),
        ["Half width"] = ("半形", "半形"),
        ["Full width"] = ("全形", "全形"),
        ["Punctuation"] = ("標點符號", "標點符號"),
        ["Cantonese"] = ("粵語", "粵語"),
        ["English"] = ("英文", "英文"),
        ["Character variant"] = ("漢字字形", "漢字字形"),
        ["Traditional"] = ("繁體", "繁體"),
        ["Hong Kong"] = ("香港", "香港"),
        ["Taiwan"] = ("臺灣", "臺灣"),
        ["Simplified"] = ("簡體", "簡體"),
        ["Candidates per page"] = ("每頁候選項數目", "每頁候選項數量"),
        ["Candidate size"] = ("候選字大小", "候選字大小"),
        ["Number size"] = ("序號大小", "序號大小"),
        ["Jyutping size"] = ("粵拼大小", "粵拼大小"),
        ["Connecting to Jyutping"] = ("正在連接粵拼", "正在連接粵拼"),
        ["Jyutping settings are unavailable. The app will reconnect."] = ("暫時讀唔到粵拼設定。程式會重新連接。", "暫時無法讀取粵拼設定。應用程式將重新連接。"),
        ["The setting could not be saved."] = ("儲存唔到呢項設定。", "無法儲存此設定。"),
        ["Type the prefix followed by the lookup code."] = ("先打前綴，再打反查碼。", "請先輸入前綴，再輸入反查碼。"),
        ["r   Pinyin"] = ("r   普通話拼音", "r   普通話拼音"),
        ["v   Cangjie"] = ("v   倉頡", "v   倉頡"),
        ["x   Stroke"] = ("x   筆畫", "x   筆畫"),
        ["q   Character structure"] = ("q   拆字", "q   拆字"),
        ["Use v, x, and q for tones 1, 2, and 3. Double the letter for tones 4, 5, and 6."] = ("用 v、x、q 輸入第一、二、三聲。撳兩次同一鍵可輸入第四、五、六聲。", "使用 v、x、q 輸入第一、二、三聲。連續輸入兩次相同字母可輸入第四、五、六聲。"),
        ["Space selects the highlighted candidate."] = ("撳空格鍵揀選反白嘅候選項。", "按空格鍵選取反白的候選項。"),
        ["Enter commits the typed text."] = ("撳 Enter 輸出已輸入嘅文字。", "按 Enter 鍵輸出已輸入的文字。"),
        ["Escape cancels the composition."] = ("撳 Esc 取消組字。", "按 Esc 鍵取消組字。"),
        ["Jyutping website"] = ("粵拼網站", "粵拼網站"),
        ["Clear learned words"] = ("清除學習詞語", "清除學習詞語"),
        ["Clear learned words?"] = ("清除所有學習詞語？", "是否清除所有學習詞語？"),
        ["This removes the words Jyutping has learned."] = ("呢個動作會移除粵拼已學嘅詞語。", "此操作會移除粵拼已學習的詞語。"),
        ["Clear"] = ("清除", "清除"),
        ["Cancel"] = ("取消", "取消"),
        ["Learned words cleared."] = ("已清除學習詞語。", "已清除學習詞語。"),
        ["Could not clear learned words."] = ("清除唔到學習詞語。", "無法清除學習詞語。"),
        ["Import learned words"] = ("匯入學習詞語", "匯入學習詞語"),
        ["Export learned words"] = ("匯出學習詞語", "匯出學習詞語"),
        ["Jyutping memory file"] = ("粵拼輸入記憶檔案", "粵拼輸入記憶檔案"),
        ["Importing learned words…"] = ("正在匯入學習詞語…", "正在匯入學習詞語…"),
        ["Exporting learned words…"] = ("正在匯出學習詞語…", "正在匯出學習詞語…"),
        ["Learned words imported."] = ("已匯入學習詞語。", "已匯入學習詞語。"),
        ["Learned words exported."] = ("已匯出學習詞語。", "已匯出學習詞語。"),
        ["Memory transfer cancelled."] = ("已取消匯入或匯出。", "已取消匯入或匯出。"),
        ["Memory transfer failed."] = ("匯入或匯出失敗。", "匯入或匯出失敗。"),
    };

    internal static string Get(string english)
    {
        if ((IsCantonese || IsWrittenHongKongChinese) && Translations.TryGetValue(english, out var translated))
        {
            return IsCantonese ? translated.Cantonese : translated.WrittenChinese;
        }
        return english;
    }
}
