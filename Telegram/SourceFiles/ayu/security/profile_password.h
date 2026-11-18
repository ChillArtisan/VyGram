#pragma once

#include <cstddef>

namespace Ayu::Security {

inline constexpr unsigned char kProfileSaltMarker[] = {
    'A', 'Y', 'U', 'S', 'C', 'R', '0', '1' };
inline constexpr auto kProfileSaltMarkerSize = sizeof(kProfileSaltMarker);
inline constexpr auto kProfileSaltSize = kProfileSaltMarkerSize + 32;
inline constexpr std::size_t kProfileKeySize = 256;

[[nodiscard]] bool IsScryptProfileSalt(
    const unsigned char *salt, std::size_t size) noexcept;
[[nodiscard]] bool DeriveProfilePasswordKey(
    const char *password, std::size_t passwordSize,
    const unsigned char *salt, std::size_t saltSize,
    unsigned char *output, std::size_t outputSize) noexcept;

} // namespace Ayu::Security
