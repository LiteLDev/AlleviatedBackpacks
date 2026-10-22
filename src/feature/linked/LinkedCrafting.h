#pragma once

// GX-2: SC's `ender_linker_endpoint` crafting recipe (see EnderLinkerCrafting.cpp).

#include <cstddef>
#include <optional>
#include <string_view>

class ItemStack;
class Player;

namespace abp::linked {

/// SC `EnderLinkPendingCraftPlan`.
enum class CraftPlan { CreatePrimary, BindExisting, AddSecondary };

/// SC `EnderLinkerEndpointRecipe.getPlan`: what crafting `linker` with `endpoint` does (nullopt =
/// no recipe: a bound linker with a linked endpoint).
[[nodiscard]] std::optional<CraftPlan> planFor(ItemStack const& linker, ItemStack const& endpoint);
/// SC's serialized plan name ("linker_endpoint_primary", ...).
[[nodiscard]] std::string_view planName(CraftPlan plan);
/// Crafts waiting for their linker result to show up (selftests).
[[nodiscard]] std::size_t pendingCraftClaims();
/// Applies a successful endpoint craft as the craft hook does at the end of the request
/// (selftests): `consumedLinker` and `consumedEndpoint` were in the grid (the endpoint in UI slot
/// `gridSlot`, -1 = none) and the recipe's plain linker was delivered to the player.
bool finalizeCraft(Player& player, CraftPlan plan, ItemStack const& consumedLinker, ItemStack const& consumedEndpoint, int gridSlot);

} // namespace abp::linked
