#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "core/ItemStackData.h"
#include "core/Tier.h"

class ItemStack;

namespace abp::game {

/// Namespace of every item/block the mod adds.
inline constexpr std::string_view kNamespace = "alleviatedbackpacks";

/// User-data key holding a backpack's uuid (a string tag in the item's NBT).
inline constexpr std::string_view kUuidKey = "abp_uuid";

/// "alleviatedbackpacks:<tier>_backpack"
[[nodiscard]] std::string backpackItemId(core::Tier tier);

/// Tier of a backpack item id / stack, or nullopt if it's not a backpack.
[[nodiscard]] std::optional<core::Tier> backpackTierOf(std::string_view itemId);
[[nodiscard]] std::optional<core::Tier> backpackTierOf(ItemStack const& stack);
[[nodiscard]] bool                      isBackpack(ItemStack const& stack);
[[nodiscard]] bool                      isBackpack(core::ItemStackData const& item);

/// Shulker boxes (any colour), by id.
[[nodiscard]] bool isShulkerBox(std::string_view itemId);

/// The uuid stored in the item, if any. Backpack uuids are assigned lazily on first open.
[[nodiscard]] std::optional<std::string> backpackUuid(ItemStack const& stack);
/// Same, for a converted stack (reads the identity NBT).
[[nodiscard]] std::optional<std::string> backpackUuid(core::ItemStackData const& item);
/// Writes the uuid into the item's user data (keeps other user data).
void setBackpackUuid(ItemStack& stack, std::string const& uuid);

/// A new backpack item of `tier` (optionally bound to an existing uuid).
[[nodiscard]] ItemStack makeBackpackItem(core::Tier tier, std::string const& uuid = {});

} // namespace abp::game
