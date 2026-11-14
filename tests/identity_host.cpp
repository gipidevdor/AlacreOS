#include "host_store.hpp"
#include "host_verifier.hpp"

#include <assert.h>
#include <memory>
#include <openssl/ssl.h>
#include <string>
#include <filesystem>
#include <fcntl.h>
#include <signal.h>
#include <sys/resource.h>
#include <unistd.h>

using namespace alacre::identity;

namespace {

template <class T, void (*release)(T *)>
using Handle = std::unique_ptr<T, decltype(release)>;

struct Credentials {
    Handle<EVP_PKEY, EVP_PKEY_free> key{nullptr, EVP_PKEY_free};
    Handle<X509, X509_free> certificate{X509_new(), X509_free};

    Credentials(long serial, const char *name) {
        const Handle<EVP_PKEY_CTX, EVP_PKEY_CTX_free> generator{
            EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr), EVP_PKEY_CTX_free};
        EVP_PKEY *generated = nullptr;
        assert(generator && EVP_PKEY_keygen_init(generator.get()) == 1);
        assert(EVP_PKEY_keygen(generator.get(), &generated) == 1);
        key.reset(generated);
        assert(certificate && X509_set_version(certificate.get(), 2) == 1);
        assert(ASN1_INTEGER_set(X509_get_serialNumber(certificate.get()), serial) == 1);
        assert(X509_gmtime_adj(X509_getm_notBefore(certificate.get()), -60));
        assert(X509_gmtime_adj(X509_getm_notAfter(certificate.get()), 3600));
        assert(X509_set_pubkey(certificate.get(), key.get()) == 1);
        auto *subject = X509_get_subject_name(certificate.get());
        assert(X509_NAME_add_entry_by_txt(subject, "CN", MBSTRING_ASC,
                                         reinterpret_cast<const unsigned char *>(name), -1, -1, 0) == 1);
        assert(X509_set_issuer_name(certificate.get(), subject) == 1);
        assert(X509_sign(certificate.get(), key.get(), nullptr) > 0);
    }
};

void use(SSL_CTX &context, const Credentials &credentials) {
    assert(SSL_CTX_use_certificate(&context, credentials.certificate.get()) == 1);
    assert(SSL_CTX_use_PrivateKey(&context, credentials.key.get()) == 1);
}

void trust(SSL_CTX &context, const Credentials &credentials) {
    assert(X509_STORE_add_cert(SSL_CTX_get_cert_store(&context), credentials.certificate.get()) == 1);
}

bool handshake(SSL &client, SSL &server) {
    BIO *client_transport = nullptr;
    BIO *server_transport = nullptr;
    assert(BIO_new_bio_pair(&client_transport, 0, &server_transport, 0) == 1);
    SSL_set_bio(&client, client_transport, client_transport);
    SSL_set_bio(&server, server_transport, server_transport);
    SSL_set_connect_state(&client);
    SSL_set_accept_state(&server);
    for (unsigned i = 0; i < 8; ++i) {
        for (auto *peer : {&client, &server}) {
            if (!SSL_is_init_finished(peer)) {
                const auto result = SSL_do_handshake(peer);
                const auto error = SSL_get_error(peer, result);
                if (result != 1 && error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE) {
                    return false;
                }
            }
        }
        if (SSL_is_init_finished(&client) && SSL_is_init_finished(&server)) {
            return true;
        }
    }
    return false;
}

constexpr AccountConfig roster[]{
    {AccountId{1}, Role::administrator, true}, {AccountId{2}, Role::operator_, true},
};

size_t count_audit(const HostStore &store, AuditAction action, bool accepted) {
    size_t count = 0;
    AuditRecord record{};
    for (size_t i = 0; store.read_audit(i, record); ++i) {
        if (record.action == action && record.accepted == accepted) {
            ++count;
        }
    }
    return count;
}

void authenticated_account_survives_restart(const char *path) {
    const Credentials server_key{1, "alacre-management"};
    const Credentials admin_key{2, "alacre-admin"};
    const Credentials unknown_key{3, "alacre-unmapped"};
    const Handle<SSL_CTX, SSL_CTX_free> server_context{SSL_CTX_new(TLS_server_method()), SSL_CTX_free};
    const Handle<SSL_CTX, SSL_CTX_free> client_context{SSL_CTX_new(TLS_client_method()), SSL_CTX_free};
    assert(server_context && client_context);
    use(*server_context, server_key);
    use(*client_context, admin_key);
    trust(*server_context, admin_key);
    trust(*server_context, unknown_key);
    trust(*client_context, server_key);
    SSL_CTX_set_verify(server_context.get(), SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, nullptr);
    SSL_CTX_set_verify(client_context.get(), SSL_VERIFY_PEER, nullptr);

    const Handle<SSL, SSL_free> client{SSL_new(client_context.get()), SSL_free};
    const Handle<SSL, SSL_free> server{SSL_new(server_context.get()), SSL_free};
    assert(client && server && SSL_set1_host(client.get(), "alacre-management") == 1);
    assert(handshake(*client, *server));

    CertificateBinding binding{{}, AccountId{1}};
    unsigned length = 0;
    assert(X509_digest(admin_key.certificate.get(), EVP_sha256(), binding.fingerprint, &length) == 1);
    assert(length == sizeof(binding.fingerprint));

    use(*client_context, unknown_key);
    const Handle<SSL, SSL_free> unknown_client{SSL_new(client_context.get()), SSL_free};
    const Handle<SSL, SSL_free> unknown_server{SSL_new(server_context.get()), SSL_free};
    assert(unknown_client && unknown_server && SSL_set1_host(unknown_client.get(), "alacre-management") == 1);
    assert(handshake(*unknown_client, *unknown_server));
    use(*client_context, admin_key);
    const Handle<SSL, SSL_free> restarted_client{SSL_new(client_context.get()), SSL_free};
    const Handle<SSL, SSL_free> restarted_server{SSL_new(server_context.get()), SSL_free};
    assert(restarted_client && restarted_server && SSL_set1_host(restarted_client.get(), "alacre-management") == 1);
    assert(handshake(*restarted_client, *restarted_server));

    {
        HostStore store;
        Directory directory;
        AccountConfig accounts[Directory::account_capacity]{};
        size_t count = 0;
        assert(store.open(path) && store.load(accounts, count));
        assert(directory.commission(accounts, count, 1000, store.account_store(), store.audit_sink()));
        assert(open_tls_session(directory, *client, &binding, 1, ChannelId{2}, 1) ==
               std::unexpected(Error::access_denied));
        assert(open_tls_session(directory, *unknown_server, &binding, 1, ChannelId{2}, 1) ==
               std::unexpected(Error::access_denied));
        const CertificateBinding ambiguous[]{binding, binding};
        assert(open_tls_session(directory, *server, ambiguous, 2, ChannelId{2}, 1) ==
               std::unexpected(Error::access_denied));
        const auto admin = open_tls_session(directory, *server, &binding, 1, ChannelId{1}, 2);
        assert(admin && directory.set_enabled(*admin, ChannelId{1}, AccountId{2}, false, 3));
        assert(open_tls_session(directory, *server, &binding, 1, ChannelId{2}, 3) ==
               std::unexpected(Error::access_denied));
        assert(!directory.audit_delivery_failed());
    }

    {
        HostStore store;
        Directory directory;
        AccountConfig accounts[Directory::account_capacity]{};
        size_t count = 0;
        assert(store.open(path) && store.load(accounts, count));
        assert(count == 2 && !accounts[1].enabled);
        assert(directory.commission(accounts, count, 1000, store.account_store(), store.audit_sink()));
        assert(directory.open_session(AccountId{2}, ChannelId{2}, 4) == std::unexpected(Error::access_denied));
        const auto admin = open_tls_session(directory, *restarted_server, &binding, 1, ChannelId{1}, 5);
        assert(admin);

        HostStore competitor;
        assert(!competitor.open(path));
        assert(count_audit(store, AuditAction::authentication_failure, false) == 4);
        assert(count_audit(store, AuditAction::disable_account, true) == 1);
        assert(directory.authorize(*admin, ChannelId{1}, Permission::manage_accounts, 6));
    }
}

void journal_full_preserves_state(const char *path) {
    {
        HostStore store;
        Directory directory;
        AccountConfig accounts[Directory::account_capacity]{};
        size_t count = 0;
        assert(store.open(path) && store.load(accounts, count));
        assert(directory.commission(accounts, count, 1000, store.account_store(), store.audit_sink()));
        const auto admin = directory.open_session(AccountId{1}, ChannelId{1}, 1);
        assert(admin);
        for (size_t i = 1; i < HostStore::record_capacity; ++i) {
            directory.authentication_failed(1);
        }
        assert(!directory.audit_delivery_failed());
        assert(directory.set_enabled(*admin, ChannelId{1}, AccountId{2}, false, 2) ==
               std::unexpected(Error::storage_unavailable));
        assert(directory.authorize(*admin, ChannelId{1}, Permission::manage_accounts, 3) ==
               std::unexpected(Error::storage_unavailable));
        assert(directory.audit_delivery_failed());
    }
    HostStore recovered;
    AccountConfig accounts[Directory::account_capacity]{};
    size_t count = 0;
    assert(recovered.open(path) && recovered.load(accounts, count));
    assert(count == 2 && accounts[1].enabled);
    assert(count_audit(recovered, AuditAction::disable_account, true) == 0);
    assert(count_audit(recovered, AuditAction::authentication_failure, false) == HostStore::record_capacity - 1);
}

void journal_rejects_failed_write(const char *path) {
    const auto file = std::filesystem::path(path) / "identity.journal";
    {
        HostStore store;
        Directory directory;
        AccountConfig accounts[Directory::account_capacity]{};
        size_t count = 0;
        assert(store.open(path) && store.load(accounts, count));
        assert(directory.commission(accounts, count, 1000, store.account_store(), store.audit_sink()));
        const auto admin = directory.open_session(AccountId{1}, ChannelId{1}, 0);
        assert(admin);

        rlimit previous{};
        assert(getrlimit(RLIMIT_FSIZE, &previous) == 0);
        rlimit limit{std::filesystem::file_size(file) + 1, previous.rlim_max};
        const auto handler = signal(SIGXFSZ, SIG_IGN);
        assert(handler != SIG_ERR && setrlimit(RLIMIT_FSIZE, &limit) == 0);
        assert(directory.set_enabled(*admin, ChannelId{1}, AccountId{2}, false, 1) ==
               std::unexpected(Error::storage_unavailable));
        assert(setrlimit(RLIMIT_FSIZE, &previous) == 0 && signal(SIGXFSZ, handler) != SIG_ERR);
        assert(directory.audit_delivery_failed());
        assert(directory.authorize(*admin, ChannelId{1}, Permission::manage_accounts, 2) ==
               std::unexpected(Error::storage_unavailable));
        assert(!store.load(accounts, count));
    }
    HostStore recovered;
    assert(!recovered.open(path));
}

void rejects_corrupt_journal(const char *path) {
    {
        HostStore store;
        assert(store.open(path));
        const auto sink = store.audit_sink();
        const AuditRecord record{1, AuditAction::authentication_failure, {}, {}, {}, false};
        assert(sink.append(sink.context, record));
    }
    const auto file = std::filesystem::path(path) / "identity.journal";
    const auto fd = open(file.c_str(), O_RDWR);
    assert(fd >= 0);
    const auto last = static_cast<off_t>(std::filesystem::file_size(file) - 1);
    unsigned char byte{};
    assert(pread(fd, &byte, 1, last) == 1);
    byte ^= 1;
    assert(pwrite(fd, &byte, 1, last) == 1 && close(fd) == 0);
    HostStore corrupt;
    assert(!corrupt.open(path));
}

} // namespace

int main(int argc, char **argv) {
    assert(argc == 2);
    const std::filesystem::path root{argv[1]};
    const auto create = [&](const char *name) {
        const auto path = root / name;
        assert(std::filesystem::create_directory(path));
        std::filesystem::permissions(path, std::filesystem::perms::owner_all);
        const auto value = path.string();
        assert(HostStore::provision(value.c_str(), roster, 2));
        assert(!HostStore::provision(value.c_str(), roster, 2));
        return value;
    };
    authenticated_account_survives_restart(create("restart").c_str());
    journal_full_preserves_state(create("full").c_str());
    journal_rejects_failed_write(create("partial").c_str());
    rejects_corrupt_journal(create("checksum").c_str());
}
