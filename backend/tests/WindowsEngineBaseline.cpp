#include "../../Jyutping/CharacterStandard.h"
#include "../../Jyutping/Converter.h"
#include "../../Jyutping/CoreImeEngine.h"
#include "../../Jyutping/InputMemory.h"
#include "../../Jyutping/Logger.h"
#include "../../Jyutping/Settings.h"

#include <iostream>
#include <stdexcept>

// The reference executable only opens the supplied dictionary and uses no TSF state.
namespace Global {
HINSTANCE dllInstanceHandle = nullptr;
void Log(PCWSTR, ...) {}
std::wstring memoryDirectory;
std::wstring UserDataDirectory() { return memoryDirectory; }
}

SettingsStore::SettingsStore() : _canPersist(true) {}

namespace {

std::string Utf8(std::wstring_view text)
{
    if (text.empty())
    {
        return {};
    }
    int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0)
    {
        throw std::runtime_error("invalid baseline text");
    }
    std::string result(length, '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), length, nullptr, nullptr))
    {
        throw std::runtime_error("failed to encode baseline text");
    }
    return result;
}

void PrintLexicon(const Ime::Lexicon& item)
{
    std::cout << static_cast<int>(item.type) << '\t' << item.number << '\t' << item.inputCount << '\t' << Utf8(item.text) << '\t'
        << Utf8(item.romanization) << '\t' << Utf8(item.input) << '\t' << Utf8(item.mark) << '\t'
        << Utf8(item.attached.value_or(L"")) << '\n';
}

void PrintLexicons(const std::vector<Ime::Lexicon>& lexicons)
{
    for (const auto& item : lexicons)
    {
        PrintLexicon(item);
    }
}

} // namespace

int wmain(int argc, wchar_t* argv[])
{
    if (argc < 2)
    {
        std::cerr << "Usage: WindowsEngineBaseline <database> [input...]" << std::endl;
        return 2;
    }
    Ime::CoreImeEngine engine;
    if (!engine.Prepare(argv[1]))
    {
        std::cerr << "Could not open the baseline dictionary" << std::endl;
        return 1;
    }
    if (argc > 2 && std::wstring_view(argv[2]) == L"--convert")
    {
        for (int index = 3; index < argc; ++index)
        {
            for (int standard : { 1, 2, 3, 4, 5, 6, 7, 8, 9, 51, -1 })
            {
                std::cout << Utf8(engine.ConvertText(argv[index], CharacterStandardFromRawValue(standard))) << '\n';
            }
        }
        return std::cout ? 0 : 1;
    }
    if (argc > 2 && std::wstring_view(argv[2]) == L"--reverse")
    {
        for (int index = 3; index < argc; ++index)
        {
            const std::wstring_view input = argv[index];
            std::cout << "input\t" << Utf8(input) << '\n';
            for (CangjieVariant variant : { CangjieVariant::Cangjie5, CangjieVariant::Cangjie3, CangjieVariant::Quick5, CangjieVariant::Quick3 })
            {
                std::cout << "lookup\n";
                PrintLexicons(engine.CangjieReverseLookup(input, variant));
            }
            for (auto method : { Ime::ReverseLookupMethod::Stroke, Ime::ReverseLookupMethod::Structure, Ime::ReverseLookupMethod::Pinyin })
            {
                std::cout << "lookup\n";
                PrintLexicons(engine.ReverseLookup(method, input));
            }
        }
        return std::cout ? 0 : 1;
    }
    if (argc >= 4 && std::wstring_view(argv[2]) == L"--memory")
    {
        Global::memoryDirectory = argv[3];
        Ime::InputMemory memory;
        if (!memory.Prepare())
        {
            std::cerr << "Could not open the isolated memory database" << std::endl;
            return 1;
        }
        for (int index = 4; index < argc; ++index)
        {
            const std::wstring_view input = argv[index];
            const auto keys = Ime::InputKeysFromText(input);
            std::cout << "input\t" << Utf8(input) << '\n';
            PrintLexicons(memory.Suggest(keys, engine.Segment(keys), engine.SegmenterForMemory()));
        }
        return std::cout ? 0 : 1;
    }

    for (int index = 2; index < argc; index++)
    {
        std::wstring_view input = argv[index];
        auto keys = Ime::InputKeysFromText(input);
        auto segmentation = engine.Segment(keys);
        std::cout << "input\t" << Utf8(input) << '\n';
        for (const auto& scheme : segmentation)
        {
            std::cout << "scheme\t" << Utf8(Ime::SchemeMark(scheme)) << '\t' << Utf8(Ime::SchemeSyllableText(scheme)) << '\n';
        }
        std::cout << "shallow\n";
        PrintLexicons(engine.Suggest(keys, segmentation, false));
        auto deep = engine.Suggest(keys, segmentation, true);
        std::cout << "deep\n";
        PrintLexicons(deep);
        auto texts = engine.SearchPlainTexts(keys);
        std::cout << "text\n";
        PrintLexicons(texts);
        auto symbols = engine.SearchSymbols(keys, segmentation);
        std::cout << "symbols\n";
        PrintLexicons(symbols);

        constexpr std::pair<CharacterStandard, Ime::RomanizationForm> options[] = {
            { CharacterStandard::Preset, Ime::RomanizationForm::Full },
            { CharacterStandard::HongKong, Ime::RomanizationForm::Toneless },
            { CharacterStandard::Mutilated, Ime::RomanizationForm::Nothing },
        };
        for (const auto& [standard, form] : options)
        {
            std::cout << "display\t" << static_cast<int>(standard) << '\n';
            for (const auto& candidate : Ime::Converter::Dispatch(engine.Database(), {}, {}, texts, symbols, deep, form, standard))
            {
                std::cout << Utf8(candidate.text) << '\t' << Utf8(candidate.comment.value_or(L"")) << '\t';
                PrintLexicon(candidate.lexicon);
            }
        }
    }
    return std::cout ? 0 : 1;
}
