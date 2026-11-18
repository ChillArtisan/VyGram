#include "ayu/security/profile_password.h"
#include <array>
#include <cassert>
#include <cstring>

int main() {
    using namespace Ayu::Security;
    std::array<unsigned char, kProfileSaltSize> salt{};
    std::memcpy(salt.data(), kProfileSaltMarker, kProfileSaltMarkerSize);
    for (auto i = 0; i != 32; ++i) salt[kProfileSaltMarkerSize + i] = i;
    std::array<unsigned char, kProfileKeySize> first{}, second{}, wrong{};
    const char password[] = "correct horse battery staple";
    assert(IsScryptProfileSalt(salt.data(), salt.size()));
    assert(DeriveProfilePasswordKey(password, sizeof(password) - 1,
        salt.data(), salt.size(), first.data(), first.size()));
    const unsigned char expected[] = {
        0xfb,0xb1,0x8f,0xb6,0xdc,0x3d,0xee,0xd0,
        0x9c,0x2a,0x88,0x17,0xbc,0x9a,0x0b,0x6e,
        0xca,0x46,0x37,0x06,0x58,0x1f,0x70,0xff,
        0x5e,0x47,0xb8,0xf2,0xcf,0x4a,0x51,0xe6};
    assert(std::memcmp(first.data(), expected, sizeof(expected)) == 0);
    assert(DeriveProfilePasswordKey(password, sizeof(password) - 1,
        salt.data(), salt.size(), second.data(), second.size()));
    assert(first == second);
    assert(DeriveProfilePasswordKey("different passphrase", 20,
        salt.data(), salt.size(), wrong.data(), wrong.size()));
    assert(first != wrong);
    salt[0] ^= 1;
    assert(!IsScryptProfileSalt(salt.data(), salt.size()));
    assert(!DeriveProfilePasswordKey(password, sizeof(password) - 1,
        salt.data(), salt.size(), wrong.data(), wrong.size()));
    assert(!IsScryptProfileSalt(nullptr, kProfileSaltSize));
    assert(!DeriveProfilePasswordKey(nullptr, 1, salt.data(), salt.size(),
        wrong.data(), wrong.size()));
}
