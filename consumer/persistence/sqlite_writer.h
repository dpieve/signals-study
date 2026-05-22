#pragma once
#include <expected>
#include <span>
#include <string>
#include <thread>
#include <sqlite3.h>
#include "../common/telemetry_types.h"
#include "../config/consumer_config.h"

struct SqliteError {
    int         code;
    std::string message;
};

/// RAII wrapper around a single sqlite3_stmt*.
struct PreparedStatement {
    sqlite3_stmt* stmt = nullptr;

    PreparedStatement() = default;
    PreparedStatement(const PreparedStatement&) = delete;
    PreparedStatement& operator=(const PreparedStatement&) = delete;

    PreparedStatement(PreparedStatement&& o) noexcept
        : stmt(o.stmt) { o.stmt = nullptr; }
    PreparedStatement& operator=(PreparedStatement&& o) noexcept {
        if (this != &o) {
            sqlite3_finalize(stmt);
            stmt   = o.stmt;
            o.stmt = nullptr;
        }
        return *this;
    }

    ~PreparedStatement() { sqlite3_finalize(stmt); }
};

/// Owns a single SQLite database connection and provides a batched insert
/// interface backed by WAL mode.
///
/// Obtain an instance via SqliteWriter::open().
class SqliteWriter {
public:
    SqliteWriter(const SqliteWriter&) = delete;
    SqliteWriter& operator=(const SqliteWriter&) = delete;
    SqliteWriter(SqliteWriter&&) noexcept;
    SqliteWriter& operator=(SqliteWriter&&) noexcept;
    ~SqliteWriter();

    /// Open (or create) a database at @p path with the given configuration.
    [[nodiscard]] static std::expected<SqliteWriter, SqliteError>
    open(const std::string& path, const PersistenceConfig& cfg);

    /// Write all packets in @p packets to the database inside a single
    /// BEGIN/COMMIT transaction.
    [[nodiscard]] std::expected<void, SqliteError>
    flush_batch(std::span<const IngestedPacket> packets);

private:
    SqliteWriter() = default;

    [[nodiscard]] std::expected<void, SqliteError>
    check_sqlite(int rc, const char* ctx) const;

    [[nodiscard]] std::expected<void, SqliteError>
    init_schema(const PersistenceConfig& cfg);

    sqlite3*          db_             = nullptr;
    PreparedStatement insert_stmt_;

    // Background WAL checkpoint thread.
    std::jthread checkpoint_thread_;
};
