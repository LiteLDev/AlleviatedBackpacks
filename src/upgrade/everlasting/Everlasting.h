#pragma once

#include <string_view>

class ItemActor;
class ItemStack;

namespace abp::upgrade::everlasting {

inline constexpr std::string_view kId = "everlasting";

/// `config/everlasting.json`.
struct EverlastingConfig {
    int version = 1;
    /// Ticks between the despawn / void checks of dropped everlasting backpacks.
    int checkInterval = 5;
    /// Horizontal search radius for solid ground when rescuing a backpack from the void. Without
    /// ground nearby the backpack hovers (no gravity) just above the bottom of the world.
    int rescueRadius = 8;
};

/// True if `stack` is a backpack whose storage has an Everlasting upgrade installed.
[[nodiscard]] bool isEverlasting(ItemStack const& stack);

/// Moves a dropped backpack that fell out of the world back to a safe spot. Returns false if the
/// actor isn't an everlasting backpack.
bool rescueFromVoid(ItemActor& actor);

} // namespace abp::upgrade::everlasting
