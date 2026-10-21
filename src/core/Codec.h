#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace abp::core {

/// Standard base64 (RFC 4648, with padding).
[[nodiscard]] std::string                base64Encode(std::string_view bytes);
[[nodiscard]] std::optional<std::string> base64Decode(std::string_view text);

/// 64-bit FNV-1a. Stable across runs and platforms (used for persisted hashes and fast compares).
[[nodiscard]] constexpr std::uint64_t fnv1a(std::string_view bytes, std::uint64_t seed = 0xcbf29ce484222325ull) {
    std::uint64_t h = seed;
    for (unsigned char c : bytes) {
        h ^= c;
        h *= 0x100000001b3ull;
    }
    return h;
}

/// Random RFC 4122 version-4 UUID string ("xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx").
[[nodiscard]] std::string newUuid();

/// True for a canonical lowercase/uppercase 36-character UUID string.
[[nodiscard]] bool isUuid(std::string_view s);

/// Glob match with `*` (any run, including empty) and `?` (one char). Case-sensitive.
[[nodiscard]] bool globMatch(std::string_view pattern, std::string_view text);

} // namespace abp::core
