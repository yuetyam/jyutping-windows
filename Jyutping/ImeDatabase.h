#pragma once

#include "stdafx.h"
#include "sal.h"

#include <cstdint>
#include <string>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

class ImeDatabase
{
public:
    struct LexiconRow
    {
        int64_t rowId = 0;
        std::wstring word;
        std::wstring romanization;
    };

    struct SyllableRow
    {
        int64_t aliasCode = 0;
        int64_t originCode = 0;
        int64_t nineKeyAliasCode = 0;
        int64_t nineKeyOriginCode = 0;
        std::wstring alias;
        std::wstring origin;
    };

    ImeDatabase();
    ~ImeDatabase();

    ImeDatabase(const ImeDatabase&) = delete;
    ImeDatabase& operator=(const ImeDatabase&) = delete;

    bool Open();
    bool Open(_In_z_ PCWSTR databasePath);
    void Close();

    bool IsOpen() const;
    const std::wstring& Path() const;
    bool VerifySchema() const;

    std::vector<LexiconRow> QueryLexiconByAnchors(int64_t anchors, int limit = 100) const;
    std::vector<LexiconRow> QueryLexiconBySpell(int64_t spell, int limit = -1) const;
    std::vector<LexiconRow> QueryLexiconStrict(int64_t spell, int64_t anchors, int limit = -1) const;
    std::vector<SyllableRow> QuerySyllables() const;

    static std::wstring DefaultDatabasePath();

private:
    bool Prepare(_In_z_ PCWSTR sql, _Outptr_result_maybenull_ sqlite3_stmt** statement) const;
    void LogError(_In_z_ PCWSTR operation, int result) const;

    sqlite3* _database;
    std::wstring _path;
};
