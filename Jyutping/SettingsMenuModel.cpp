#include "Private.h"
#include "Settings.h"
#include "LanguageBar.h"
#include "SettingsMenuModel.h"
#include "Localization.h"
#include "resource.h"

#include <set>

namespace SettingsMenu {

namespace {

std::wstring LoadMenuString(UINT resourceId, PCWSTR fallback)
{
    return Localization::LoadStringOrFallback(resourceId, fallback);
}

Item CommandItem(UINT id, std::wstring text, BOOL checked = FALSE, BOOL enabled = TRUE)
{
    Item item;
    item.kind = ItemKind::Command;
    item.id = id;
    item.text = std::move(text);
    item.enabled = enabled;
    item.checked = checked;
    return item;
}

Item SubmenuItem(UINT id, std::wstring text, std::vector<Item> children)
{
    Item item;
    item.kind = ItemKind::Submenu;
    item.id = id;
    item.text = std::move(text);
    item.children = std::move(children);
    return item;
}

Item SeparatorItem()
{
    Item item;
    item.kind = ItemKind::Separator;
    item.id = Separator;
    item.enabled = FALSE;
    return item;
}

std::vector<Item> FontSizeItems(UINT firstId, DWORD currentFontSize)
{
    std::vector<Item> items;
    for (DWORD fontSize = MinimumCandidateFontSize; fontSize <= MaximumCandidateFontSize; fontSize++)
    {
        WCHAR text[3] = {};
        StringCchPrintf(text, ARRAYSIZE(text), L"%lu", fontSize);
        items.push_back(CommandItem(FontSizeId(firstId, fontSize), text, currentFontSize == fontSize));
    }
    return items;
}

HRESULT AddTfItems(_In_ ITfMenu* menu, _In_ const std::vector<Item>& items)
{
    for (const Item& item : items)
    {
        DWORD flags = 0;
        if (item.kind == ItemKind::Submenu)
        {
            flags |= TF_LBMENUF_SUBMENU;
        }
        else if (item.kind == ItemKind::Separator)
        {
            flags |= TF_LBMENUF_SEPARATOR;
        }
        if (!item.enabled)
        {
            flags |= TF_LBMENUF_GRAYED;
        }
        if (item.checked)
        {
            flags |= TF_LBMENUF_RADIOCHECKED;
        }

        ITfMenu* submenu = nullptr;
        const WCHAR* text = item.kind == ItemKind::Separator ? nullptr : item.text.c_str();
        ULONG textLength = item.kind == ItemKind::Separator ? 0 : static_cast<ULONG>(item.text.length());
        HRESULT hr = menu->AddMenuItem(item.id, flags, nullptr, nullptr, text, textLength, item.kind == ItemKind::Submenu ? &submenu : nullptr);
        if (FAILED(hr))
        {
            return hr;
        }
        if (submenu != nullptr)
        {
            hr = AddTfItems(submenu, item.children);
            submenu->Release();
            if (FAILED(hr))
            {
                return hr;
            }
        }
    }
    return S_OK;
}

BOOL ValidateItems(_In_ const std::vector<Item>& items, _Inout_ std::set<UINT>& ids)
{
    for (const Item& item : items)
    {
        if (item.id == 0 || !ids.insert(item.id).second)
        {
            return FALSE;
        }
        if (item.kind == ItemKind::Submenu)
        {
            if (item.children.empty() || !ValidateItems(item.children, ids))
            {
                return FALSE;
            }
        }
        else if (!item.children.empty())
        {
            return FALSE;
        }
    }
    return TRUE;
}

}

UINT FontSizeId(UINT firstId, DWORD fontSize)
{
    return firstId + fontSize - MinimumCandidateFontSize;
}

DWORD FontSizeFromId(UINT firstId, UINT id)
{
    return MinimumCandidateFontSize + id - firstId;
}

UINT CandidatePageSizeId(DWORD pageSize)
{
    return CandidatePageSizeFirst + pageSize - 1;
}

DWORD CandidatePageSizeFromId(UINT id)
{
    return id - CandidatePageSizeFirst + 1;
}

Snapshot BuildSnapshot(_In_ const ILangBarItemButtonSettingsMenuHandler& handler)
{
    Snapshot snapshot;
    snapshot.items.push_back(SubmenuItem(CandidateFontSize, LoadMenuString(IDS_MENU_CANDIDATE_FONT_SIZE, L"Candidate Font Size"), FontSizeItems(CandidateFontSizeFirst, handler.CurrentCandidateFontSize())));
    snapshot.items.push_back(SubmenuItem(CandidateNumberFontSize, LoadMenuString(IDS_MENU_CANDIDATE_NUMBER_FONT_SIZE, L"Candidate Number Font Size"), FontSizeItems(CandidateNumberFontSizeFirst, handler.CurrentCandidateNumberFontSize())));
    snapshot.items.push_back(SubmenuItem(CandidateCommentFontSize, LoadMenuString(IDS_MENU_CANDIDATE_COMMENT_FONT_SIZE, L"Candidate Comment Font Size"), FontSizeItems(CandidateCommentFontSizeFirst, handler.CurrentCandidateCommentFontSize())));

    std::vector<Item> pageSizeItems;
    DWORD currentPageSize = handler.CurrentCandidatePageSize();
    for (DWORD pageSize = 1; pageSize <= 10; pageSize++)
    {
        WCHAR text[3] = {};
        StringCchPrintf(text, ARRAYSIZE(text), L"%lu", pageSize);
        pageSizeItems.push_back(CommandItem(CandidatePageSizeId(pageSize), text, currentPageSize == pageSize));
    }
    snapshot.items.push_back(SubmenuItem(CandidatePageSize, LoadMenuString(IDS_MENU_CANDIDATE_PAGE_SIZE, L"Candidate Count per Page"), std::move(pageSizeItems)));

    ::PunctuationForm punctuationForm = handler.CurrentPunctuationForm();
    std::vector<Item> punctuationItems;
    punctuationItems.push_back(CommandItem(PunctuationFormCantonese, LoadMenuString(IDS_MENU_PUNCTUATION_FORM_CANTONESE, L"Cantonese"), punctuationForm == ::PunctuationForm::Cantonese));
    punctuationItems.push_back(CommandItem(PunctuationFormEnglish, LoadMenuString(IDS_MENU_PUNCTUATION_FORM_ENGLISH, L"English"), punctuationForm == ::PunctuationForm::English));
    snapshot.items.push_back(SubmenuItem(PunctuationForm, LoadMenuString(IDS_MENU_PUNCTUATION_FORM, L"Punctuation Form"), std::move(punctuationItems)));

    ::CharacterForm characterForm = handler.CurrentCharacterForm();
    std::vector<Item> characterFormItems;
    characterFormItems.push_back(CommandItem(CharacterFormHalfWidth, LoadMenuString(IDS_MENU_CHARACTER_FORM_HALF_WIDTH, L"Half-width"), characterForm == ::CharacterForm::HalfWidth));
    characterFormItems.push_back(CommandItem(CharacterFormFullWidth, LoadMenuString(IDS_MENU_CHARACTER_FORM_FULL_WIDTH, L"Full-width"), characterForm == ::CharacterForm::FullWidth));
    snapshot.items.push_back(SubmenuItem(CharacterForm, LoadMenuString(IDS_MENU_CHARACTER_FORM, L"Character Form"), std::move(characterFormItems)));

    ::CharacterVariant variant = handler.CurrentCharacterVariant();
    std::vector<Item> variantItems;
    variantItems.push_back(CommandItem(CharacterVariantTraditional, LoadMenuString(IDS_MENU_CHARACTER_VARIANT_TRADITIONAL, L"Traditional"), variant == ::CharacterVariant::Traditional));
    variantItems.push_back(CommandItem(CharacterVariantHongKong, LoadMenuString(IDS_MENU_CHARACTER_VARIANT_HONG_KONG, L"Hong Kong"), variant == ::CharacterVariant::HongKong));
    variantItems.push_back(CommandItem(CharacterVariantTaiwan, LoadMenuString(IDS_MENU_CHARACTER_VARIANT_TAIWAN, L"Taiwan"), variant == ::CharacterVariant::Taiwan));
    variantItems.push_back(CommandItem(CharacterVariantSimplified, LoadMenuString(IDS_MENU_CHARACTER_VARIANT_SIMPLIFIED, L"Simplified"), variant == ::CharacterVariant::Simplified));
    snapshot.items.push_back(SubmenuItem(CharacterVariant, LoadMenuString(IDS_MENU_CHARACTER_VARIANT, L"Character Variants"), std::move(variantItems)));

    snapshot.items.push_back(SeparatorItem());
    snapshot.items.push_back(CommandItem(MoreSettings, LoadMenuString(IDS_MENU_MORE_SETTINGS, L"More Settings…"), FALSE, FALSE));

    assert(ValidateSnapshot(snapshot));
    return snapshot;
}

HRESULT PopulateTfMenu(_In_ ITfMenu* menu, _In_ const Snapshot& snapshot)
{
    if (menu == nullptr || !ValidateSnapshot(snapshot))
    {
        return E_INVALIDARG;
    }
    return AddTfItems(menu, snapshot.items);
}

BOOL ValidateSnapshot(_In_ const Snapshot& snapshot)
{
    std::set<UINT> ids;
    return !snapshot.items.empty() && ValidateItems(snapshot.items, ids);
}

}
