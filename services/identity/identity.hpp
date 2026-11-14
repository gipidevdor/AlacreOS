#pragma once

#include <expected>
#include <stddef.h>
#include <stdint.h>

#if !__STDC_HOSTED__ && !defined(__cpp_lib_freestanding_expected)
#error "The identity service requires a C++23 library with freestanding std::expected"
#endif

namespace alacre::identity {

enum class AccountId : uint64_t {};
enum class ChannelId : uint64_t {};
enum class SessionId : uint64_t {};
using Tick = uint64_t;

enum class Role : uint8_t { administrator, operator_, auditor };
enum class Permission : uint8_t { manage_accounts, operate, read_audit };
enum class Error : uint8_t {
    invalid_configuration, already_configured, not_configured, invalid_time, access_denied,
    capacity, channel_in_use, overflow, last_administrator, audit_record_unavailable, storage_unavailable,
};

struct AccountConfig {
    AccountId id;
    Role role;
    bool enabled;
};

enum class AuditAction : uint8_t {
    open_session, close_session, disconnect, enable_account, disable_account, clock_fault, authentication_failure,
};

struct AuditRecord {
    Tick time;
    AuditAction action;
    AccountId actor;
    AccountId target;
    SessionId session;
    bool accepted;
};

struct AuditEntry {
    AuditRecord record;
    size_t retained;
    uint64_t overwritten;
};

struct AccountStore {
    void *context{};
    // Persist the status change and its audit event in the same transaction.
    // Failure stops requests until the directory restarts from durable state.
    bool (*commit)(void *, AccountId, bool, const AuditRecord &) noexcept{};
};

struct AuditSink {
    void *context{};
    bool (*append)(void *, const AuditRecord &) noexcept{};
};

// Owned by a single, serialized management service. IDs are scoped to its boot.
// The adapter supplies time and channel identity from trusted sources.
class Directory {
public:
    static constexpr size_t account_capacity = 16;
    static constexpr size_t sessions_per_account = 2;
    static constexpr size_t audit_capacity = 32;

    Directory() noexcept = default;
    Directory(const Directory &) = delete;
    Directory &operator=(const Directory &) = delete;

    // Commission once from a trusted configuration before accepting requests.
    [[nodiscard]] std::expected<void, Error> commission(
        const AccountConfig *accounts, size_t count, Tick session_lifetime,
        AccountStore store = {}, AuditSink sink = {}) noexcept;

    // Call only after the trusted verifier authenticates this account on this channel.
    [[nodiscard]] std::expected<SessionId, Error> open_session(
        AccountId account, ChannelId channel, Tick now) noexcept;
    [[nodiscard]] std::expected<void, Error> close_session(
        SessionId session, ChannelId channel, Tick now) noexcept;
    void disconnect(ChannelId channel, Tick now) noexcept;
    void authentication_failed(Tick now) noexcept;
    [[nodiscard]] bool audit_delivery_failed() const noexcept { return audit_delivery_failed_; }
    [[nodiscard]] std::expected<void, Error> authorize(
        SessionId session, ChannelId channel, Permission permission, Tick now) noexcept;
    [[nodiscard]] std::expected<void, Error> set_enabled(
        SessionId session, ChannelId channel, AccountId target, bool enabled, Tick now) noexcept;
    [[nodiscard]] std::expected<AuditEntry, Error> read_audit(
        SessionId session, ChannelId channel, size_t offset, Tick now) noexcept;

private:
    struct Session {
        SessionId id;
        ChannelId channel;
        Tick expires;
    };

    struct Account {
        AccountConfig config;
        Session sessions[sessions_per_account];
    };

    struct SessionMatch {
        Account *account;
        Session *session;
    };

    [[nodiscard]] std::expected<void, Error> observe(Tick now) noexcept;
    [[nodiscard]] Account *find_account(AccountId id) noexcept;
    [[nodiscard]] SessionMatch find_session(SessionId id, ChannelId channel) noexcept;
    void record(AuditAction action, AccountId actor, AccountId target, SessionId session,
                bool accepted) noexcept;
    void retain(const AuditRecord &entry) noexcept;

    Account accounts_[account_capacity]{};
    AuditRecord audit_[audit_capacity]{};
    size_t account_count_{};
    size_t audit_start_{};
    size_t audit_count_{};
    size_t enabled_administrators_{};
    uint64_t overwritten_{};
    uint64_t last_session_{};
    Tick session_lifetime_{};
    Tick now_{};
    bool clock_valid_{true};
    bool storage_valid_{true};
    bool audit_delivery_failed_{};
    AccountStore store_{};
    AuditSink sink_{};
};

} // namespace alacre::identity
