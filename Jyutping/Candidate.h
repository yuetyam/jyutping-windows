#pragma once

#include "ImeTypes.h"

#include <optional>
#include <string>

namespace Ime {

enum class RomanizationForm
{
    Full = 1,
    Toneless = 2,
    Nothing = 3
};

struct Candidate
{
    std::wstring text;
    std::optional<std::wstring> comment;
    std::optional<std::wstring> secondaryComment;
    Lexicon lexicon;

    Candidate() = default;
    Candidate(
        Lexicon source,
        RomanizationForm romanizationForm,
        std::optional<std::wstring> displayText = std::nullopt,
        std::optional<std::wstring> composedComment = std::nullopt);

    bool IsCantonese() const;
    bool IsNotCantonese() const;
};

bool operator==(const Candidate& left, const Candidate& right);
bool operator!=(const Candidate& left, const Candidate& right);

} // namespace Ime
