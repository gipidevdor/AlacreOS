#include "host_store.hpp"

#include <array>
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace alacre::identity {
namespace {

constexpr char journal_name[] = "identity.journal";
constexpr uint64_t format = 0x314c4e524a434c41; // "ALCJRNL1" in little endian.
constexpr size_t account_bytes = 10;
constexpr size_t header_bytes = 16 + Directory::account_capacity * account_bytes + 8;
constexpr size_t record_bytes = 56;
constexpr size_t record_checksum = record_bytes - 8;
using Header = std::array<uint8_t, header_bytes>;
using Record = std::array<uint8_t, record_bytes>;

struct File {
    explicit File(int descriptor) noexcept : fd{descriptor} {}
    ~File() {
        if (fd >= 0) {
            (void)close(fd);
        }
    }
    File(const File &) = delete;
    File &operator=(const File &) = delete;
    int fd;
};

void encode(uint8_t *bytes, uint64_t value) noexcept {
    for (unsigned i = 0; i < 8; ++i) {
        bytes[i] = static_cast<uint8_t>(value >> (i * 8));
    }
}

uint64_t decode(const uint8_t *bytes) noexcept {
    uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) {
        value |= uint64_t{bytes[i]} << (i * 8);
    }
    return value;
}

// CRC-64/ECMA-182 detects accidental corruption on replay.
uint64_t checksum(const uint8_t *bytes, size_t length) noexcept {
    uint64_t crc = 0;
    for (size_t i = 0; i < length; ++i) {
        crc ^= uint64_t{bytes[i]} << 56;
        for (unsigned bit = 0; bit < 8; ++bit) {
            const bool carry = (crc >> 63) != 0;
            crc <<= 1;
            if (carry) {
                crc ^= 0x42f0e1eba9ea3693;
            }
        }
    }
    return crc;
}

bool read_at(int fd, uint8_t *bytes, size_t length, off_t offset) noexcept {
    while (length != 0) {
        const auto n = pread(fd, bytes, length, offset);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            return false;
        }
        bytes += n;
        offset += n;
        length -= static_cast<size_t>(n);
    }
    return true;
}

bool write_at(int fd, const uint8_t *bytes, size_t length, off_t offset) noexcept {
    while (length != 0) {
        const auto n = pwrite(fd, bytes, length, offset);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            return false;
        }
        bytes += n;
        offset += n;
        length -= static_cast<size_t>(n);
    }
    return true;
}

bool sync(int fd) noexcept {
    int result;
    do {
        result = fsync(fd);
    } while (result < 0 && errno == EINTR);
    return result == 0;
}

bool sync_journal(int fd) noexcept {
#if defined(__APPLE__)
    int result;
    do {
        result = fcntl(fd, F_FULLFSYNC);
    } while (result < 0 && errno == EINTR);
    return result == 0;
#else
    return sync(fd);
#endif
}

int open_directory(const char *path) noexcept {
    if (path == nullptr || *path == '\0') {
        return -1;
    }
    File directory{::open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)};
    struct stat info{};
    if (directory.fd < 0 || fstat(directory.fd, &info) != 0 || info.st_uid != geteuid() ||
        (info.st_mode & (S_IRWXG | S_IRWXO)) != 0) {
        return -1;
    }
    const int result = directory.fd;
    directory.fd = -1;
    return result;
}

off_t record_offset(size_t offset) noexcept {
    return static_cast<off_t>(header_bytes + offset * record_bytes);
}

bool valid_event(const AuditRecord &event) noexcept {
    return static_cast<uint8_t>(event.action) <= static_cast<uint8_t>(AuditAction::authentication_failure);
}

bool decode_record(const Record &bytes, size_t index, bool &change, AuditRecord &event) noexcept {
    if (decode(bytes.data()) != index + 1 ||
        decode(bytes.data() + record_checksum) != checksum(bytes.data(), record_checksum) ||
        bytes[40] > static_cast<uint8_t>(AuditAction::authentication_failure) ||
        bytes[41] > 1 || bytes[42] > 1) {
        return false;
    }
    for (size_t i = 43; i < record_checksum; ++i) {
        if (bytes[i] != 0) {
            return false;
        }
    }
    event = {decode(bytes.data() + 8), static_cast<AuditAction>(bytes[40]),
             static_cast<AccountId>(decode(bytes.data() + 16)),
             static_cast<AccountId>(decode(bytes.data() + 24)),
             static_cast<SessionId>(decode(bytes.data() + 32)), bytes[41] == 1};
    change = bytes[42] == 1;
    return true;
}

bool apply_change(AccountConfig *accounts, size_t count, const AuditRecord &event) noexcept {
    if (!event.accepted || event.actor == AccountId{} || event.session == SessionId{} ||
        (event.action != AuditAction::enable_account && event.action != AuditAction::disable_account)) {
        return false;
    }
    const bool enabled = event.action == AuditAction::enable_account;
    AccountConfig *target = nullptr;
    bool authorized = false;
    bool other_admin = false;
    for (size_t i = 0; i < count; ++i) {
        auto &account = accounts[i];
        if (account.id == event.target) {
            target = &account;
        } else if (account.enabled && account.role == Role::administrator) {
            other_admin = true;
        }
        if (account.id == event.actor && account.enabled && account.role == Role::administrator) {
            authorized = true;
        }
    }
    if (!authorized || target == nullptr || target->enabled == enabled ||
        (!enabled && target->role == Role::administrator && !other_admin)) {
        return false;
    }
    target->enabled = enabled;
    return true;
}

} // namespace

HostStore::~HostStore() {
    if (fd_ >= 0) {
        (void)close(fd_);
    }
}

bool HostStore::provision(const char *path, const AccountConfig *accounts, size_t count) noexcept {
    Directory validation;
    if (!validation.commission(accounts, count, 1)) {
        return false;
    }
    const File directory{open_directory(path)};
    if (directory.fd < 0) {
        return false;
    }
    const File journal{openat(directory.fd, journal_name, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
                              S_IRUSR | S_IWUSR)};
    if (journal.fd < 0 || flock(journal.fd, LOCK_EX | LOCK_NB) != 0) {
        return false;
    }
    Header bytes{};
    encode(bytes.data(), format);
    encode(bytes.data() + 8, count);
    for (size_t i = 0; i < count; ++i) {
        auto *entry = bytes.data() + 16 + i * account_bytes;
        encode(entry, static_cast<uint64_t>(accounts[i].id));
        entry[8] = static_cast<uint8_t>(accounts[i].role);
        entry[9] = accounts[i].enabled ? 1 : 0;
    }
    encode(bytes.data() + header_bytes - 8, checksum(bytes.data(), header_bytes - 8));
    return write_at(journal.fd, bytes.data(), bytes.size(), 0) && sync_journal(journal.fd) && sync(directory.fd);
}

bool HostStore::open(const char *path) noexcept {
    if (fd_ >= 0) {
        return false;
    }
    const File directory{open_directory(path)};
    if (directory.fd < 0) {
        return false;
    }
    File journal{openat(directory.fd, journal_name, O_RDWR | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK)};
    struct stat info{};
    if (journal.fd < 0 || flock(journal.fd, LOCK_EX | LOCK_NB) != 0 || fstat(journal.fd, &info) != 0 ||
        !S_ISREG(info.st_mode) || info.st_uid != geteuid() || info.st_nlink != 1 ||
        (info.st_mode & (S_IRWXG | S_IRWXO)) != 0 ||
        info.st_size < record_offset(0) || info.st_size > record_offset(record_capacity)) {
        return false;
    }
    const auto tail = static_cast<size_t>(info.st_size - record_offset(0));
    Header bytes{};
    if (tail % record_bytes != 0 || !read_at(journal.fd, bytes.data(), bytes.size(), 0) ||
        decode(bytes.data()) != format ||
        decode(bytes.data() + header_bytes - 8) != checksum(bytes.data(), header_bytes - 8)) {
        return false;
    }
    const auto count = decode(bytes.data() + 8);
    if (count == 0 || count > Directory::account_capacity) {
        return false;
    }
    AccountConfig restored[Directory::account_capacity]{};
    for (size_t i = 0; i < count; ++i) {
        const auto *entry = bytes.data() + 16 + i * account_bytes;
        if (entry[9] > 1) {
            return false;
        }
        restored[i] = {static_cast<AccountId>(decode(entry)), static_cast<Role>(entry[8]), entry[9] == 1};
    }
    Directory validation;
    if (!validation.commission(restored, count, 1)) {
        return false;
    }
    const auto records = tail / record_bytes;
    for (size_t i = 0; i < records; ++i) {
        Record record{};
        bool change{};
        AuditRecord event{};
        if (!read_at(journal.fd, record.data(), record.size(), record_offset(i)) ||
            !decode_record(record, i, change, event) || (change && !apply_change(restored, count, event))) {
            return false;
        }
    }
    // A complete last record may have survived an earlier failed sync. Establish
    // a new durable boundary before accepting requests against the recovered state.
    if (!sync_journal(journal.fd)) {
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        accounts_[i] = restored[i];
    }
    count_ = count;
    records_ = records;
    fd_ = journal.fd;
    journal.fd = -1;
    return true;
}

bool HostStore::load(AccountConfig (&accounts)[Directory::account_capacity], size_t &count) const noexcept {
    if (fd_ < 0 || failed_) {
        return false;
    }
    for (size_t i = 0; i < count_; ++i) {
        accounts[i] = accounts_[i];
    }
    count = count_;
    return true;
}

bool HostStore::read_audit(size_t index, AuditRecord &record) const noexcept {
    Record bytes{};
    bool change{};
    return fd_ >= 0 && !failed_ && index < records_ &&
           read_at(fd_, bytes.data(), bytes.size(), record_offset(index)) &&
           decode_record(bytes, index, change, record);
}

AccountStore HostStore::account_store() noexcept {
    return {this, update};
}

AuditSink HostStore::audit_sink() noexcept {
    return {this, append};
}

bool HostStore::update(void *store, AccountId account, bool enabled, const AuditRecord &event) noexcept {
    auto &self = *static_cast<HostStore *>(store);
    if (self.fd_ < 0 || self.failed_ || account != event.target ||
        event.action != (enabled ? AuditAction::enable_account : AuditAction::disable_account)) {
        return false;
    }
    AccountConfig updated[Directory::account_capacity]{};
    for (size_t i = 0; i < self.count_; ++i) {
        updated[i] = self.accounts_[i];
    }
    if (!apply_change(updated, self.count_, event) || !self.write(true, event)) {
        return false;
    }
    for (size_t i = 0; i < self.count_; ++i) {
        self.accounts_[i] = updated[i];
    }
    return true;
}

bool HostStore::append(void *store, const AuditRecord &event) noexcept {
    return static_cast<HostStore *>(store)->write(false, event);
}

bool HostStore::write(bool change, const AuditRecord &event) noexcept {
    if (fd_ < 0 || failed_ || records_ == record_capacity || !valid_event(event)) {
        return false;
    }
    Record record{};
    encode(record.data(), records_ + 1);
    encode(record.data() + 8, event.time);
    encode(record.data() + 16, static_cast<uint64_t>(event.actor));
    encode(record.data() + 24, static_cast<uint64_t>(event.target));
    encode(record.data() + 32, static_cast<uint64_t>(event.session));
    record[40] = static_cast<uint8_t>(event.action);
    record[41] = event.accepted ? 1 : 0;
    record[42] = change ? 1 : 0;
    encode(record.data() + record_checksum, checksum(record.data(), record_checksum));
    if (!write_at(fd_, record.data(), record.size(), record_offset(records_)) || !sync_journal(fd_)) {
        failed_ = true;
        return false;
    }
    ++records_;
    return true;
}

} // namespace alacre::identity
