#pragma once

#include "identity.hpp"

namespace alacre::identity {

// Bounded, single-writer identity journal for the development host.
class HostStore {
public:
    static constexpr size_t record_capacity = 1024;

    HostStore() noexcept = default;
    ~HostStore();
    HostStore(const HostStore &) = delete;
    HostStore &operator=(const HostStore &) = delete;

    [[nodiscard]] static bool provision(const char *directory, const AccountConfig *accounts, size_t count) noexcept;
    [[nodiscard]] bool open(const char *directory) noexcept;
    [[nodiscard]] bool load(AccountConfig (&accounts)[Directory::account_capacity], size_t &count) const noexcept;
    [[nodiscard]] bool read_audit(size_t offset, AuditRecord &record) const noexcept;
    [[nodiscard]] AccountStore account_store() noexcept;
    [[nodiscard]] AuditSink audit_sink() noexcept;

private:
    static bool update(void *store, AccountId account, bool enabled, const AuditRecord &event) noexcept;
    static bool append(void *store, const AuditRecord &record) noexcept;
    [[nodiscard]] bool write(bool change, const AuditRecord &record) noexcept;

    AccountConfig accounts_[Directory::account_capacity]{};
    size_t count_{};
    size_t records_{};
    int fd_{-1};
    bool failed_{};
};

} // namespace alacre::identity
