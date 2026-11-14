#pragma once

#include "identity.hpp"

typedef struct ssl_st SSL;

namespace alacre::identity {

using CertificateDigest = unsigned char[32];

struct CertificateBinding {
    CertificateDigest fingerprint;
    AccountId account;
};

// Called on the transport-owned TLS connection and its kernel-assigned channel.
[[nodiscard]] std::expected<SessionId, Error> open_tls_session(
    Directory &directory, SSL &connection, const CertificateBinding *bindings, size_t count,
    ChannelId channel, Tick now) noexcept;

} // namespace alacre::identity
