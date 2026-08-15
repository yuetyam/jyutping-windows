#pragma once

#include "stdafx.h"
#include "Candidate.h"
#include "CoreImeEngine.h"
#include "InputMemory.h"
#include "sal.h"

#include <optional>
#include <string_view>
#include <vector>

namespace Ime {

class InputEngine
{
public:
    bool Prepare();
    bool Prepare(_In_z_ PCWSTR databasePath);
    bool IsPrepared() const;

    std::vector<Candidate> Suggest(
        std::wstring_view input,
        CharacterStandard standard,
        RomanizationForm romanizationForm = RomanizationForm::Full) const;
    std::vector<Candidate> Suggest(
        const std::vector<VirtualInputKey>& keys,
        CharacterStandard standard,
        RomanizationForm romanizationForm = RomanizationForm::Full) const;
    std::vector<Candidate> SearchPlainTexts(
        const std::vector<VirtualInputKey>& keys,
        CharacterStandard standard,
        RomanizationForm romanizationForm = RomanizationForm::Full) const;
    std::vector<Candidate> ReverseLookup(
        ReverseLookupMethod method,
        const std::vector<VirtualInputKey>& keys,
        CharacterStandard standard,
        RomanizationForm romanizationForm = RomanizationForm::Full) const;
    Segmentation Segment(const std::vector<VirtualInputKey>& keys) const;
    bool Remember(const Lexicon& lexicon);
    bool Forget(const Lexicon& lexicon);
    bool DeleteAllMemory();

private:
    CoreImeEngine _coreIme;
    InputMemory _inputMemory;
};

} // namespace Ime
