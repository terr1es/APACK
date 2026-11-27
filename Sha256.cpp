//===--- Sha256.cpp - Digest of the produced executable ------------------===//
#include "Sha256.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <vector>

namespace apack {
namespace {

constexpr std::uint32_t kRound[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
    0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
    0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
    0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
    0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
    0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
    0xc67178f2,
};

inline std::uint32_t rotateRight(std::uint32_t value, int bits) {
    return (value >> bits) | (value << (32 - bits));
}

} // namespace

Sha256::Sha256() {
    m_state[0] = 0x6a09e667;
    m_state[1] = 0xbb67ae85;
    m_state[2] = 0x3c6ef372;
    m_state[3] = 0xa54ff53a;
    m_state[4] = 0x510e527f;
    m_state[5] = 0x9b05688c;
    m_state[6] = 0x1f83d9ab;
    m_state[7] = 0x5be0cd19;
}

void Sha256::compress(const unsigned char* block) {
    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) |
               (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
               (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
               static_cast<std::uint32_t>(block[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
        const std::uint32_t s0 =
            rotateRight(w[i - 15], 7) ^ rotateRight(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const std::uint32_t s1 =
            rotateRight(w[i - 2], 17) ^ rotateRight(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    std::uint32_t a = m_state[0];
    std::uint32_t b = m_state[1];
    std::uint32_t c = m_state[2];
    std::uint32_t d = m_state[3];
    std::uint32_t e = m_state[4];
    std::uint32_t f = m_state[5];
    std::uint32_t g = m_state[6];
    std::uint32_t h = m_state[7];

    for (int i = 0; i < 64; ++i) {
        const std::uint32_t s1 = rotateRight(e, 6) ^ rotateRight(e, 11) ^ rotateRight(e, 25);
        const std::uint32_t choose = (e & f) ^ (~e & g);
        const std::uint32_t temp1 = h + s1 + choose + kRound[i] + w[i];
        const std::uint32_t s0 = rotateRight(a, 2) ^ rotateRight(a, 13) ^ rotateRight(a, 22);
        const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t temp2 = s0 + majority;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    m_state[0] += a;
    m_state[1] += b;
    m_state[2] += c;
    m_state[3] += d;
    m_state[4] += e;
    m_state[5] += f;
    m_state[6] += g;
    m_state[7] += h;
}

void Sha256::update(const void* data, std::size_t size) {
    if (m_finished || size == 0) {
        return;
    }
    const auto* bytes = static_cast<const unsigned char*>(data);
    m_bitCount += static_cast<std::uint64_t>(size) * 8;

    // Fill the buffer first: a digest of several small writes has to equal the digest of
    // the same bytes written at once, and only buffering makes that true.
    while (size > 0) {
        const std::size_t take = std::min(sizeof(m_buffer) - m_bufferSize, size);
        std::memcpy(m_buffer + m_bufferSize, bytes, take);
        m_bufferSize += take;
        bytes += take;
        size -= take;
        if (m_bufferSize == sizeof(m_buffer)) {
            compress(m_buffer);
            m_bufferSize = 0;
        }
    }
}

std::string Sha256::hexDigest() {
    if (!m_finished) {
        // Padding: a single 1 bit, then zeros, then the length in bits as a big-endian
        // 64-bit value, so that the total is a multiple of 64 bytes.
        const std::uint64_t bits = m_bitCount;
        unsigned char pad = 0x80;
        update(&pad, 1);
        unsigned char zero = 0;
        while (m_bufferSize != 56) {
            update(&zero, 1);
        }
        unsigned char length[8];
        for (int i = 0; i < 8; ++i) {
            length[i] = static_cast<unsigned char>((bits >> (56 - 8 * i)) & 0xFF);
        }
        // The length bytes must not count towards the message length.
        m_bitCount = bits;
        update(length, 8);
        m_finished = true;
    }

    static const char* kHex = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (std::uint32_t word : m_state) {
        for (int shift = 28; shift >= 0; shift -= 4) {
            out.push_back(kHex[(word >> shift) & 0xF]);
        }
    }
    return out;
}

std::string fileDigest(const std::string& path, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot open '" + path + "'";
        return {};
    }
    Sha256 digest;
    std::vector<char> buffer(64 * 1024);
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize got = in.gcount();
        if (got > 0) {
            digest.update(buffer.data(), static_cast<std::size_t>(got));
        }
    }
    if (in.bad()) {
        error = "cannot read '" + path + "'";
        return {};
    }
    return digest.hexDigest();
}

} // namespace apack
