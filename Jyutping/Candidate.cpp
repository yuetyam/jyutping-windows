#include "Candidate.h"

namespace Ime {

Candidate::Candidate(
    Lexicon source,
    RomanizationForm romanizationForm,
    std::optional<std::wstring> displayText,
    std::optional<std::wstring> composedComment) :
    text(displayText.value_or(source.text)),
    lexicon(std::move(source))
{
    switch (lexicon.type)
    {
    case LexiconType::Cantonese:
        switch (romanizationForm)
        {
        case RomanizationForm::Full:
            comment = lexicon.romanization;
            break;
        case RomanizationForm::Toneless:
            comment = StrippedTones(lexicon.romanization);
            break;
        case RomanizationForm::Nothing:
            break;
        }
        break;
    case LexiconType::Composed:
        comment = std::move(composedComment);
        if (!lexicon.romanization.empty())
        {
            secondaryComment = lexicon.romanization;
        }
        break;
    default:
        break;
    }
}

bool Candidate::IsCantonese() const
{
    return lexicon.IsCantonese();
}

bool Candidate::IsNotCantonese() const
{
    return lexicon.IsNotCantonese();
}

bool operator==(const Candidate& left, const Candidate& right)
{
    if (left.IsCantonese() && right.IsCantonese() && !left.comment)
    {
        return left.text == right.text &&
            StrippedTones(left.lexicon.romanization) == StrippedTones(right.lexicon.romanization);
    }
    return left.text == right.text && left.comment == right.comment;
}

bool operator!=(const Candidate& left, const Candidate& right)
{
    return !(left == right);
}

} // namespace Ime
