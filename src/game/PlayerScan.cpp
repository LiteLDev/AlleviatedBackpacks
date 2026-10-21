#include "game/PlayerScan.h"

#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

#include "game/BackpackItem.h"
#include "game/Storage.h"

namespace abp::game {

ItemStack const& itemAt(Player& player, ItemLocation loc) {
    switch (loc.kind) {
    case ItemLocation::Kind::Inventory: {
        auto& inv = player.getInventory();
        if (loc.slot < 0 || loc.slot >= inv.getContainerSize()) return ItemStack::EMPTY_ITEM();
        return inv.getItem(loc.slot);
    }
    case ItemLocation::Kind::Offhand:
        return player.getOffhandSlot();
    case ItemLocation::Kind::Armor:
        return player.getArmor(static_cast<SharedTypes::Legacy::ArmorSlot>(loc.slot));
    }
    return ItemStack::EMPTY_ITEM();
}

void setItemAt(Player& player, ItemLocation loc, ItemStack const& item) {
    switch (loc.kind) {
    case ItemLocation::Kind::Inventory: {
        auto& inv = player.getInventory();
        if (loc.slot < 0 || loc.slot >= inv.getContainerSize()) return;
        inv.setItem(loc.slot, item);
        player.sendInventory(false);
        break;
    }
    case ItemLocation::Kind::Offhand:
        player.setOffhandSlot(item);
        break;
    case ItemLocation::Kind::Armor:
        player.setArmor(static_cast<SharedTypes::Legacy::ArmorSlot>(loc.slot), item);
        break;
    }
}

ItemLocation selectedSlot(Player& player) { return {ItemLocation::Kind::Inventory, player.getSelectedItemSlot()}; }

std::vector<CarriedBackpack> scanBackpacks(Player& player) {
    std::vector<CarriedBackpack> out;
    auto visit = [&](ItemLocation loc) {
        auto const& stack = itemAt(player, loc);
        if (auto tier = backpackTierOf(stack)) out.push_back({loc, *tier, backpackUuid(stack)});
    };
    int size = player.getInventory().getContainerSize();
    for (int i = 0; i < size; ++i) visit({ItemLocation::Kind::Inventory, i});
    visit({ItemLocation::Kind::Offhand, 0});
    visit({ItemLocation::Kind::Armor, static_cast<int>(SharedTypes::Legacy::ArmorSlot::Torso)});
    return out;
}

namespace {
NestedBackpackProvider& nestedProvider() {
    static NestedBackpackProvider provider;
    return provider;
}
} // namespace

void setNestedBackpackProvider(NestedBackpackProvider provider) { nestedProvider() = std::move(provider); }

std::vector<std::pair<CarriedBackpack, std::shared_ptr<Backpack>>>
carriedBackpacks(Player& player, bool assignUuids, bool includeNested) {
    std::vector<std::pair<CarriedBackpack, std::shared_ptr<Backpack>>> out;
    auto&                                                              storage = Storage::get();
    if (!storage.available()) return out;
    for (auto& c : scanBackpacks(player)) {
        if (c.uuid) {
            if (auto bp = storage.find(*c.uuid)) out.emplace_back(c, std::move(bp));
        } else if (assignUuids) {
            ItemStack copy = itemAt(player, c.where);
            bool      changed = false;
            if (auto bp = storage.forItem(copy, &changed)) {
                if (changed) setItemAt(player, c.where, copy);
                c.uuid = bp->uuid();
                out.emplace_back(c, std::move(bp));
            }
        }
    }
    if (includeNested && !out.empty() && nestedProvider()) nestedProvider()(player, out);
    return out;
}

} // namespace abp::game
