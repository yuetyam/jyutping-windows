#include "InputEngine.h"
#include "Converter.h"
#include "Logger.h"

namespace Ime {

bool InputEngine::Prepare()
{
    if (!_coreIme.Prepare())
    {
        return false;
    }

    bool isMemoryReady = _inputMemory.Prepare();
    if (!isMemoryReady)
    {
        Global::Log(L"InputEngine prepare: input memory is unavailable; continuing without memory suggestions");
    }
    Global::Log(L"InputEngine prepare success: memory=%d", isMemoryReady);
    return true;
}

bool InputEngine::Prepare(_In_z_ PCWSTR databasePath)
{
    if (!_coreIme.Prepare(databasePath))
    {
        return false;
    }

    bool isMemoryReady = _inputMemory.Prepare();
    if (!isMemoryReady)
    {
        Global::Log(L"InputEngine prepare: input memory is unavailable; continuing without memory suggestions");
    }
    Global::Log(L"InputEngine prepare success: memory=%d", isMemoryReady);
    return true;
}

bool InputEngine::IsPrepared() const
{
    return _coreIme.IsPrepared();
}

std::vector<Candidate> InputEngine::Suggest(
    std::wstring_view input,
    CharacterStandard standard,
    RomanizationForm romanizationForm) const
{
    return Suggest(InputKeysFromText(input), standard, romanizationForm);
}

std::vector<Candidate> InputEngine::Suggest(
    const std::vector<VirtualInputKey>& keys,
    CharacterStandard standard,
    RomanizationForm romanizationForm) const
{
    if (!_coreIme.IsPrepared() || keys.empty())
    {
        return std::vector<Candidate>();
    }

    Segmentation segmentation = _coreIme.Segment(keys);
    std::vector<Lexicon> queried = _coreIme.Suggest(keys, segmentation);
    std::vector<Lexicon> texts = _coreIme.SearchPlainTexts(keys);
    std::vector<Lexicon> symbols = _coreIme.SearchSymbols(keys, segmentation);
    std::vector<Lexicon> memory;
    if (_inputMemory.IsPrepared())
    {
        memory = _inputMemory.Suggest(keys, segmentation, _coreIme.SegmenterForMemory());
    }

    return Converter::Dispatch(
        _coreIme.Database(),
        memory,
        std::vector<Lexicon>(),
        texts,
        symbols,
        queried,
        romanizationForm,
        standard);
}

std::vector<Candidate> InputEngine::SearchPlainTexts(
    const std::vector<VirtualInputKey>& keys,
    CharacterStandard standard,
    RomanizationForm romanizationForm) const
{
    return Converter::Transform(
        _coreIme.Database(),
        _coreIme.SearchPlainTexts(keys),
        romanizationForm,
        standard);
}

std::vector<Candidate> InputEngine::ReverseLookup(
    ReverseLookupMethod method,
    const std::vector<VirtualInputKey>& keys,
    CharacterStandard standard,
    RomanizationForm romanizationForm) const
{
    return Converter::Transform(
        _coreIme.Database(),
        _coreIme.ReverseLookup(method, keys),
        romanizationForm,
        standard);
}

Segmentation InputEngine::Segment(const std::vector<VirtualInputKey>& keys) const
{
    return _coreIme.Segment(keys);
}

bool InputEngine::Remember(const Lexicon& lexicon)
{
    return _inputMemory.Handle(lexicon);
}

bool InputEngine::Forget(const Lexicon& lexicon)
{
    return _inputMemory.Forget(lexicon);
}

bool InputEngine::DeleteAllMemory()
{
    bool result = _inputMemory.DeleteAll();
    Global::Log(L"InputEngine delete all memory: result=%d", result);
    return result;
}

} // namespace Ime
