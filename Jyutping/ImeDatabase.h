#pragma once

#include "stdafx.h"
#include "sal.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

enum class CharacterStandard : int;

enum class CangjieVariant
{
    Cangjie5,
    Cangjie3,
    Quick5,
    Quick3
};

class ImeDatabase
{
public:
    class VariantLookup
    {
    public:
        VariantLookup();
        ~VariantLookup();

        VariantLookup(const VariantLookup&) = delete;
        VariantLookup& operator=(const VariantLookup&) = delete;
        VariantLookup(VariantLookup&& other) noexcept;
        VariantLookup& operator=(VariantLookup&& other) noexcept;

        bool IsValid() const;
        std::optional<uint32_t> Query(uint32_t source) const;

    private:
        friend class ImeDatabase;

        VariantLookup(const ImeDatabase* database, sqlite3_stmt* statement);

        const ImeDatabase* _database;
        sqlite3_stmt* _statement;
    };

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
        std::wstring alias;
        std::wstring origin;
    };

    struct PinyinLexiconRow
    {
        int64_t rowId = 0;
        std::wstring word;
        std::wstring romanization;
    };

    struct ShapeRow
    {
        int64_t rowId = 0;
        std::wstring word;
        int complex = 0;
    };

    struct StructureRow
    {
        std::wstring word;
        std::wstring romanization;
    };

    struct SymbolRow
    {
        int category = 0;
        int unicodeVersion = 0;
        std::wstring codePoint;
        std::wstring cantonese;
        std::wstring romanization;
    };

    struct PinyinSyllableRow
    {
        int64_t code = 0;
        std::wstring syllable;
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
    std::vector<PinyinLexiconRow> QueryPinyinBySpell(int64_t spell, int limit = -1) const;
    std::vector<PinyinLexiconRow> QueryPinyinByAnchors(int64_t anchors, int limit = 100) const;
    std::vector<PinyinSyllableRow> QueryPinyinSyllables() const;
    std::vector<ShapeRow> QueryCangjieByExactCode(CangjieVariant variant, int64_t code) const;
    std::vector<ShapeRow> QueryCangjieByPrefix(CangjieVariant variant, const std::wstring& prefix, int limit = 100) const;
    std::vector<ShapeRow> QueryQuickByExactCode(CangjieVariant variant, int64_t code) const;
    std::vector<ShapeRow> QueryQuickByPrefix(CangjieVariant variant, const std::wstring& prefix, int limit = 100) const;
    std::vector<ShapeRow> QueryStrokeByCode(int64_t code) const;
    std::vector<ShapeRow> QueryStrokeBySpell(int64_t spell) const;
    std::vector<ShapeRow> QueryStrokeByPattern(const std::wstring& pattern, bool isLike, int limit = 100) const;
    std::vector<StructureRow> QueryStructureBySpell(int64_t spell) const;
    std::vector<std::wstring> QueryTextMarksBySpell(int64_t spell) const;
    std::vector<SymbolRow> QuerySymbolsBySpell(int64_t spell) const;
    std::optional<std::wstring> QueryEmojiSkinTarget(const std::wstring& source) const;
    std::vector<std::wstring> LookupRomanizationsForWord(const std::wstring& word) const;
    VariantLookup CreateVariantLookup(CharacterStandard standard) const;
    std::optional<uint32_t> QueryVariantTarget(CharacterStandard standard, uint32_t source) const;

    static std::wstring DefaultDatabasePath();

    void LogError(_In_z_ PCWSTR operation, int result) const;

private:
    bool Prepare(_In_z_ PCWSTR sql, _Outptr_result_maybenull_ sqlite3_stmt** statement) const;

    sqlite3* _database;
    std::wstring _path;
};
