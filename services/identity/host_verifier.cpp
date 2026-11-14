#include "host_verifier.hpp"

#include <memory>
#include <openssl/ssl.h>
#include <openssl/x509.h>

namespace alacre::identity {
namespace {

int used_connection_index() noexcept {
    static const int index = SSL_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
    return index;
}

} // namespace

std::expected<SessionId, Error> open_tls_session(
    Directory &directory, SSL &connection, const CertificateBinding *bindings, size_t count,
    ChannelId channel, Tick now) noexcept {
    constexpr int required = SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT;
    const auto denied = [&]() -> std::expected<SessionId, Error> {
        directory.authentication_failed(now);
        return std::unexpected(Error::access_denied);
    };
    if (bindings == nullptr || count == 0 || count > Directory::account_capacity ||
        !SSL_is_server(&connection) || !SSL_is_init_finished(&connection) ||
        SSL_version(&connection) != TLS1_3_VERSION || SSL_session_reused(&connection) ||
        SSL_get_early_data_status(&connection) == SSL_EARLY_DATA_ACCEPTED ||
        (SSL_get_verify_mode(&connection) & required) != required || SSL_get_verify_callback(&connection) != nullptr ||
        SSL_get_verify_result(&connection) != X509_V_OK) {
        return denied();
    }

    const std::unique_ptr<X509, decltype(&X509_free)> certificate{SSL_get1_peer_certificate(&connection), X509_free};
    CertificateDigest fingerprint{};
    unsigned int length = 0;
    if (!certificate || X509_digest(certificate.get(), EVP_sha256(), fingerprint, &length) != 1 ||
        length != sizeof(fingerprint)) {
        return denied();
    }

    AccountId account{};
    bool matched = false;
    for (size_t i = 0; i < count; ++i) {
        if (CRYPTO_memcmp(fingerprint, bindings[i].fingerprint, sizeof(fingerprint)) == 0) {
            if (matched) {
                return denied();
            }
            matched = true;
            account = bindings[i].account;
        }
    }
    if (!matched || account == AccountId{}) {
        return denied();
    }
    const auto index = used_connection_index();
    static unsigned char used;
    if (index < 0 || SSL_get_ex_data(&connection, index) != nullptr ||
        SSL_set_ex_data(&connection, index, &used) != 1) {
        return denied();
    }
    return directory.open_session(account, channel, now);
}

} // namespace alacre::identity
