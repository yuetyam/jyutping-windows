#pragma once

#include "Settings.h"

#include <string>
#include <vector>

class ILangBarItemButtonSettingsMenuHandler;

namespace SettingsMenu {

enum class ItemKind
{
    Command,
    Submenu,
    Separator
};

struct Item
{
    ItemKind kind = ItemKind::Command;
    UINT id = 0;
    std::wstring text;
    BOOL enabled = TRUE;
    BOOL checked = FALSE;
    std::vector<Item> children;
};

struct Snapshot
{
    std::vector<Item> items;
};

constexpr UINT CandidateFontSize = 1;
constexpr UINT CandidateFontSizeFirst = CandidateFontSize + 1;
constexpr UINT CandidateFontSizeLast = CandidateFontSizeFirst + MaximumCandidateFontSize - MinimumCandidateFontSize;
constexpr UINT CandidateNumberFontSize = CandidateFontSizeLast + 1;
constexpr UINT CandidateNumberFontSizeFirst = CandidateNumberFontSize + 1;
constexpr UINT CandidateNumberFontSizeLast = CandidateNumberFontSizeFirst + MaximumCandidateFontSize - MinimumCandidateFontSize;
constexpr UINT CandidateCommentFontSize = CandidateNumberFontSizeLast + 1;
constexpr UINT CandidateCommentFontSizeFirst = CandidateCommentFontSize + 1;
constexpr UINT CandidateCommentFontSizeLast = CandidateCommentFontSizeFirst + MaximumCandidateFontSize - MinimumCandidateFontSize;
constexpr UINT CandidatePageSize = CandidateCommentFontSizeLast + 1;
constexpr UINT CandidatePageSizeFirst = CandidatePageSize + 1;
constexpr UINT CandidatePageSizeLast = CandidatePageSizeFirst + 9;
constexpr UINT PunctuationForm = CandidatePageSizeLast + 1;
constexpr UINT PunctuationFormCantonese = PunctuationForm + 1;
constexpr UINT PunctuationFormEnglish = PunctuationFormCantonese + 1;
constexpr UINT CharacterForm = PunctuationFormEnglish + 1;
constexpr UINT CharacterFormHalfWidth = CharacterForm + 1;
constexpr UINT CharacterFormFullWidth = CharacterFormHalfWidth + 1;
constexpr UINT CharacterVariant = CharacterFormFullWidth + 1;
constexpr UINT CharacterVariantTraditional = CharacterVariant + 1;
constexpr UINT CharacterVariantHongKong = CharacterVariantTraditional + 1;
constexpr UINT CharacterVariantTaiwan = CharacterVariantHongKong + 1;
constexpr UINT CharacterVariantSimplified = CharacterVariantTaiwan + 1;
constexpr UINT Separator = CharacterVariantSimplified + 1;
constexpr UINT MoreSettings = Separator + 1;

Snapshot BuildSnapshot(_In_ const ILangBarItemButtonSettingsMenuHandler& handler);
HRESULT PopulateTfMenu(_In_ ITfMenu* menu, _In_ const Snapshot& snapshot);
BOOL ValidateSnapshot(_In_ const Snapshot& snapshot);

UINT FontSizeId(UINT firstId, DWORD fontSize);
DWORD FontSizeFromId(UINT firstId, UINT id);
UINT CandidatePageSizeId(DWORD pageSize);
DWORD CandidatePageSizeFromId(UINT id);

}
