#include "identity.hpp"

namespace alacre::identity {
namespace {

bool permits(Role role, Permission permission) noexcept {
    switch (role) {
    case Role::administrator: return permission == Permission::manage_accounts;
    case Role::operator_: return permission == Permission::operate;
    case Role::auditor: return permission == Permission::read_audit;
    }
    return false;
}

} // namespace

std::expected<void, Error> Directory::commission(
    const AccountConfig *accounts, size_t count, Tick session_lifetime,
    AccountStore store, AuditSink sink) noexcept {
    if (account_count_ != 0) {
        return std::unexpected(Error::already_configured);
    }
    if (accounts == nullptr || count == 0 || count > account_capacity || session_lifetime == 0) {
        return std::unexpected(Error::invalid_configuration);
    }
    size_t enabled_administrators = 0;
    for (size_t i = 0; i < count; ++i) {
        const auto &account = accounts[i];
        if (account.id == AccountId{} ||
            (account.role != Role::administrator && account.role != Role::operator_ &&
             account.role != Role::auditor)) {
            return std::unexpected(Error::invalid_configuration);
        }
        for (size_t j = 0; j < i; ++j) {
            if (accounts[j].id == account.id) {
                return std::unexpected(Error::invalid_configuration);
            }
        }
        if (account.role == Role::administrator && account.enabled) {
            ++enabled_administrators;
        }
    }
    if (enabled_administrators == 0) {
        return std::unexpected(Error::invalid_configuration);
    }
    for (size_t i = 0; i < count; ++i) {
        accounts_[i].config = accounts[i];
    }
    account_count_ = count;
    enabled_administrators_ = enabled_administrators;
    session_lifetime_ = session_lifetime;
    store_ = store;
    sink_ = sink;
    return {};
}

std::expected<void, Error> Directory::observe(Tick now) noexcept {
    if (account_count_ == 0) {
        return std::unexpected(Error::not_configured);
    }
    if (!storage_valid_) {
        return std::unexpected(Error::storage_unavailable);
    }
    if (!clock_valid_) {
        return std::unexpected(Error::invalid_time);
    }
    if (now < now_) {
        clock_valid_ = false;
        record(AuditAction::clock_fault, {}, {}, {}, false);
        return std::unexpected(Error::invalid_time);
    }
    now_ = now;
    return {};
}

Directory::Account *Directory::find_account(AccountId id) noexcept {
    for (size_t i = 0; i < account_count_; ++i) {
        if (accounts_[i].config.id == id) {
            return &accounts_[i];
        }
    }
    return nullptr;
}

Directory::SessionMatch Directory::find_session(SessionId id, ChannelId channel) noexcept {
    if (id == SessionId{} || channel == ChannelId{}) {
        return {};
    }
    for (size_t i = 0; i < account_count_; ++i) {
        auto &account = accounts_[i];
        for (auto &session : account.sessions) {
            if (session.id == id && session.channel == channel && now_ < session.expires) {
                return {&account, &session};
            }
        }
    }
    return {};
}

void Directory::record(AuditAction action, AccountId actor, AccountId target, SessionId session,
                       bool accepted) noexcept {
    const AuditRecord entry{now_, action, actor, target, session, accepted};
    retain(entry);
    if (sink_.append != nullptr && !sink_.append(sink_.context, entry)) {
        audit_delivery_failed_ = true;
    }
}

void Directory::retain(const AuditRecord &entry) noexcept {
    audit_[(audit_start_ + audit_count_) % audit_capacity] = entry;
    if (audit_count_ < audit_capacity) {
        ++audit_count_;
    } else {
        audit_start_ = (audit_start_ + 1) % audit_capacity;
        if (overwritten_ != UINT64_MAX) {
            ++overwritten_;
        }
    }
}

std::expected<SessionId, Error> Directory::open_session(
    AccountId account, ChannelId channel, Tick now) noexcept {
    if (const auto observed = observe(now); !observed) {
        return std::unexpected(observed.error());
    }
    auto *entry = find_account(account);
    if (entry == nullptr || !entry->config.enabled || channel == ChannelId{}) {
        record(AuditAction::open_session, {}, account, {}, false);
        return std::unexpected(Error::access_denied);
    }
    if (session_lifetime_ > UINT64_MAX - now_ || last_session_ == UINT64_MAX) {
        record(AuditAction::open_session, account, account, {}, false);
        return std::unexpected(Error::overflow);
    }

    for (size_t i = 0; i < account_count_; ++i) {
        for (const auto &session : accounts_[i].sessions) {
            if (session.channel == channel && now_ < session.expires) {
                record(AuditAction::open_session, account, account, {}, false);
                return std::unexpected(Error::channel_in_use);
            }
        }
    }
    for (auto &session : entry->sessions) {
        if (session.id == SessionId{} || now_ >= session.expires) {
            const auto id = static_cast<SessionId>(++last_session_);
            session = {id, channel, now_ + session_lifetime_};
            record(AuditAction::open_session, account, account, id, true);
            return id;
        }
    }
    record(AuditAction::open_session, account, account, {}, false);
    return std::unexpected(Error::capacity);
}

std::expected<void, Error> Directory::close_session(SessionId id, ChannelId channel, Tick now) noexcept {
    if (const auto observed = observe(now); !observed) {
        return observed;
    }
    const auto match = find_session(id, channel);
    if (match.session == nullptr) {
        record(AuditAction::close_session, {}, {}, {}, false);
        return std::unexpected(Error::access_denied);
    }
    const auto account = match.account->config.id;
    record(AuditAction::close_session, account, account, id, true);
    *match.session = {};
    return {};
}

void Directory::disconnect(ChannelId channel, Tick now) noexcept {
    // Teardown still revokes access if the clock went backwards.
    (void)observe(now);
    for (size_t i = 0; i < account_count_; ++i) {
        auto &account = accounts_[i];
        for (auto &session : account.sessions) {
            if (session.id != SessionId{} && session.channel == channel) {
                record(AuditAction::disconnect, account.config.id, account.config.id, session.id, true);
                session = {};
            }
        }
    }
}

void Directory::authentication_failed(Tick now) noexcept {
    if (observe(now)) {
        record(AuditAction::authentication_failure, {}, {}, {}, false);
    }
}

std::expected<void, Error> Directory::authorize(
    SessionId id, ChannelId channel, Permission permission, Tick now) noexcept {
    if (const auto observed = observe(now); !observed) {
        return observed;
    }
    const auto *account = find_session(id, channel).account;
    if (account != nullptr && account->config.enabled && permits(account->config.role, permission)) {
        return {};
    }
    return std::unexpected(Error::access_denied);
}

std::expected<void, Error> Directory::set_enabled(
    SessionId id, ChannelId channel, AccountId target, bool enabled, Tick now) noexcept {
    if (const auto observed = observe(now); !observed) {
        return observed;
    }
    const auto *caller = find_session(id, channel).account;
    const auto actor = caller != nullptr ? caller->config.id : AccountId{};
    auto *account = caller != nullptr && caller->config.enabled &&
                    permits(caller->config.role, Permission::manage_accounts)
        ? find_account(target) : nullptr;
    const auto action = enabled ? AuditAction::enable_account : AuditAction::disable_account;
    if (account == nullptr) {
        record(action, actor, target, caller != nullptr ? id : SessionId{}, false);
        return std::unexpected(Error::access_denied);
    }
    if (account->config.enabled == enabled) {
        record(action, actor, target, id, true);
        return {};
    }
    const bool administrator = account->config.role == Role::administrator;
    if (administrator && !enabled && enabled_administrators_ == 1) {
        record(action, actor, target, id, false);
        return std::unexpected(Error::last_administrator);
    }
    const AuditRecord event{now_, action, actor, target, id, true};
    if (store_.commit != nullptr && !store_.commit(store_.context, target, enabled, event)) {
        record(action, actor, target, id, false);
        storage_valid_ = false;
        return std::unexpected(Error::storage_unavailable);
    }
    if (administrator) {
        enabled_administrators_ = enabled ? enabled_administrators_ + 1 : enabled_administrators_ - 1;
    }
    account->config.enabled = enabled;
    if (!enabled) {
        for (auto &session : account->sessions) {
            session = {};
        }
    }
    if (store_.commit != nullptr) {
        retain(event);
    } else {
        record(action, actor, target, id, true);
    }
    return {};
}

std::expected<AuditEntry, Error> Directory::read_audit(
    SessionId id, ChannelId channel, size_t offset, Tick now) noexcept {
    const auto result = authorize(id, channel, Permission::read_audit, now);
    if (!result) {
        return std::unexpected(result.error());
    }
    if (offset >= audit_count_) {
        return std::unexpected(Error::audit_record_unavailable);
    }
    return AuditEntry{audit_[(audit_start_ + offset) % audit_capacity], audit_count_, overwritten_};
}

} // namespace alacre::identity
