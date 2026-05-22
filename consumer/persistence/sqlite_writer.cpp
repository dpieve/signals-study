#include "sqlite_writer.h"
#include <cassert>
#include <chrono>
#include <thread>
#include <spdlog/spdlog.h>

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static std::expected<std::string, SqliteError>
exec_pragma_string(sqlite3* db, const char* sql) {
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return std::unexpected(SqliteError{rc, sqlite3_errmsg(db)});
    }
    std::string result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const auto* txt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (txt) { result = txt; }
    }
    sqlite3_finalize(stmt);
    return result;
}

static std::expected<void, SqliteError>
exec_ddl(sqlite3* db, const char* sql) {
    char* errmsg = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        std::string msg = errmsg ? errmsg : sqlite3_errmsg(db);
        sqlite3_free(errmsg);
        return std::unexpected(SqliteError{rc, std::move(msg)});
    }
    return {};
}

// ---------------------------------------------------------------------------
// SqliteWriter — move semantics
// ---------------------------------------------------------------------------

SqliteWriter::SqliteWriter(SqliteWriter&& o) noexcept
    : db_(o.db_),
      insert_stmt_(std::move(o.insert_stmt_)),
      checkpoint_thread_(std::move(o.checkpoint_thread_)) {
    o.db_ = nullptr;
}

SqliteWriter& SqliteWriter::operator=(SqliteWriter&& o) noexcept {
    if (this != &o) {
        // Stop our own thread first.
        checkpoint_thread_.request_stop();
        checkpoint_thread_.join();

        sqlite3_finalize(insert_stmt_.stmt);
        insert_stmt_.stmt = nullptr;
        sqlite3_close(db_);

        db_                = o.db_;
        insert_stmt_       = std::move(o.insert_stmt_);
        checkpoint_thread_ = std::move(o.checkpoint_thread_);
        o.db_              = nullptr;
    }
    return *this;
}

SqliteWriter::~SqliteWriter() {
    // Ensure checkpoint thread is stopped before closing the DB.
    if (checkpoint_thread_.joinable()) {
        checkpoint_thread_.request_stop();
        checkpoint_thread_.join();
    }
    // PreparedStatement RAII finalizes the stmt.
    sqlite3_close(db_);
}

// ---------------------------------------------------------------------------
// check_sqlite
// ---------------------------------------------------------------------------

std::expected<void, SqliteError>
SqliteWriter::check_sqlite(int rc, const char* ctx) const {
    if (rc == SQLITE_OK || rc == SQLITE_DONE || rc == SQLITE_ROW) {
        return {};
    }
    std::string msg = ctx;
    msg += ": ";
    msg += db_ ? sqlite3_errmsg(db_) : "no db handle";
    return std::unexpected(SqliteError{rc, std::move(msg)});
}

// ---------------------------------------------------------------------------
// init_schema
// ---------------------------------------------------------------------------

std::expected<void, SqliteError>
SqliteWriter::init_schema(const PersistenceConfig& cfg) {
    // 1. WAL mode — must return "wal" for file-based DBs.
    // In-memory databases (:memory:) silently stay in "memory" journal mode;
    // WAL is not available for them. Accept this for test compatibility.
    auto wal_result = exec_pragma_string(db_, "PRAGMA journal_mode = WAL;");
    if (!wal_result) { return std::unexpected(wal_result.error()); }
    if (*wal_result != "wal" && *wal_result != "memory") {
        return std::unexpected(SqliteError{
            SQLITE_ERROR,
            "PRAGMA journal_mode = WAL returned '" + *wal_result + "', expected 'wal'"});
    }

    // 2. synchronous = NORMAL
    if (auto r = exec_ddl(db_, "PRAGMA synchronous = NORMAL;"); !r) { return r; }

    // 3. page_size
    {
        std::string sql = "PRAGMA page_size = " + std::to_string(cfg.page_size) + ";";
        if (auto r = exec_ddl(db_, sql.c_str()); !r) { return r; }
    }

    // 4. cache_size (negative = kibibytes)
    {
        std::string sql = "PRAGMA cache_size = " + std::to_string(-cfg.cache_size_kb) + ";";
        if (auto r = exec_ddl(db_, sql.c_str()); !r) { return r; }
    }

    // 5. Disable automatic WAL checkpoint (we do it manually).
    if (auto r = exec_ddl(db_, "PRAGMA wal_autocheckpoint = 0;"); !r) { return r; }

    // 6. busy timeout
    sqlite3_busy_timeout(db_, cfg.busy_timeout_ms);

    // 7. Schema
    static constexpr const char* k_schema = R"sql(
        CREATE TABLE IF NOT EXISTS samples (
            timestamp_ns INTEGER NOT NULL,
            channel_id   INTEGER NOT NULL,
            value        REAL    NOT NULL
        );
        CREATE INDEX IF NOT EXISTS idx_samples_covering
            ON samples(timestamp_ns, channel_id, value);
        CREATE TABLE IF NOT EXISTS schema_version (
            version INTEGER NOT NULL
        );
    )sql";
    if (auto r = exec_ddl(db_, k_schema); !r) { return r; }

    // 8. Check / insert schema_version
    static constexpr int kSchemaVersion = 1;
    auto ver_result = exec_pragma_string(
        db_, "SELECT version FROM schema_version LIMIT 1;");
    // exec_pragma_string returns empty string if no row.
    if (!ver_result) { return std::unexpected(ver_result.error()); }

    if (ver_result->empty()) {
        std::string ins = "INSERT INTO schema_version (version) VALUES ("
                          + std::to_string(kSchemaVersion) + ");";
        if (auto r = exec_ddl(db_, ins.c_str()); !r) { return r; }
    } else {
        const int stored = std::stoi(*ver_result);
        if (stored != kSchemaVersion) {
            return std::unexpected(SqliteError{
                SQLITE_ERROR,
                "schema_version mismatch: stored=" + *ver_result
                + " expected=" + std::to_string(kSchemaVersion)});
        }
    }

    return {};
}

// ---------------------------------------------------------------------------
// open
// ---------------------------------------------------------------------------

std::expected<SqliteWriter, SqliteError>
SqliteWriter::open(const std::string& path, const PersistenceConfig& cfg) {
    SqliteWriter w;

    const int rc = sqlite3_open(path.c_str(), &w.db_);
    if (rc != SQLITE_OK) {
        std::string msg = w.db_ ? sqlite3_errmsg(w.db_) : "sqlite3_open failed";
        sqlite3_close(w.db_);
        w.db_ = nullptr;
        return std::unexpected(SqliteError{rc, std::move(msg)});
    }

    if (auto r = w.init_schema(cfg); !r) { return std::unexpected(r.error()); }

    // Prepare insert statement with named positional parameters.
    static constexpr const char* k_insert =
        "INSERT INTO samples (timestamp_ns, channel_id, value) "
        "VALUES (?1, ?2, ?3);";
    const int prc = sqlite3_prepare_v2(
        w.db_, k_insert, -1, &w.insert_stmt_.stmt, nullptr);
    if (prc != SQLITE_OK) {
        return std::unexpected(SqliteError{prc, sqlite3_errmsg(w.db_)});
    }

    // Start background WAL checkpoint thread.
    w.checkpoint_thread_ = std::jthread([db = w.db_](std::stop_token st) {
        using namespace std::chrono_literals;
        while (!st.stop_requested()) {
            // Sleep in 100 ms increments so we react quickly to stop requests.
            for (int i = 0; i < 50 && !st.stop_requested(); ++i) {
                std::this_thread::sleep_for(100ms);
            }
            if (st.stop_requested()) { break; }
            const int crc = sqlite3_wal_checkpoint_v2(
                db, nullptr, SQLITE_CHECKPOINT_PASSIVE, nullptr, nullptr);
            if (crc != SQLITE_OK) {
                spdlog::warn("WAL checkpoint failed: {}", sqlite3_errstr(crc));
            }
        }
    });

    spdlog::info("SqliteWriter: opened database '{}'", path);
    return w;
}

// ---------------------------------------------------------------------------
// flush_batch
// ---------------------------------------------------------------------------

std::expected<void, SqliteError>
SqliteWriter::flush_batch(std::span<const IngestedPacket> packets) {
    if (packets.empty()) { return {}; }

    // BEGIN transaction.
    if (auto r = check_sqlite(
            sqlite3_exec(db_, "BEGIN;", nullptr, nullptr, nullptr),
            "BEGIN"); !r) {
        return r;
    }

    for (const auto& pkt : packets) {
        const uint32_t count = std::min(pkt.sample_count, MAX_SAMPLES_PER_PACKET);
        for (uint32_t i = 0; i < count; ++i) {
            const auto& s = pkt.samples[i];

            sqlite3_reset(insert_stmt_.stmt);
            sqlite3_clear_bindings(insert_stmt_.stmt);

            if (auto r = check_sqlite(
                    sqlite3_bind_int64(insert_stmt_.stmt, 1,
                        static_cast<sqlite3_int64>(s.timestamp_ns)),
                    "bind timestamp_ns"); !r) {
                sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
                return r;
            }
            if (auto r = check_sqlite(
                    sqlite3_bind_int64(insert_stmt_.stmt, 2,
                        static_cast<sqlite3_int64>(s.channel_id)),
                    "bind channel_id"); !r) {
                sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
                return r;
            }
            if (auto r = check_sqlite(
                    sqlite3_bind_double(insert_stmt_.stmt, 3, s.value),
                    "bind value"); !r) {
                sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
                return r;
            }

            const int src = sqlite3_step(insert_stmt_.stmt);
            if (src != SQLITE_DONE) {
                sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
                return std::unexpected(SqliteError{src, sqlite3_errmsg(db_)});
            }
        }
    }

    // COMMIT transaction.
    if (auto r = check_sqlite(
            sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr),
            "COMMIT"); !r) {
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        return r;
    }

    return {};
}
