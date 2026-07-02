#include "PinyinSegmenter.h"
#include "Globals.h"
#include "Logger.h"

#include <algorithm>

namespace {

constexpr size_t maxSyllableKeyCount = 6;

struct SplitEdge
{
    Ime::PinyinSyllable syllable;
    size_t endIndex = 0;
};

struct SplitNode
{
    Ime::PinyinSyllable syllable;
    std::optional<size_t> previousIndex;
    size_t length = 0;
    size_t count = 0;
};

std::vector<VirtualInputKey> LetterKeys(const std::vector<VirtualInputKey>& keys)
{
    std::vector<VirtualInputKey> result;
    result.reserve(keys.size());
    for (const VirtualInputKey& key : keys)
    {
        if (key.IsLetter())
        {
            result.push_back(key);
        }
    }
    return result;
}

std::vector<std::vector<SplitEdge>> SplitEdges(
    const std::vector<VirtualInputKey>& keys,
    const std::unordered_map<int64_t, Ime::PinyinSyllable>& syllables)
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
            auto found = syllables.find(code);
            if (found != syllables.end())
            {
                edges[startIndex].push_back({ found->second, endIndex + 1 });
            }
        }
    }
    return edges;
}

Ime::PinyinScheme SchemeAt(size_t nodeIndex, const std::vector<SplitNode>& nodes)
{
    Ime::PinyinScheme syllables;
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

} // namespace

namespace Ime {

PinyinSyllable::PinyinSyllable(int64_t inputCode, std::wstring inputText) :
    code(inputCode),
    keys(InputKeysFromCode(inputCode)),
    text(std::move(inputText))
{
}

bool operator==(const PinyinSyllable& left, const PinyinSyllable& right)
{
    return left.code == right.code;
}

bool operator!=(const PinyinSyllable& left, const PinyinSyllable& right)
{
    return !(left == right);
}

size_t PinyinSchemeLength(const PinyinScheme& scheme)
{
    size_t result = 0;
    for (const PinyinSyllable& syllable : scheme)
    {
        result += syllable.keys.size();
    }
    return result;
}

std::wstring PinyinSchemeText(const PinyinScheme& scheme)
{
    std::wstring result;
    for (const PinyinSyllable& syllable : scheme)
    {
        result.append(syllable.text);
    }
    return result;
}

bool PinyinSegmenter::Prepare()
{
    ImeDatabase database;
    if (!database.Open())
    {
        return false;
    }
    return Prepare(database);
}

bool PinyinSegmenter::Prepare(const ImeDatabase& database)
{
    std::vector<ImeDatabase::PinyinSyllableRow> rows = database.QueryPinyinSyllables();
    if (rows.empty())
    {
        Global::Log(L"PinyinSegmenter prepare failed: syllable dictionary is empty");
        _syllables.clear();
        return false;
    }

    std::unordered_map<int64_t, PinyinSyllable> syllables;
    syllables.reserve(rows.size());
    for (const ImeDatabase::PinyinSyllableRow& row : rows)
    {
        syllables[row.code] = PinyinSyllable(row.code, row.syllable);
    }

    _syllables = std::move(syllables);
    return true;
}

bool PinyinSegmenter::IsPrepared() const
{
    return !_syllables.empty();
}

PinyinSegmentation PinyinSegmenter::Segment(const std::vector<VirtualInputKey>& keys) const
{
    std::vector<VirtualInputKey> letterKeys = LetterKeys(keys);
    size_t inputLength = letterKeys.size();
    if (inputLength == 0 || !IsPrepared())
    {
        return PinyinSegmentation();
    }

    std::vector<std::vector<SplitEdge>> edges = SplitEdges(letterKeys, _syllables);
    if (edges.empty() || edges.front().empty())
    {
        return PinyinSegmentation();
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
        PinyinScheme scheme;
        size_t length = 0;
        size_t count = 0;
    };

    std::vector<Candidate> candidates;
    candidates.reserve(nodes.size());
    for (size_t nodeIndex = 0; nodeIndex < nodes.size(); nodeIndex++)
    {
        candidates.push_back({ SchemeAt(nodeIndex, nodes), nodes[nodeIndex].length, nodes[nodeIndex].count });
    }

    std::sort(candidates.begin(), candidates.end(), [](const Candidate& left, const Candidate& right)
    {
        if (left.length == right.length)
        {
            return left.count < right.count;
        }
        return left.length > right.length;
    });

    PinyinSegmentation segmentation;
    segmentation.reserve(candidates.size());
    for (Candidate& candidate : candidates)
    {
        segmentation.push_back(std::move(candidate.scheme));
    }
    return segmentation;
}

const PinyinSyllable* PinyinSegmenter::Lookup(int64_t code) const
{
    auto found = _syllables.find(code);
    return (found == _syllables.end()) ? nullptr : &found->second;
}

} // namespace Ime
