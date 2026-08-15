#pragma once

#include "Candidate.h"
#include "CharacterStandard.h"

#include <vector>

class ImeDatabase;

namespace Ime {

class Converter
{
public:
    static std::vector<Candidate> Dispatch(
        const ImeDatabase& database,
        const std::vector<Lexicon>& memory,
        const std::vector<Lexicon>& defined,
        const std::vector<Lexicon>& texts,
        const std::vector<Lexicon>& symbols,
        const std::vector<Lexicon>& queried,
        RomanizationForm romanizationForm,
        CharacterStandard standard);

    static std::vector<Candidate> Transform(
        const ImeDatabase& database,
        const std::vector<Lexicon>& lexicons,
        RomanizationForm romanizationForm,
        CharacterStandard standard);
};

} // namespace Ime
