#pragma once

// SB `canBePlacedInContainerItems` for bundles (and every other storage item: Bedrock's
// `minecraft:storage_item` containers are "dynamic containers").
//
// SB's BackpackItem answers vanilla's `canFitInsideContainerItems()` with the config value, so a
// backpack goes into a bundle only when `canBePlacedInContainerItems = true` (default false). It
// then weighs what vanilla gives any unstackable item: 1/1 of a bundle (Java's BundleContents
// weight is 1/maxStackSize), which on Bedrock is the default weight 64 of a 64-weight bundle.
//
// The server checks what may go into a storage item in `DynamicContainerValidation::isItemAllowedInSlot`
// (hooked, reads the config live, so `/backpack reload` applies at once). The client decides
// before it sends the request, in `ContainerManagerController::handleMoveItemFromSlotIntoStorageItem`
// (hooked in the client build), with the rule the server announced in `BundleRulePacket`
// (companion clients; sent after the handshake and after every change).

#include <optional>

#include "mc/_HeaderOutputPredefine.h"

#include "net/Packets.h"

class ItemStackBase;

namespace abp::bundle {

/// Server -> companion client: {"allowed": bool} (the server's `canBePlacedInContainerItems`).
class BundleRulePacket : public net::JsonPacket<BundleRulePacket> {
    using JsonPacket::JsonPacket;
};

/// The server's rule: `sophisticatedbackpacks-server.json` `canBePlacedInContainerItems`.
[[nodiscard]] bool serverAllowsBackpacks();
/// The rule the connected server announced (nullopt until it did: then refused, SB's default).
[[nodiscard]] std::optional<bool> clientRule();
/// Client: the server's announcement (nullopt when leaving the world).
void setClientRule(std::optional<bool> allowed);

/// Whether `item` may go into a bundle: everything but backpacks, which only when allowed (the
/// client's view of the rule when `clientSide`).
[[nodiscard]] bool fitsBundles(ItemStackBase const& item, bool clientSide);
/// Logs a decision about a backpack ("bundle: client|server allows|refuses <id>"), at most once a
/// second per side.
void noteDecision(bool clientSide, bool allowed, ItemStackBase const& item);

} // namespace abp::bundle
