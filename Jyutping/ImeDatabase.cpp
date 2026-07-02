#include "Private.h"
#include "ImeDatabase.h"
#include "Define.h"
#include "Globals.h"
#include "Logger.h"

#include <winsqlite/winsqlite3.h>

#include <string>

namespace {

class Statement
{
public:
    Statement() : _statement(nullptr)
    {
    }

    ~Statement()
    {
        sqlite3_finalize(_statement);
    }

    sqlite3_stmt** Out()
    {
        return &_statement;
    }

    sqlite3_stmt* Get() const
    {
        return _statement;
    }

private:
    sqlite3_stmt* _statement;
};

std::string WideToUtf8(_In_z_ PCWSTR text)
{
    if (text == nullptr || *text == L'\0')
    {
        return std::string();
    }

    int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1)
    {
        return std::string();
    }

    std::string result(static_cast<size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring ColumnText(sqlite3_stmt* statement, int column)
{
    const void* text = sqlite3_column_text16(statement, column);
    if (text == nullptr)
    {
        return std::wstring();
    }

    int byteCount = sqlite3_column_bytes16(statement, column);
    return std::wstring(static_cast<const wchar_t*>(text), static_cast<size_t>(byteCount) / sizeof(wchar_t));
}

int64_t NormalizeLimit(int limit)
{
    return static_cast<int64_t>(limit <= 0 ? -1 : limit);
}

void BindText(sqlite3_stmt* statement, int index, const std::wstring& text)
{
    sqlite3_bind_text16(statement, index, text.c_str(), -1, SQLITE_TRANSIENT);
}

std::wstring DirectoryFromPath(const std::wstring& path)
{
    size_t index = path.find_last_of(L"\\/");
    if (index == std::wstring::npos)
    {
        return std::wstring();
    }
    return path.substr(0, index + 1);
}

std::vector<ImeDatabase::LexiconRow> ReadLexiconRows(sqlite3_stmt* statement, const ImeDatabase* database, _In_z_ PCWSTR operation)
{
    std::vector<ImeDatabase::LexiconRow> rows;
    int result = SQLITE_OK;
    while ((result = sqlite3_step(statement)) == SQLITE_ROW)
    {
        rows.push_back({
            sqlite3_column_int64(statement, 0),
            ColumnText(statement, 1),
            ColumnText(statement, 2)
        });
    }

    if (result != SQLITE_DONE)
    {
        database->LogError(operation, result);
    }
    return rows;
}

std::vector<ImeDatabase::PinyinLexiconRow> ReadPinyinRows(sqlite3_stmt* statement, const ImeDatabase* database, _In_z_ PCWSTR operation)
{
    std::vector<ImeDatabase::PinyinLexiconRow> rows;
    int result = SQLITE_OK;
    while ((result = sqlite3_step(statement)) == SQLITE_ROW)
    {
        rows.push_back({
            sqlite3_column_int64(statement, 0),
            ColumnText(statement, 1),
            ColumnText(statement, 2)
        });
    }

    if (result != SQLITE_DONE)
    {
        database->LogError(operation, result);
    }
    return rows;
}

std::vector<ImeDatabase::ShapeRow> ReadShapeRows(sqlite3_stmt* statement, const ImeDatabase* database, _In_z_ PCWSTR operation)
{
    std::vector<ImeDatabase::ShapeRow> rows;
    int result = SQLITE_OK;
    while ((result = sqlite3_step(statement)) == SQLITE_ROW)
    {
        rows.push_back({
            sqlite3_column_int64(statement, 0),
            ColumnText(statement, 1),
            sqlite3_column_int(statement, 2)
        });
    }

    if (result != SQLITE_DONE)
    {
        database->LogError(operation, result);
    }
    return rows;
}

bool IsCangjieVariant(CangjieVariant variant)
{
    return variant == CangjieVariant::Cangjie5 || variant == CangjieVariant::Cangjie3;
}

bool IsQuickVariant(CangjieVariant variant)
{
    return variant == CangjieVariant::Quick5 || variant == CangjieVariant::Quick3;
}

struct ShapeColumns
{
    PCWSTR exactSql;
    PCWSTR prefixSql;
};

ShapeColumns CangjieColumns(CangjieVariant variant)
{
    static constexpr WCHAR cangjie5Exact[] =
        L"SELECT rowid, word, c5complex FROM cangjie_table WHERE c5code = ? ORDER BY rowid;";
    static constexpr WCHAR cangjie5Prefix[] =
        L"SELECT rowid, word, c5complex FROM cangjie_table WHERE cangjie5 GLOB ? ORDER BY c5complex ASC, rowid ASC LIMIT ?;";
    static constexpr WCHAR cangjie3Exact[] =
        L"SELECT rowid, word, c3complex FROM cangjie_table WHERE c3code = ? ORDER BY rowid;";
    static constexpr WCHAR cangjie3Prefix[] =
        L"SELECT rowid, word, c3complex FROM cangjie_table WHERE cangjie3 GLOB ? ORDER BY c3complex ASC, rowid ASC LIMIT ?;";

    return (variant == CangjieVariant::Cangjie3) ?
        ShapeColumns{ cangjie3Exact, cangjie3Prefix } :
        ShapeColumns{ cangjie5Exact, cangjie5Prefix };
}

ShapeColumns QuickColumns(CangjieVariant variant)
{
    static constexpr WCHAR quick5Exact[] =
        L"SELECT rowid, word, q5complex FROM quick_table WHERE q5code = ? ORDER BY rowid;";
    static constexpr WCHAR quick5Prefix[] =
        L"SELECT rowid, word, q5complex FROM quick_table WHERE quick5 GLOB ? ORDER BY q5complex ASC, rowid ASC LIMIT ?;";
    static constexpr WCHAR quick3Exact[] =
        L"SELECT rowid, word, q3complex FROM quick_table WHERE q3code = ? ORDER BY rowid;";
    static constexpr WCHAR quick3Prefix[] =
        L"SELECT rowid, word, q3complex FROM quick_table WHERE quick3 GLOB ? ORDER BY q3complex ASC, rowid ASC LIMIT ?;";

    return (variant == CangjieVariant::Quick3) ?
        ShapeColumns{ quick3Exact, quick3Prefix } :
        ShapeColumns{ quick5Exact, quick5Prefix };
}

} // namespace

ImeDatabase::ImeDatabase() : _database(nullptr)
{
}

ImeDatabase::~ImeDatabase()
{
    Close();
}

bool ImeDatabase::Open()
{
    return Open(DefaultDatabasePath().c_str());
}

bool ImeDatabase::Open(_In_z_ PCWSTR databasePath)
{
    Close();

    if (databasePath == nullptr || *databasePath == L'\0')
    {
        Global::Log(L"ImeDatabase open failed: database path is empty");
        return false;
    }

    std::string path = WideToUtf8(databasePath);
    if (path.empty())
    {
        Global::Log(L"ImeDatabase open failed: unable to convert path to UTF-8: %s", databasePath);
        return false;
    }

    int flags = SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX;
    int result = sqlite3_open_v2(path.c_str(), &_database, flags, nullptr);
    if (result != SQLITE_OK)
    {
        LogError(L"open", result);
        Close();
        return false;
    }

    _path = databasePath;
    return true;
}

void ImeDatabase::Close()
{
    if (_database != nullptr)
    {
        sqlite3_close_v2(_database);
        _database = nullptr;
    }
    _path.clear();
}

bool ImeDatabase::IsOpen() const
{
    return _database != nullptr;
}

const std::wstring& ImeDatabase::Path() const
{
    return _path;
}

bool ImeDatabase::VerifySchema() const
{
    static constexpr PCWSTR statements[] =
    {
        L"SELECT rowid, word, romanization FROM core_lexicon WHERE spell = ? AND anchors = ? LIMIT 0;",
        L"SELECT rowid, word, romanization FROM core_lexicon WHERE word = ? ORDER BY rowid LIMIT 0;",
        L"SELECT alias_code, origin_code, alias, origin FROM core_syllable_table LIMIT 0;",
        L"SELECT rowid, word, romanization FROM pinyin_lexicon WHERE spell = ? LIMIT 0;",
        L"SELECT rowid, word, romanization FROM pinyin_lexicon WHERE anchors = ? LIMIT 0;",
        L"SELECT code, syllable FROM pinyin_syllable_table LIMIT 0;",
        L"SELECT rowid, word, c5code, cangjie5, c5complex FROM cangjie_table WHERE c5code = ? ORDER BY rowid LIMIT 0;",
        L"SELECT rowid, word, c5complex FROM cangjie_table WHERE cangjie5 GLOB ? ORDER BY c5complex ASC, rowid ASC LIMIT 0;",
        L"SELECT rowid, word, c3code, cangjie3, c3complex FROM cangjie_table WHERE c3code = ? ORDER BY rowid LIMIT 0;",
        L"SELECT rowid, word, c3complex FROM cangjie_table WHERE cangjie3 GLOB ? ORDER BY c3complex ASC, rowid ASC LIMIT 0;",
        L"SELECT rowid, word, q5code, quick5, q5complex FROM quick_table WHERE q5code = ? ORDER BY rowid LIMIT 0;",
        L"SELECT rowid, word, q5complex FROM quick_table WHERE quick5 GLOB ? ORDER BY q5complex ASC, rowid ASC LIMIT 0;",
        L"SELECT rowid, word, q3code, quick3, q3complex FROM quick_table WHERE q3code = ? ORDER BY rowid LIMIT 0;",
        L"SELECT rowid, word, q3complex FROM quick_table WHERE quick3 GLOB ? ORDER BY q3complex ASC, rowid ASC LIMIT 0;",
        L"SELECT rowid, word, code, spell, stroke, complex FROM stroke_table WHERE code = ? ORDER BY rowid LIMIT 0;",
        L"SELECT rowid, word, complex FROM stroke_table WHERE spell = ? ORDER BY rowid LIMIT 0;",
        L"SELECT rowid, word, complex FROM stroke_table WHERE stroke GLOB ? ORDER BY complex ASC, rowid ASC LIMIT 0;",
        L"SELECT word, romanization FROM structure_table WHERE spell = ? LIMIT 0;"
    };

    for (PCWSTR sql : statements)
    {
        Statement statement;
        if (!Prepare(sql, statement.Out()))
        {
            return false;
        }
    }
    return true;
}

std::vector<ImeDatabase::LexiconRow> ImeDatabase::QueryLexiconByAnchors(int64_t anchors, int limit) const
{
    static constexpr WCHAR sql[] = L"SELECT rowid, word, romanization FROM core_lexicon WHERE anchors = ? ORDER BY rowid LIMIT ?;";

    Statement statement;
    if (!Prepare(sql, statement.Out()))
    {
        return std::vector<LexiconRow>();
    }

    sqlite3_bind_int64(statement.Get(), 1, anchors);
    sqlite3_bind_int64(statement.Get(), 2, NormalizeLimit(limit));

    return ReadLexiconRows(statement.Get(), this, L"query lexicon by anchors");
}

std::vector<ImeDatabase::LexiconRow> ImeDatabase::QueryLexiconBySpell(int64_t spell, int limit) const
{
    static constexpr WCHAR sql[] = L"SELECT rowid, word, romanization FROM core_lexicon WHERE spell = ? ORDER BY rowid LIMIT ?;";

    Statement statement;
    if (!Prepare(sql, statement.Out()))
    {
        return std::vector<LexiconRow>();
    }

    sqlite3_bind_int64(statement.Get(), 1, spell);
    sqlite3_bind_int64(statement.Get(), 2, NormalizeLimit(limit));

    return ReadLexiconRows(statement.Get(), this, L"query lexicon by spell");
}

std::vector<ImeDatabase::LexiconRow> ImeDatabase::QueryLexiconStrict(int64_t spell, int64_t anchors, int limit) const
{
    static constexpr WCHAR sql[] = L"SELECT rowid, word, romanization FROM core_lexicon WHERE spell = ? AND anchors = ? ORDER BY rowid LIMIT ?;";

    Statement statement;
    if (!Prepare(sql, statement.Out()))
    {
        return std::vector<LexiconRow>();
    }

    sqlite3_bind_int64(statement.Get(), 1, spell);
    sqlite3_bind_int64(statement.Get(), 2, anchors);
    sqlite3_bind_int64(statement.Get(), 3, NormalizeLimit(limit));

    return ReadLexiconRows(statement.Get(), this, L"query strict lexicon");
}

std::vector<ImeDatabase::SyllableRow> ImeDatabase::QuerySyllables() const
{
    static constexpr WCHAR sql[] = L"SELECT alias_code, origin_code, alias, origin FROM core_syllable_table;";

    Statement statement;
    if (!Prepare(sql, statement.Out()))
    {
        return std::vector<SyllableRow>();
    }

    std::vector<SyllableRow> rows;
    int result = SQLITE_OK;
    while ((result = sqlite3_step(statement.Get())) == SQLITE_ROW)
    {
        rows.push_back({
            sqlite3_column_int64(statement.Get(), 0),
            sqlite3_column_int64(statement.Get(), 1),
            ColumnText(statement.Get(), 2),
            ColumnText(statement.Get(), 3)
        });
    }

    if (result != SQLITE_DONE)
    {
        LogError(L"query syllables", result);
    }
    return rows;
}

std::vector<ImeDatabase::PinyinLexiconRow> ImeDatabase::QueryPinyinBySpell(int64_t spell, int limit) const
{
    static constexpr WCHAR sql[] = L"SELECT rowid, word, romanization FROM pinyin_lexicon WHERE spell = ? ORDER BY rowid LIMIT ?;";

    Statement statement;
    if (!Prepare(sql, statement.Out()))
    {
        return std::vector<PinyinLexiconRow>();
    }

    sqlite3_bind_int64(statement.Get(), 1, spell);
    sqlite3_bind_int64(statement.Get(), 2, NormalizeLimit(limit));
    return ReadPinyinRows(statement.Get(), this, L"query pinyin by spell");
}

std::vector<ImeDatabase::PinyinLexiconRow> ImeDatabase::QueryPinyinByAnchors(int64_t anchors, int limit) const
{
    static constexpr WCHAR sql[] = L"SELECT rowid, word, romanization FROM pinyin_lexicon WHERE anchors = ? ORDER BY rowid LIMIT ?;";

    Statement statement;
    if (!Prepare(sql, statement.Out()))
    {
        return std::vector<PinyinLexiconRow>();
    }

    sqlite3_bind_int64(statement.Get(), 1, anchors);
    sqlite3_bind_int64(statement.Get(), 2, NormalizeLimit(limit));
    return ReadPinyinRows(statement.Get(), this, L"query pinyin by anchors");
}

std::vector<ImeDatabase::PinyinSyllableRow> ImeDatabase::QueryPinyinSyllables() const
{
    static constexpr WCHAR sql[] = L"SELECT code, syllable FROM pinyin_syllable_table ORDER BY code;";

    Statement statement;
    if (!Prepare(sql, statement.Out()))
    {
        return std::vector<PinyinSyllableRow>();
    }

    std::vector<PinyinSyllableRow> rows;
    int result = SQLITE_OK;
    while ((result = sqlite3_step(statement.Get())) == SQLITE_ROW)
    {
        rows.push_back({
            sqlite3_column_int64(statement.Get(), 0),
            ColumnText(statement.Get(), 1)
        });
    }

    if (result != SQLITE_DONE)
    {
        LogError(L"query pinyin syllables", result);
    }
    return rows;
}

std::vector<ImeDatabase::ShapeRow> ImeDatabase::QueryCangjieByExactCode(CangjieVariant variant, int64_t code) const
{
    if (!IsCangjieVariant(variant))
    {
        return std::vector<ShapeRow>();
    }

    Statement statement;
    if (!Prepare(CangjieColumns(variant).exactSql, statement.Out()))
    {
        return std::vector<ShapeRow>();
    }

    sqlite3_bind_int64(statement.Get(), 1, code);
    return ReadShapeRows(statement.Get(), this, L"query cangjie by exact code");
}

std::vector<ImeDatabase::ShapeRow> ImeDatabase::QueryCangjieByPrefix(CangjieVariant variant, const std::wstring& prefix, int limit) const
{
    if (!IsCangjieVariant(variant))
    {
        return std::vector<ShapeRow>();
    }

    Statement statement;
    if (!Prepare(CangjieColumns(variant).prefixSql, statement.Out()))
    {
        return std::vector<ShapeRow>();
    }

    BindText(statement.Get(), 1, prefix + L"*");
    sqlite3_bind_int64(statement.Get(), 2, NormalizeLimit(limit));
    return ReadShapeRows(statement.Get(), this, L"query cangjie by prefix");
}

std::vector<ImeDatabase::ShapeRow> ImeDatabase::QueryQuickByExactCode(CangjieVariant variant, int64_t code) const
{
    if (!IsQuickVariant(variant))
    {
        return std::vector<ShapeRow>();
    }

    Statement statement;
    if (!Prepare(QuickColumns(variant).exactSql, statement.Out()))
    {
        return std::vector<ShapeRow>();
    }

    sqlite3_bind_int64(statement.Get(), 1, code);
    return ReadShapeRows(statement.Get(), this, L"query quick by exact code");
}

std::vector<ImeDatabase::ShapeRow> ImeDatabase::QueryQuickByPrefix(CangjieVariant variant, const std::wstring& prefix, int limit) const
{
    if (!IsQuickVariant(variant))
    {
        return std::vector<ShapeRow>();
    }

    Statement statement;
    if (!Prepare(QuickColumns(variant).prefixSql, statement.Out()))
    {
        return std::vector<ShapeRow>();
    }

    BindText(statement.Get(), 1, prefix + L"*");
    sqlite3_bind_int64(statement.Get(), 2, NormalizeLimit(limit));
    return ReadShapeRows(statement.Get(), this, L"query quick by prefix");
}

std::vector<ImeDatabase::ShapeRow> ImeDatabase::QueryStrokeByCode(int64_t code) const
{
    static constexpr WCHAR sql[] = L"SELECT rowid, word, complex FROM stroke_table WHERE code = ? ORDER BY rowid;";

    Statement statement;
    if (!Prepare(sql, statement.Out()))
    {
        return std::vector<ShapeRow>();
    }

    sqlite3_bind_int64(statement.Get(), 1, code);
    return ReadShapeRows(statement.Get(), this, L"query stroke by code");
}

std::vector<ImeDatabase::ShapeRow> ImeDatabase::QueryStrokeBySpell(int64_t spell) const
{
    static constexpr WCHAR sql[] = L"SELECT rowid, word, complex FROM stroke_table WHERE spell = ? ORDER BY rowid;";

    Statement statement;
    if (!Prepare(sql, statement.Out()))
    {
        return std::vector<ShapeRow>();
    }

    sqlite3_bind_int64(statement.Get(), 1, spell);
    return ReadShapeRows(statement.Get(), this, L"query stroke by spell");
}

std::vector<ImeDatabase::ShapeRow> ImeDatabase::QueryStrokeByPattern(const std::wstring& pattern, bool isLike, int limit) const
{
    static constexpr WCHAR likeSql[] =
        L"SELECT rowid, word, complex FROM stroke_table WHERE stroke LIKE ? ORDER BY complex ASC, rowid ASC LIMIT ?;";
    static constexpr WCHAR globSql[] =
        L"SELECT rowid, word, complex FROM stroke_table WHERE stroke GLOB ? ORDER BY complex ASC, rowid ASC LIMIT ?;";

    Statement statement;
    if (!Prepare(isLike ? likeSql : globSql, statement.Out()))
    {
        return std::vector<ShapeRow>();
    }

    BindText(statement.Get(), 1, pattern);
    sqlite3_bind_int64(statement.Get(), 2, NormalizeLimit(limit));
    return ReadShapeRows(statement.Get(), this, L"query stroke by pattern");
}

std::vector<ImeDatabase::StructureRow> ImeDatabase::QueryStructureBySpell(int64_t spell) const
{
    static constexpr WCHAR sql[] = L"SELECT word, romanization FROM structure_table WHERE spell = ?;";

    Statement statement;
    if (!Prepare(sql, statement.Out()))
    {
        return std::vector<StructureRow>();
    }

    sqlite3_bind_int64(statement.Get(), 1, spell);

    std::vector<StructureRow> rows;
    int result = SQLITE_OK;
    while ((result = sqlite3_step(statement.Get())) == SQLITE_ROW)
    {
        rows.push_back({
            ColumnText(statement.Get(), 0),
            ColumnText(statement.Get(), 1)
        });
    }

    if (result != SQLITE_DONE)
    {
        LogError(L"query structure by spell", result);
    }
    return rows;
}

std::vector<std::wstring> ImeDatabase::LookupRomanizationsForWord(const std::wstring& word) const
{
    static constexpr WCHAR sql[] = L"SELECT romanization FROM core_lexicon WHERE word = ? ORDER BY rowid;";

    Statement statement;
    if (!Prepare(sql, statement.Out()))
    {
        return std::vector<std::wstring>();
    }

    BindText(statement.Get(), 1, word);

    std::vector<std::wstring> romanizations;
    int result = SQLITE_OK;
    while ((result = sqlite3_step(statement.Get())) == SQLITE_ROW)
    {
        romanizations.push_back(ColumnText(statement.Get(), 0));
    }

    if (result != SQLITE_DONE)
    {
        LogError(L"lookup romanizations for word", result);
    }
    return romanizations;
}

std::wstring ImeDatabase::DefaultDatabasePath()
{
    WCHAR modulePath[MAX_PATH] = { L'\0' };
    DWORD length = GetModuleFileName(Global::dllInstanceHandle, modulePath, ARRAYSIZE(modulePath));
    if (length == 0 || length >= ARRAYSIZE(modulePath))
    {
        Global::Log(L"ImeDatabase failed to resolve module path");
        return TEXTSERVICE_SQLITE_DIC;
    }

    return DirectoryFromPath(std::wstring(modulePath, length)) + TEXTSERVICE_SQLITE_DIC;
}

bool ImeDatabase::Prepare(_In_z_ PCWSTR sql, _Outptr_result_maybenull_ sqlite3_stmt** statement) const
{
    if (statement == nullptr)
    {
        return false;
    }
    *statement = nullptr;

    if (_database == nullptr)
    {
        Global::Log(L"ImeDatabase prepare failed: database is not open");
        return false;
    }

    int result = sqlite3_prepare16_v2(_database, sql, -1, statement, nullptr);
    if (result != SQLITE_OK)
    {
        LogError(L"prepare statement", result);
        return false;
    }
    return true;
}

void ImeDatabase::LogError(_In_z_ PCWSTR operation, int result) const
{
    const WCHAR* message = L"";
    if (_database != nullptr)
    {
        message = static_cast<const WCHAR*>(sqlite3_errmsg16(_database));
    }
    Global::Log(L"ImeDatabase %s failed (%d): %s", operation, result, message ? message : L"");
}
