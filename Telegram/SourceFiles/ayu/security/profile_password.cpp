#include "ayu/security/profile_password.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <cstring>

namespace Ayu::Security {

bool IsScryptProfileSalt(
        const unsigned char *salt, std::size_t size) noexcept {
    return salt && size == kProfileSaltSize
        && !std::memcmp(salt, kProfileSaltMarker, kProfileSaltMarkerSize);
}

bool DeriveProfilePasswordKey(
        const char *password, std::size_t passwordSize,
        const unsigned char *salt, std::size_t saltSize,
        unsigned char *output, std::size_t outputSize) noexcept {
    if (!password || !output || outputSize != kProfileKeySize
        || !IsScryptProfileSalt(salt, saltSize)) {
        return false;
    }
    // Fixed, versioned parameters: 64 MiB of working memory. Never accept
    // resource parameters from an untrusted file header.
    const auto ok = EVP_PBE_scrypt(
        password, passwordSize, salt + kProfileSaltMarkerSize, 32,
        65536, 8, 1, 128 * 1024 * 1024, output, outputSize) == 1;
    if (!ok) {
        OPENSSL_cleanse(output, outputSize);
    }
    return ok;
}

} // namespace Ayu::Security
