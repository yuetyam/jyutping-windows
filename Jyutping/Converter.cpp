#include "Converter.h"
#include "ImeDatabase.h"

#include <algorithm>

namespace {

void Append(std::vector<Ime::Lexicon>& target, const std::vector<Ime::Lexicon>& source)
{
    target.insert(target.end(), source.begin(), source.end());
}

bool MatchesSymbolTarget(const Ime::Lexicon& item, const Ime::Lexicon& symbol)
{
    return item.IsCantonese() &&
        symbol.attached &&
        item.text == *symbol.attached &&
        item.romanization == symbol.romanization;
}

} // namespace

namespace Ime {

std::vector<Candidate> Converter::Dispatch(
    const ImeDatabase& database,
    const std::vector<Lexicon>& memory,
    const std::vector<Lexicon>& defined,
    const std::vector<Lexicon>& texts,
    const std::vector<Lexicon>& symbols,
    const std::vector<Lexicon>& queried,
    RomanizationForm romanizationForm,
    CharacterStandard standard)
{
    std::vector<Lexicon> idealMemory;
    std::vector<Lexicon> notIdealMemory;
    for (const Lexicon& item : memory)
    {
        if (item.IsIdealInputMemory())
        {
            idealMemory.push_back(item);
        }
        else if (item.IsNotIdealInputMemory())
        {
            notIdealMemory.push_back(item);
        }
    }

    std::vector<Lexicon> chained;
    chained.reserve(queried.size() + notIdealMemory.size());
    for (const Lexicon& item : queried)
    {
        if (idealMemory.empty() || !item.IsCompound())
        {
            chained.push_back(item);
        }
    }

    for (auto iterator = notIdealMemory.rbegin(); iterator != notIdealMemory.rend(); ++iterator)
    {
        auto position = std::find_if(chained.begin(), chained.end(), [&iterator](const Lexicon& item)
        {
            return item.inputCount <= iterator->inputCount;
        });
        chained.insert(position, *iterator);
    }

    std::vector<Lexicon> merged;
    Append(merged, std::vector<Lexicon>(idealMemory.begin(), idealMemory.begin() + (std::min)(idealMemory.size(), size_t(3))));
    Append(merged, defined);
    Append(merged, texts);
    Append(merged, idealMemory);
    Append(merged, chained);

    for (auto iterator = symbols.rbegin(); iterator != symbols.rend(); ++iterator)
    {
        auto position = std::find_if(merged.begin(), merged.end(), [&iterator](const Lexicon& item)
        {
            return MatchesSymbolTarget(item, *iterator);
        });
        if (position != merged.end())
        {
            merged.insert(position + 1, *iterator);
        }
    }
    return Transform(database, merged, romanizationForm, standard);
}

std::vector<Candidate> Converter::Transform(
    const ImeDatabase& database,
    const std::vector<Lexicon>& lexicons,
    RomanizationForm romanizationForm,
    CharacterStandard standard)
{
    std::vector<Candidate> result;
    result.reserve(lexicons.size());
    for (const Lexicon& lexicon : lexicons)
    {
        std::optional<std::wstring> displayText;
        std::optional<std::wstring> composedComment;
        if (lexicon.IsCantonese())
        {
            displayText = ConvertText(database, lexicon.text, standard);
        }
        else if (lexicon.type == LexiconType::Composed && lexicon.attached && !lexicon.attached->empty())
        {
            composedComment = ConvertText(database, *lexicon.attached, standard);
        }

        Candidate candidate(lexicon, romanizationForm, std::move(displayText), std::move(composedComment));
        if (std::find(result.begin(), result.end(), candidate) == result.end())
        {
            result.push_back(std::move(candidate));
        }
    }
    return result;
}

} // namespace Ime
