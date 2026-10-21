#include "core/Codec.h"

#include <array>
#include <random>

namespace abp::core {

namespace {
constexpr std::string_view kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int decodeChar(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}
} // namespace

std::string base64Encode(std::string_view bytes) {
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 2 < bytes.size(); i += 3) {
        auto n = (std::uint32_t(std::uint8_t(bytes[i])) << 16) | (std::uint32_t(std::uint8_t(bytes[i + 1])) << 8)
               | std::uint32_t(std::uint8_t(bytes[i + 2]));
        out += kAlphabet[(n >> 18) & 63];
        out += kAlphabet[(n >> 12) & 63];
        out += kAlphabet[(n >> 6) & 63];
        out += kAlphabet[n & 63];
    }
    if (std::size_t rest = bytes.size() - i; rest > 0) {
        std::uint32_t n = std::uint32_t(std::uint8_t(bytes[i])) << 16;
        if (rest == 2) n |= std::uint32_t(std::uint8_t(bytes[i + 1])) << 8;
        out += kAlphabet[(n >> 18) & 63];
        out += kAlphabet[(n >> 12) & 63];
        out += rest == 2 ? kAlphabet[(n >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

std::optional<std::string> base64Decode(std::string_view text) {
    if (text.size() % 4 != 0) return std::nullopt;
    std::string out;
    out.reserve(text.size() / 4 * 3);
    for (std::size_t i = 0; i < text.size(); i += 4) {
        std::array<int, 4> v{};
        int                pad = 0;
        for (int k = 0; k < 4; ++k) {
            char c = text[i + k];
            if (c == '=') {
                // Padding only in the last quad, positions 2/3.
                if (i + 4 != text.size() || k < 2) return std::nullopt;
                ++pad;
                v[k] = 0;
            } else {
                if (pad) return std::nullopt;
                v[k] = decodeChar(c);
                if (v[k] < 0) return std::nullopt;
            }
        }
        std::uint32_t n = (std::uint32_t(v[0]) << 18) | (std::uint32_t(v[1]) << 12) | (std::uint32_t(v[2]) << 6)
                        | std::uint32_t(v[3]);
        out += char((n >> 16) & 0xff);
        if (pad < 2) out += char((n >> 8) & 0xff);
        if (pad < 1) out += char(n & 0xff);
    }
    return out;
}

std::string newUuid() {
    static thread_local std::mt19937_64 rng{std::random_device{}() ^ (std::uint64_t(std::random_device{}()) << 32)};
    std::array<std::uint8_t, 16>        b{};
    for (int i = 0; i < 16; i += 8) {
        auto v = rng();
        for (int k = 0; k < 8; ++k) b[i + k] = std::uint8_t(v >> (k * 8));
    }
    b[6] = std::uint8_t((b[6] & 0x0f) | 0x40);
    b[8] = std::uint8_t((b[8] & 0x3f) | 0x80);
    constexpr std::string_view hex = "0123456789abcdef";
    std::string                out;
    out.reserve(36);
    for (int i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) out += '-';
        out += hex[b[i] >> 4];
        out += hex[b[i] & 15];
    }
    return out;
}

bool isUuid(std::string_view s) {
    if (s.size() != 36) return false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-') return false;
        } else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
            return false;
        }
    }
    return true;
}

bool globMatch(std::string_view pattern, std::string_view text) {
    std::size_t p = 0, t = 0, starP = std::string_view::npos, starT = 0;
    while (t < text.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == text[t])) {
            ++p;
            ++t;
        } else if (p < pattern.size() && pattern[p] == '*') {
            starP = p++;
            starT = t;
        } else if (starP != std::string_view::npos) {
            p = starP + 1;
            t = ++starT;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

} // namespace abp::core
