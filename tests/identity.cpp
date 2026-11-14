#include "identity.hpp"

#include <assert.h>

using namespace alacre::identity;

namespace {

constexpr AccountId admin{1};
constexpr AccountId backup{2};
constexpr AccountId operator_account{3};
constexpr AccountId auditor{4};
constexpr ChannelId management{1};
constexpr ChannelId operations{2};
constexpr ChannelId audit{3};
constexpr AccountConfig configuration[]{
    {admin, Role::administrator, true},
    {backup, Role::administrator, false},
    {operator_account, Role::operator_, true},
    {auditor, Role::auditor, true},
};

void commission(Directory &directory, Tick lifetime = 100) {
    assert(directory.commission(configuration, sizeof(configuration) / sizeof(configuration[0]), lifetime));
}

SessionId open(Directory &directory, AccountId account, ChannelId channel, Tick now = 0) {
    const auto result = directory.open_session(account, channel, now);
    assert(result);
    return *result;
}

void commissioning_validates_the_entire_configuration() {
    Directory directory;
    constexpr AccountConfig duplicate[]{{admin, Role::administrator, true}, {admin, Role::operator_, true}};
    constexpr AccountConfig invalid_role[]{configuration[0], {auditor, static_cast<Role>(255), true}};
    constexpr AccountConfig no_admin[]{{backup, Role::administrator, false}};
    constexpr AccountConfig zero_id[]{{AccountId{}, Role::administrator, true}};
    assert(directory.open_session(admin, management, 99) == std::unexpected(Error::not_configured));
    assert(directory.commission(duplicate, 2, 10) == std::unexpected(Error::invalid_configuration));
    assert(directory.commission(invalid_role, 2, 10) == std::unexpected(Error::invalid_configuration));
    assert(directory.commission(no_admin, 1, 10) == std::unexpected(Error::invalid_configuration));
    assert(directory.commission(zero_id, 1, 10) == std::unexpected(Error::invalid_configuration));
    assert(directory.commission(configuration, Directory::account_capacity + 1, 10) ==
           std::unexpected(Error::invalid_configuration));
    assert(directory.commission(configuration, 4, 0) == std::unexpected(Error::invalid_configuration));
    commission(directory);
    assert(directory.commission(configuration, 4, 10) == std::unexpected(Error::already_configured));
    assert(directory.open_session(admin, management, 0));
    assert(directory.open_session({}, operations, 0) == std::unexpected(Error::access_denied));
    assert(directory.open_session(admin, {}, 0) == std::unexpected(Error::access_denied));
}

void roles_are_separate_and_bound_to_the_channel() {
    Directory directory;
    commission(directory);
    const auto administrator = open(directory, admin, management);
    const auto worker = open(directory, operator_account, operations);
    const auto reviewer = open(directory, auditor, audit);
    constexpr Permission permissions[]{Permission::manage_accounts, Permission::operate, Permission::read_audit};
    const SessionId sessions[]{administrator, worker, reviewer};
    constexpr ChannelId channels[]{management, operations, audit};
    for (size_t role = 0; role < 3; ++role) {
        for (size_t permission = 0; permission < 3; ++permission) {
            assert(directory.authorize(sessions[role], channels[role], permissions[permission], 0).has_value() ==
                   (role == permission));
        }
    }
    assert(directory.authorize(administrator, management, static_cast<Permission>(255), 0) ==
           std::unexpected(Error::access_denied));
    assert(directory.set_enabled(administrator, operations, operator_account, false, 1) ==
           std::unexpected(Error::access_denied));
    assert(directory.close_session(worker, audit, 1) == std::unexpected(Error::access_denied));
    assert(directory.authorize(worker, operations, Permission::operate, 1));
    assert(directory.read_audit(administrator, management, 0, 1) == std::unexpected(Error::access_denied));
    const auto denied = directory.read_audit(reviewer, audit, 3, 1);
    assert(denied && denied->record.action == AuditAction::disable_account && !denied->record.accepted);
    assert(denied->record.actor == AccountId{} && denied->record.session == SessionId{});
    const auto first = directory.read_audit(reviewer, audit, 0, 1);
    assert(first && first->record.actor == admin && first->record.session == administrator);
}

void disabling_revokes_every_session_and_preserves_an_administrator() {
    Directory directory;
    commission(directory);
    const auto administrator = open(directory, admin, management);
    const auto first = open(directory, operator_account, operations);
    const ChannelId second_channel{4};
    const auto second = open(directory, operator_account, second_channel);
    assert(directory.set_enabled(administrator, management, admin, false, 1) ==
           std::unexpected(Error::last_administrator));
    assert(directory.set_enabled(administrator, management, operator_account, false, 1));
    assert(directory.authorize(first, operations, Permission::operate, 1) == std::unexpected(Error::access_denied));
    assert(directory.authorize(second, second_channel, Permission::operate, 1) ==
           std::unexpected(Error::access_denied));
    assert(directory.open_session(operator_account, operations, 1) == std::unexpected(Error::access_denied));
    assert(directory.set_enabled(administrator, management, operator_account, true, 2));
    const auto replacement = open(directory, operator_account, operations, 2);
    assert(replacement != first);
    assert(directory.authorize(first, operations, Permission::operate, 2) == std::unexpected(Error::access_denied));
    assert(directory.set_enabled(administrator, management, backup, true, 2));
    assert(directory.set_enabled(administrator, management, backup, true, 2));
    assert(directory.set_enabled(administrator, management, admin, false, 2));
    assert(directory.authorize(administrator, management, Permission::manage_accounts, 2) ==
           std::unexpected(Error::access_denied));
    const auto successor = open(directory, backup, management, 2);
    assert(directory.set_enabled(successor, management, backup, false, 2) ==
           std::unexpected(Error::last_administrator));
}

void expiry_disconnect_and_capacity_are_bounded() {
    Directory directory;
    commission(directory, 5);
    const auto first = open(directory, operator_account, operations, 3);
    const auto second = open(directory, operator_account, ChannelId{4}, 4);
    assert(directory.open_session(auditor, operations, 4) == std::unexpected(Error::channel_in_use));
    assert(directory.open_session(operator_account, ChannelId{5}, 4) == std::unexpected(Error::capacity));
    assert(directory.open_session(admin, management, 4));
    assert(directory.authorize(first, operations, Permission::operate, 7));
    assert(directory.authorize(first, operations, Permission::operate, 8) == std::unexpected(Error::access_denied));
    const auto replacement = open(directory, operator_account, operations, 8);
    assert(replacement != first);
    assert(directory.close_session(second, ChannelId{4}, 8));
    assert(directory.authorize(second, ChannelId{4}, Permission::operate, 8) ==
           std::unexpected(Error::access_denied));
    directory.disconnect(operations, 8);
    assert(directory.authorize(replacement, operations, Permission::operate, 8) ==
           std::unexpected(Error::access_denied));
    assert(directory.open_session(operator_account, operations, 8));
}

void clock_faults_fail_closed_and_deadlines_cannot_wrap() {
    Directory directory;
    commission(directory, 5);
    const auto id = open(directory, admin, management, UINT64_MAX - 5);
    assert(directory.open_session(auditor, audit, UINT64_MAX - 4) == std::unexpected(Error::overflow));
    assert(directory.authorize(id, management, Permission::manage_accounts, UINT64_MAX) ==
           std::unexpected(Error::access_denied));
    Directory regressed_clock;
    commission(regressed_clock, 5);
    const auto active = open(regressed_clock, admin, management, 3);
    assert(regressed_clock.authorize(active, management, Permission::manage_accounts, 2) ==
           std::unexpected(Error::invalid_time));
    assert(regressed_clock.authorize(active, management, Permission::manage_accounts, 4) ==
           std::unexpected(Error::invalid_time));
    assert(regressed_clock.open_session(auditor, audit, 4) == std::unexpected(Error::invalid_time));
}

void audit_reports_overwrite_without_blocking_account_operations() {
    Directory directory;
    commission(directory);
    const auto administrator = open(directory, admin, management);
    const auto reviewer = open(directory, auditor, audit);
    for (Tick time = 1; time <= Directory::audit_capacity; ++time) {
        assert(directory.set_enabled(administrator, management, operator_account, false, time));
    }
    const auto first = directory.read_audit(reviewer, audit, 0, Directory::audit_capacity);
    const auto last = directory.read_audit(reviewer, audit, Directory::audit_capacity - 1, Directory::audit_capacity);
    assert(first && first->retained == Directory::audit_capacity && first->overwritten == 2);
    assert(first->record.time == 1 && first->record.target == operator_account && first->record.accepted);
    assert(last && last->record.time == Directory::audit_capacity);
    assert(directory.read_audit(reviewer, audit, Directory::audit_capacity, Directory::audit_capacity) ==
           std::unexpected(Error::audit_record_unavailable));
}

} // namespace

int main() {
    commissioning_validates_the_entire_configuration();
    roles_are_separate_and_bound_to_the_channel();
    disabling_revokes_every_session_and_preserves_an_administrator();
    expiry_disconnect_and_capacity_are_bounded();
    clock_faults_fail_closed_and_deadlines_cannot_wrap();
    audit_reports_overwrite_without_blocking_account_operations();
}
