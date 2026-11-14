#pragma once

#include "identity.hpp"

struct sqlite3;

namespace alacre::identity {

// Reference backend for a dedicated management process on a POSIX host.
class HostStore {
public:
    HostStore() noexcept = default;
    ~HostStore();
    HostStore(const HostStore &) = delete;
    HostStore &operator=(const HostStore &) = delete;

    [[nodiscard]] bool open(const char *path) noexcept;
    [[nodiscard]] bool load(AccountConfig (&accounts)[Directory::account_capacity], size_t &count) noexcept;
    [[nodiscard]] AccountStore account_store() noexcept;
    [[nodiscard]] AuditSink audit_sink() noexcept;

private:
    static bool update(void *store, AccountId account, bool enabled, const AuditRecord &event) noexcept;
    static bool append(void *store, const AuditRecord &record) noexcept;
    sqlite3 *db_{};
};

} // namespace alacre::identity
