#pragma once
/**
 * @file SqlEngine.hpp
 * @brief Native SQLite / MySQL / PostgreSQL query runner, schema browser, and CSV/JSON exporter (#42).
 */

#include <string>
#include <vector>

namespace phide::db {

/**
 * @enum SqlDialect
 * @brief Supported SQL database engines for schema inspection and query execution.
 */
enum class SqlDialect {
    SQLite = 0,
    MySQL = 1,
    PostgreSQL = 2,
    SQLServer = 3
};

/**
 * @struct SqlConnectionProfile
 * @brief Connection settings and discovered database list for a local/remote SQL target.
 */
struct SqlConnectionProfile {
    std::wstring name;
    SqlDialect dialect = SqlDialect::SQLite;
    std::wstring sqlitePath;
    std::wstring host = L"127.0.0.1";
    int port = 3306;
    std::wstring user = L"root";
    std::wstring password;
    std::wstring database;
    std::vector<std::wstring> availableDatabases;
    bool isConnected = false;
};

/**
 * @struct SqlColumnInfo
 * @brief Column metadata extracted from live DB catalogs or CREATE TABLE DDL.
 */
struct SqlColumnInfo {
    std::wstring name;
    std::wstring type;
    bool notNull = false;
    bool isPrimaryKey = false;
    bool isUnique = false; ///< True when covered by a single-column UNIQUE constraint/index (used for 1:1 ERD detection)
};

/**
 * @struct SqlForeignKeyInfo
 * @brief Explicit foreign key reference from `column` to `refTable(refColumn)`.
 */
struct SqlForeignKeyInfo {
    std::wstring column;
    std::wstring refTable;
    std::wstring refColumn;
    std::wstring refSchema; ///< Optional referenced schema/database qualifier
};

/**
 * @struct SqlTableInfo
 * @brief Table structure including schema/database qualifier, row estimate, columns, and foreign keys.
 */
struct SqlTableInfo {
    std::wstring schemaName;
    std::wstring name;
    int64_t rowCount = 0;
    std::vector<SqlColumnInfo> columns;
    std::vector<SqlForeignKeyInfo> foreignKeys;
};

struct SqlQueryResult {
    bool success = false;
    bool isSelect = false;
    std::wstring errorMessage;
    std::wstring statusMessage;
    int64_t affectedRows = 0;
    double executionMs = 0.0;
    std::vector<std::wstring> columns;
    std::vector<std::vector<std::wstring>> rows;
};

class SqlEngine {
public:
    /**
     * @brief Discovers SQLite database files (.sqlite, .sqlite3, .db) in the workspace and live SQL servers.
     */
    static std::vector<SqlConnectionProfile> DiscoverWorkspaceDatabases(
        const std::wstring& workspaceRoot,
        const std::wstring& mysqlHost = L"127.0.0.1",
        int mysqlPort = 3306,
        const std::wstring& mysqlUser = L"root",
        const std::wstring& mysqlPass = L"",
        const std::wstring& mysqlDb = L"");

    /**
     * @brief Performs a fast non-blocking TCP port check (default 35ms timeout).
     */
    static bool IsTcpPortOpen(const std::wstring& host, int port, int timeoutMs = 35);

    /**
     * @brief Checks whether a TCP port can be exclusively bound locally (0.0.0.0 and ::).
     */
    static bool IsPortAvailableForBind(int port);

    /**
     * @brief Resolves the active dynamic MySQL/MariaDB port (env PHIDE_MYSQL_PORT, mysql_port.txt, .env, or live scan).
     */
    static int ResolveDynamicMysqlPort(int preferredPort = 3306, const std::wstring& workspaceRoot = L"");

    /**
     * @brief Returns all local TCP ports currently listened on by live `mysqld.exe` / `mariadbd.exe` processes.
     */
    static std::vector<int> GetListeningMysqlPorts();

    /**
     * @brief Returns true if a local `mysqld.exe` / `mariadbd.exe` process is listening on `port`.
     */
    static bool IsLocalMysqlListeningOnPort(int port);

    /**
     * @brief Starts the local database server for the given profile (MySQL, PostgreSQL, or SQL Server).
     */
    static bool StartDatabaseServer(const SqlConnectionProfile& profile, std::wstring& outMessage, int* outResolvedPort = nullptr);

    /**
     * @brief Stops the local database server for the given profile (MySQL, PostgreSQL, or SQL Server).
     */
    static bool StopDatabaseServer(const SqlConnectionProfile& profile, std::wstring& outMessage);

    /**
     * @brief Lists user databases/schemas available on the connected server profile.
     */
    static std::vector<std::wstring> ListDatabases(const SqlConnectionProfile& profile, std::wstring& outError);

    /**
     * @brief Lists tables, columns, and row counts for the selected profile/database.
     */
    static std::vector<SqlTableInfo> InspectSchema(const SqlConnectionProfile& profile, std::wstring& outError);

    /**
     * @brief Lists tables and columns across all non-system databases/schemas on the connected server
     *        so host-language SQL validation does not emit false positives when ERD views another DB.
     */
    static std::vector<SqlTableInfo> InspectAllServerSchemas(const SqlConnectionProfile& profile, std::wstring& outError);

    /**
     * @brief Executes one or more semicolon-separated SQL statements natively.
     */
    static SqlQueryResult ExecuteQuery(const SqlConnectionProfile& profile, const std::wstring& sqlText);

    /**
     * @brief Builds a server-side LIMIT/OFFSET paginated SELECT statement for a table.
     */
    static std::wstring BuildPagedSelectQuery(const SqlConnectionProfile& profile,
                                              const SqlTableInfo& table,
                                              size_t pageIndex,
                                              size_t pageSize);

    /**
     * @brief Executes a SELECT query with server-side LIMIT/OFFSET pagination when eligible.
     */
    static SqlQueryResult ExecuteQueryWithPagination(const SqlConnectionProfile& profile,
                                                     const std::wstring& sqlText,
                                                     size_t pageIndex,
                                                     size_t pageSize,
                                                     int64_t& outTotalRows,
                                                     bool& outUsedServerPaging);

    /**
     * @brief Commits an inline grid cell edit using primary key matching.
     */
    static SqlQueryResult UpdateTableCell(const SqlConnectionProfile& profile,
                                          const std::wstring& schemaName,
                                          const std::wstring& tableName,
                                          const std::wstring& targetCol,
                                          const std::wstring& newValue,
                                          const std::vector<std::pair<std::wstring, std::wstring>>& pkColumnsAndValues);

    /**
     * @brief Exports query result rows to an RFC 4180 UTF-8 CSV file.
     */
    static bool ExportCsv(const SqlQueryResult& res, const std::wstring& outFilePath);

    /**
     * @brief Exports query result rows to a UTF-8 JSON array of row objects.
     */
    static bool ExportJson(const SqlQueryResult& res, const std::wstring& outFilePath);

    /**
     * @brief Locates a native SQLite3 DLL (`sqlite3.dll` or `libsqlite3.dll`) on the system.
     */
    static std::wstring FindNativeSqliteDllPath();
};

} // namespace phide::db
