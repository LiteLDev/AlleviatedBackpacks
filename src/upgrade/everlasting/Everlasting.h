#pragma once

#include <string_view>

class ItemActor;
class ItemStack;

namespace abp::upgrade::everlasting {

inline constexpr std::string_view kId = "everlasting";

/// `config/everlasting.json`.
struct EverlastingConfig {
    int version = 1;
    /// Ticks between scans for newly dropped everlasting backpacks (found ones tick every tick).
    int checkInterval = 5;
};

/// True if `stack` is a backpack whose storage has an Everlasting upgrade installed.
[[nodiscard]] bool isEverlasting(ItemStack const& stack);

} // namespace abp::upgrade::everlasting
