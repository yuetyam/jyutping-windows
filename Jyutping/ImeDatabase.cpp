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

std::wstring DirectoryFromPath(const std::wstring& path)
{
    size_t index = path.find_last_of(L"\\/");
    if (index == std::wstring::npos)
    {
        return std::wstring();
    }
    return path.substr(0, index + 1);
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
    static constexpr WCHAR sql[] =
        L"SELECT name FROM sqlite_master "
        L"WHERE type = 'table' AND name IN ('core_lexicon', 'core_syllable_table');";

    Statement statement;
    if (!Prepare(sql, statement.Out()))
    {
        return false;
    }

    int tableCount = 0;
    int result = SQLITE_OK;
    while ((result = sqlite3_step(statement.Get())) == SQLITE_ROW)
    {
        tableCount++;
    }

    if (result != SQLITE_DONE)
    {
        LogError(L"verify schema", result);
        return false;
    }

    return tableCount == 2;
}

std::vector<ImeDatabase::LexiconRow> ImeDatabase::QueryLexiconByAnchors(int64_t anchors, int limit) const
{
    static constexpr WCHAR sql[] = L"SELECT rowid, word, romanization FROM core_lexicon WHERE anchors = ? LIMIT ?;";

    Statement statement;
    if (!Prepare(sql, statement.Out()))
    {
        return std::vector<LexiconRow>();
    }

    sqlite3_bind_int64(statement.Get(), 1, anchors);
    sqlite3_bind_int64(statement.Get(), 2, NormalizeLimit(limit));

    std::vector<LexiconRow> rows;
    int result = SQLITE_OK;
    while ((result = sqlite3_step(statement.Get())) == SQLITE_ROW)
    {
        rows.push_back({
            sqlite3_column_int64(statement.Get(), 0),
            ColumnText(statement.Get(), 1),
            ColumnText(statement.Get(), 2)
        });
    }

    if (result != SQLITE_DONE)
    {
        LogError(L"query lexicon by anchors", result);
    }
    return rows;
}

std::vector<ImeDatabase::LexiconRow> ImeDatabase::QueryLexiconBySpell(int64_t spell, int limit) const
{
    static constexpr WCHAR sql[] = L"SELECT rowid, word, romanization FROM core_lexicon WHERE spell = ? LIMIT ?;";

    Statement statement;
    if (!Prepare(sql, statement.Out()))
    {
        return std::vector<LexiconRow>();
    }

    sqlite3_bind_int64(statement.Get(), 1, spell);
    sqlite3_bind_int64(statement.Get(), 2, NormalizeLimit(limit));

    std::vector<LexiconRow> rows;
    int result = SQLITE_OK;
    while ((result = sqlite3_step(statement.Get())) == SQLITE_ROW)
    {
        rows.push_back({
            sqlite3_column_int64(statement.Get(), 0),
            ColumnText(statement.Get(), 1),
            ColumnText(statement.Get(), 2)
        });
    }

    if (result != SQLITE_DONE)
    {
        LogError(L"query lexicon by spell", result);
    }
    return rows;
}

std::vector<ImeDatabase::LexiconRow> ImeDatabase::QueryLexiconStrict(int64_t spell, int64_t anchors, int limit) const
{
    static constexpr WCHAR sql[] = L"SELECT rowid, word, romanization FROM core_lexicon WHERE spell = ? AND anchors = ? LIMIT ?;";

    Statement statement;
    if (!Prepare(sql, statement.Out()))
    {
        return std::vector<LexiconRow>();
    }

    sqlite3_bind_int64(statement.Get(), 1, spell);
    sqlite3_bind_int64(statement.Get(), 2, anchors);
    sqlite3_bind_int64(statement.Get(), 3, NormalizeLimit(limit));

    std::vector<LexiconRow> rows;
    int result = SQLITE_OK;
    while ((result = sqlite3_step(statement.Get())) == SQLITE_ROW)
    {
        rows.push_back({
            sqlite3_column_int64(statement.Get(), 0),
            ColumnText(statement.Get(), 1),
            ColumnText(statement.Get(), 2)
        });
    }

    if (result != SQLITE_DONE)
    {
        LogError(L"query strict lexicon", result);
    }
    return rows;
}

std::vector<ImeDatabase::SyllableRow> ImeDatabase::QuerySyllables() const
{
    static constexpr WCHAR sql[] =
        L"SELECT alias_code, origin_code, nine_key_alias_code, nine_key_origin_code, alias, origin "
        L"FROM core_syllable_table;";

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
            sqlite3_column_int64(statement.Get(), 2),
            sqlite3_column_int64(statement.Get(), 3),
            ColumnText(statement.Get(), 4),
            ColumnText(statement.Get(), 5)
        });
    }

    if (result != SQLITE_DONE)
    {
        LogError(L"query syllables", result);
    }
    return rows;
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
