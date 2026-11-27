//===--- Sha256.h - Digest of the produced executable ---------------------===//
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace apack {

/// SHA-256, FIPS 180-4.
///
/// Written here rather than taken from the platform because a checksum that only exists on
/// one platform is not a checksum: the point of publishing one is that anyone can recompute
/// it with whatever they have. It is also small enough to keep beside its only caller.
class Sha256 {
public:
    Sha256();

    void update(const void* data, std::size_t size);

    /// Finishes the digest and returns it as lowercase hex.
    ///
    /// The object is left finalised: calling `update` afterwards is a programming error and
    /// will not produce the digest of the concatenated input.
    [[nodiscard]] std::string hexDigest();

private:
    void compress(const unsigned char* block);

    std::uint32_t m_state[8];
    std::uint64_t m_bitCount = 0;
    unsigned char m_buffer[64] = {};
    std::size_t m_bufferSize = 0;
    bool m_finished = false;
};

/// Digest of a file, as lowercase hex. Returns an empty string with `error` set on failure.
std::string fileDigest(const std::string& path, std::string& error);

} // namespace apack
