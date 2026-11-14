#include "host_store.hpp"

#include <memory>
#include <sqlite3.h>

namespace alacre::identity {
namespace {

using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;

Statement prepare(sqlite3 *db, const char *sql) noexcept {
    sqlite3_stmt *statement = nullptr;
    (void)sqlite3_prepare_v2(db, sql, -1, &statement, nullptr);
    return {statement, sqlite3_finalize};
}

bool execute(sqlite3 *db, const char *sql) noexcept {
    return sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}

} // namespace

HostStore::~HostStore() {
    if (db_ != nullptr) {
        (void)sqlite3_close(db_);
    }
}

bool HostStore::open(const char *path) noexcept {
    if (db_ != nullptr || path == nullptr || *path == '\0') {
        return false;
    }
    sqlite3 *opened = nullptr;
    if (sqlite3_open_v2(path, &opened, SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOFOLLOW, nullptr) != SQLITE_OK ||
        sqlite3_db_readonly(opened, "main") != 0 ||
        !execute(opened, "PRAGMA trusted_schema = OFF; PRAGMA synchronous = FULL; PRAGMA fullfsync = ON;")) {
        (void)sqlite3_close(opened);
        return false;
    }
    db_ = opened;
    return true;
}

bool HostStore::load(AccountConfig (&accounts)[Directory::account_capacity], size_t &count) noexcept {
    if (db_ == nullptr) {
        return false;
    }
    auto statement = prepare(db_, "SELECT id, role, enabled FROM accounts ORDER BY id LIMIT ?");
    if (!statement || sqlite3_bind_int64(statement.get(), 1, Directory::account_capacity + 1) != SQLITE_OK) {
        return false;
    }
    AccountConfig loaded[Directory::account_capacity]{};
    size_t size = 0;
    int status;
    while ((status = sqlite3_step(statement.get())) == SQLITE_ROW) {
        const auto id = sqlite3_column_int64(statement.get(), 0);
        const auto role = sqlite3_column_int64(statement.get(), 1);
        const auto enabled = sqlite3_column_int64(statement.get(), 2);
        if (size == Directory::account_capacity || sqlite3_column_type(statement.get(), 0) != SQLITE_INTEGER ||
            sqlite3_column_type(statement.get(), 1) != SQLITE_INTEGER ||
            sqlite3_column_type(statement.get(), 2) != SQLITE_INTEGER || id <= 0 || role < 0 || role > 2 ||
            (enabled != 0 && enabled != 1)) {
            return false;
        }
        loaded[size++] = {static_cast<AccountId>(id), static_cast<Role>(role), enabled == 1};
    }
    if (status != SQLITE_DONE || size == 0) {
        return false;
    }
    for (size_t i = 0; i < size; ++i) {
        accounts[i] = loaded[i];
    }
    count = size;
    return true;
}

AccountStore HostStore::account_store() noexcept {
    return {this, update};
}

AuditSink HostStore::audit_sink() noexcept {
    return {this, append};
}

bool HostStore::update(void *store, AccountId account, bool enabled, const AuditRecord &event) noexcept {
    auto *db = static_cast<HostStore *>(store)->db_;
    const auto id = static_cast<uint64_t>(account);
    if (db == nullptr || id == 0 || id > INT64_MAX || !execute(db, "BEGIN IMMEDIATE")) {
        return false;
    }
    bool updated;
    {
        auto statement = prepare(db, "UPDATE accounts SET enabled = ? WHERE id = ?");
        updated = statement && sqlite3_bind_int(statement.get(), 1, enabled ? 1 : 0) == SQLITE_OK &&
                  sqlite3_bind_int64(statement.get(), 2, static_cast<sqlite3_int64>(id)) == SQLITE_OK &&
                  sqlite3_step(statement.get()) == SQLITE_DONE && sqlite3_changes(db) == 1;
    }
    if (updated && append(store, event) && execute(db, "COMMIT")) {
        return true;
    }
    (void)execute(db, "ROLLBACK");
    return false;
}

bool HostStore::append(void *store, const AuditRecord &event) noexcept {
    auto *db = static_cast<HostStore *>(store)->db_;
    constexpr auto fits = [](uint64_t value) { return value <= INT64_MAX; };
    const auto actor = static_cast<uint64_t>(event.actor);
    const auto target = static_cast<uint64_t>(event.target);
    const auto session = static_cast<uint64_t>(event.session);
    if (db == nullptr || !fits(event.time) || !fits(actor) || !fits(target) || !fits(session)) {
        return false;
    }
    auto statement = prepare(db, "INSERT INTO audit(time, action, actor, target, session, accepted) VALUES(?,?,?,?,?,?)");
    return statement && sqlite3_bind_int64(statement.get(), 1, static_cast<sqlite3_int64>(event.time)) == SQLITE_OK &&
           sqlite3_bind_int(statement.get(), 2, static_cast<int>(event.action)) == SQLITE_OK &&
           sqlite3_bind_int64(statement.get(), 3, static_cast<sqlite3_int64>(actor)) == SQLITE_OK &&
           sqlite3_bind_int64(statement.get(), 4, static_cast<sqlite3_int64>(target)) == SQLITE_OK &&
           sqlite3_bind_int64(statement.get(), 5, static_cast<sqlite3_int64>(session)) == SQLITE_OK &&
           sqlite3_bind_int(statement.get(), 6, event.accepted ? 1 : 0) == SQLITE_OK &&
           sqlite3_step(statement.get()) == SQLITE_DONE && sqlite3_changes(db) == 1;
}

} // namespace alacre::identity
