#include "Segmenter.h"
#include "Globals.h"
#include "Logger.h"

#include <algorithm>

namespace {

constexpr size_t maxSyllableKeyCount = 6;
constexpr int64_t codeMaaMaa = 32203220;
constexpr int64_t codeMaaMi = 32203228;

struct SplitEdge
{
    Ime::Syllable syllable;
    size_t endIndex = 0;
};

struct SplitNode
{
    Ime::Syllable syllable;
    std::optional<size_t> previousIndex;
    size_t length = 0;
    size_t count = 0;
};

Ime::Segmentation SingleScheme(Ime::Syllable syllable)
{
    return Ime::Segmentation{ Ime::Scheme{ std::move(syllable) } };
}

Ime::Segmentation MaaMaaScheme()
{
    return Ime::Segmentation{
        Ime::Scheme{
            Ime::Syllable(3220, 322020),
            Ime::Syllable(3220, 322020)
        }
    };
}

Ime::Segmentation MaaMiScheme()
{
    return Ime::Segmentation{
        Ime::Scheme{
            Ime::Syllable(3220, 322020),
            Ime::Syllable(3228, 3228)
        }
    };
}

std::vector<std::vector<SplitEdge>> SplitEdges(
    const std::vector<VirtualInputKey>& keys,
    const std::unordered_map<int64_t, Ime::Syllable>& syllables,
    const std::unordered_set<int64_t>& prefixes)
{
    size_t inputLength = keys.size();
    std::vector<std::vector<SplitEdge>> edges(inputLength);
    for (size_t startIndex = 0; startIndex < inputLength; startIndex++)
    {
        int64_t code = 0;
        size_t endIndexLimit = (std::min)(inputLength, startIndex + maxSyllableKeyCount);
        for (size_t endIndex = startIndex; endIndex < endIndexLimit; endIndex++)
        {
            code = code * 100 + keys[endIndex].code;
            if (!prefixes.contains(code))
            {
                break;
            }

            auto found = syllables.find(code);
            if (found != syllables.end())
            {
                edges[startIndex].push_back({ found->second, endIndex + 1 });
            }
        }
    }
    return edges;
}

Ime::Scheme SchemeAt(size_t nodeIndex, const std::vector<SplitNode>& nodes)
{
    Ime::Scheme syllables;
    syllables.reserve(nodes[nodeIndex].count);

    std::optional<size_t> currentIndex = nodeIndex;
    while (currentIndex)
    {
        const SplitNode& node = nodes[*currentIndex];
        syllables.push_back(node.syllable);
        currentIndex = node.previousIndex;
    }

    std::reverse(syllables.begin(), syllables.end());
    return syllables;
}

bool NonLastOriginEndsWithA(const Ime::Scheme& scheme)
{
    if (scheme.size() <= 1)
    {
        return false;
    }

    for (size_t index = 0; index + 1 < scheme.size(); index++)
    {
        const std::vector<VirtualInputKey>& origin = scheme[index].origin;
        if (!origin.empty() && origin.back() == VirtualInputKey::letterA)
        {
            return true;
        }
    }
    return false;
}

int LongAEndingCount(const Ime::Scheme& scheme, int64_t Ime::Syllable::*codeMember)
{
    int count = 0;
    int thirdLastCode = 0;
    int secondLastCode = 0;
    int lastCode = 0;

    for (const Ime::Syllable& syllable : scheme)
    {
        int64_t syllableCode = syllable.*codeMember;
        int64_t divisor = 1;
        while ((syllableCode / divisor) >= 100)
        {
            divisor *= 100;
        }

        while (divisor > 0)
        {
            int keyCode = static_cast<int>(syllableCode / divisor);
            if (secondLastCode == VirtualInputKey::letterA.code &&
                lastCode == VirtualInputKey::letterA.code &&
                keyCode == VirtualInputKey::letterM.code)
            {
                count++;
            }
            else if (thirdLastCode == VirtualInputKey::letterA.code &&
                secondLastCode == VirtualInputKey::letterA.code &&
                lastCode == VirtualInputKey::letterN.code &&
                keyCode == VirtualInputKey::letterG.code)
            {
                count++;
            }

            syllableCode %= divisor;
            divisor /= 100;
            thirdLastCode = secondLastCode;
            secondLastCode = lastCode;
            lastCode = keyCode;
        }
    }
    return count;
}

bool IsValidScheme(const Ime::Scheme& scheme)
{
    if (scheme.size() <= 1)
    {
        return true;
    }

    if (!NonLastOriginEndsWithA(scheme))
    {
        return true;
    }

    int originCount = LongAEndingCount(scheme, &Ime::Syllable::originCode);
    if (originCount <= 0)
    {
        return true;
    }
    return originCount == LongAEndingCount(scheme, &Ime::Syllable::aliasCode);
}

} // namespace

namespace Ime {

bool Segmenter::Prepare()
{
    ImeDatabase database;
    if (!database.Open())
    {
        return false;
    }
    return Prepare(database);
}

bool Segmenter::Prepare(const ImeDatabase& database)
{
    std::vector<ImeDatabase::SyllableRow> rows = database.QuerySyllables();
    if (rows.empty())
    {
        Global::Log(L"Segmenter prepare failed: syllable dictionary is empty");
        _syllables.clear();
        _prefixes.clear();
        return false;
    }

    std::unordered_map<int64_t, Syllable> syllables;
    syllables.reserve(rows.size());
    for (const ImeDatabase::SyllableRow& row : rows)
    {
        syllables[row.aliasCode] = Syllable(row.aliasCode, row.originCode);
    }

    std::unordered_set<int64_t> prefixes;
    prefixes.reserve(syllables.size() * maxSyllableKeyCount);
    for (const auto& item : syllables)
    {
        int64_t code = 0;
        for (const VirtualInputKey& key : item.second.alias)
        {
            code = code * 100 + key.code;
            prefixes.insert(code);
        }
    }

    _syllables = std::move(syllables);
    _prefixes = std::move(prefixes);
    return true;
}

bool Segmenter::IsPrepared() const
{
    return !_syllables.empty();
}

Segmentation Segmenter::Segment(const std::vector<VirtualInputKey>& keys) const
{
    switch (keys.size())
    {
    case 0:
        return Segmentation();
    case 1:
        if (keys.front() == VirtualInputKey::letterA)
        {
            return SingleScheme(Syllable(20, 2020));
        }
        if (keys.front() == VirtualInputKey::letterO)
        {
            return SingleScheme(Syllable(34, 34));
        }
        if (keys.front() == VirtualInputKey::letterM)
        {
            return SingleScheme(Syllable(32, 32));
        }
        return Segmentation();
    case 4:
        switch (CombinedCode(keys))
        {
        case codeMaaMaa:
            return MaaMaaScheme();
        case codeMaaMi:
            return MaaMiScheme();
        default:
            break;
        }
    default:
        break;
    }

    std::vector<VirtualInputKey> syllableKeys = SyllableKeys(keys);
    size_t inputLength = syllableKeys.size();
    if (inputLength == 0 || !IsPrepared())
    {
        return Segmentation();
    }

    std::vector<std::vector<SplitEdge>> edges = SplitEdges(syllableKeys, _syllables, _prefixes);
    if (edges.empty() || edges.front().empty())
    {
        return Segmentation();
    }

    std::vector<SplitNode> nodes;
    std::vector<size_t> frontier;
    for (const SplitEdge& edge : edges.front())
    {
        nodes.push_back({ edge.syllable, std::nullopt, edge.endIndex, 1 });
        frontier.push_back(nodes.size() - 1);
    }

    while (!frontier.empty())
    {
        std::vector<size_t> nextFrontier;
        for (size_t nodeIndex : frontier)
        {
            size_t nodeLength = nodes[nodeIndex].length;
            size_t nodeCount = nodes[nodeIndex].count;
            if (nodeLength >= inputLength)
            {
                continue;
            }

            for (const SplitEdge& edge : edges[nodeLength])
            {
                nodes.push_back({ edge.syllable, nodeIndex, edge.endIndex, nodeCount + 1 });
                nextFrontier.push_back(nodes.size() - 1);
            }
        }
        frontier = std::move(nextFrontier);
    }

    struct Candidate
    {
        Scheme scheme;
        size_t length = 0;
        size_t count = 0;
    };

    std::vector<Candidate> candidates;
    candidates.reserve(nodes.size());
    for (size_t nodeIndex = 0; nodeIndex < nodes.size(); nodeIndex++)
    {
        Scheme scheme = SchemeAt(nodeIndex, nodes);
        if (IsValidScheme(scheme))
        {
            candidates.push_back({ std::move(scheme), nodes[nodeIndex].length, nodes[nodeIndex].count });
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const Candidate& left, const Candidate& right)
    {
        if (left.length == right.length)
        {
            return left.count < right.count;
        }
        return left.length > right.length;
    });

    Segmentation segmentation;
    segmentation.reserve(candidates.size());
    for (Candidate& candidate : candidates)
    {
        segmentation.push_back(std::move(candidate.scheme));
    }
    return segmentation;
}

std::optional<std::wstring> Segmenter::SyllableText(const std::vector<VirtualInputKey>& keys) const
{
    if (keys.size() > maxSyllableKeyCount)
    {
        return std::nullopt;
    }

    const Syllable* syllable = Lookup(CombinedCode(keys));
    if (syllable == nullptr)
    {
        return std::nullopt;
    }
    return syllable->OriginText();
}

const Syllable* Segmenter::Lookup(int64_t code) const
{
    auto found = _syllables.find(code);
    return (found == _syllables.end()) ? nullptr : &found->second;
}

} // namespace Ime
