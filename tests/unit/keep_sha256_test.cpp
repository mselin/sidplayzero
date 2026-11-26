// SPDX-License-Identifier: GPL-3.0-or-later
// SPEC-241: the SHA-256 that `render_hash_accurate` and `render_hash_light` are, against the known values of
// FIPS 180-4's examples and NIST's test vectors.
#include <cstdint>
#include <string>

#include "doctest.h"
#include "spz/sha256.h"

namespace {

spz::Bytes of(const std::string& text) { return spz::Bytes(text.begin(), text.end()); }

}  // namespace

TEST_CASE("SPEC-241 the SHA-256 of the render hashes gives the known values of FIPS 180-4's examples") {
    CHECK(spz::sha256_hex(of("")) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(spz::sha256_hex(of("abc")) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(spz::sha256_hex(of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")) ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    CHECK(spz::sha256_hex(of("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu")) ==
          "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1");
    // A million times "a": many blocks, and a length past what fits in one byte of the padding.
    CHECK(spz::sha256_hex(of(std::string(1000000, 'a'))) ==
          "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE("SPEC-241 the SHA-256 is right at each length around the edges of a block") {
    // 55 bytes is the most whose padding fits in their own block; 56 and 64 need a second.
    CHECK(spz::sha256_hex(of(std::string(55, 'a'))) == "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318");
    CHECK(spz::sha256_hex(of(std::string(56, 'a'))) == "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a");
    CHECK(spz::sha256_hex(of(std::string(64, 'a'))) == "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");
    // Every byte value, so that the bytes over 0x7F are read as unsigned.
    spz::Bytes every(256);
    for (int i = 0; i < 256; ++i) every[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i);
    CHECK(spz::sha256_hex(every) == "40aff2e9d2d8922e47afd4648e6967497158785fbd1da870e7110266bf944880");
}
