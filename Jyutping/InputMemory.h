#pragma once

#include "ImeTypes.h"
#include "Segmenter.h"
#include "VirtualInputKey.h"
#include "sal.h"

#include <vector>

struct sqlite3;
struct sqlite3_stmt;

namespace Ime {

class InputMemory
{
public:
    InputMemory();
    ~InputMemory();

    InputMemory(const InputMemory&) = delete;
    InputMemory& operator=(const InputMemory&) = delete;

    bool Prepare();
    bool IsPrepared() const;
    void Close();

    bool Handle(const Lexicon& lexicon);
    bool Forget(const Lexicon& lexicon);
    bool DeleteAll();

    std::vector<Lexicon> Suggest(
        const std::vector<VirtualInputKey>& keys,
        const Segmentation& segmentation,
        const Segmenter& segmenter) const;

private:
    bool Execute(_In_z_ PCWSTR sql) const;
    bool PrepareStatement(_In_z_ PCWSTR sql, _Outptr_result_maybenull_ sqlite3_stmt** statement) const;
    void LogError(_In_z_ PCWSTR operation, int result) const;

    sqlite3* _database;
    bool _isPrepared;
};

} // namespace Ime
