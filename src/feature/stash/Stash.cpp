#include "feature/stash/Stash.h"

#include <algorithm>

#include "mc/deps/shared_types/legacy/ContainerType.h"
#include "mc/world/PlayerUIContainer.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/containers/ContainerEnumName.h"
#include "mc/world/containers/managers/models/ContainerManagerModel.h"
#include "mc/world/containers/models/ContainerModel.h"
#include "mc/world/item/ItemStack.h"

#include "feature/gui/Gui.h"
#include "game/Backpack.h"
#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/PlayerScan.h"
#include "game/Storage.h"
#include "mod/Log.h"
#include "ui/menu/BackpackMenu.h"
#include "ui/menu/MenuManager.h"

namespace abp::feature::stash {

namespace {
using Area = Location::Area;

constexpr int kInventorySlots = 36;

/// SB stashes through `getInventoryForUpgradeProcessing` (Void and other upgrade wrappers apply,
/// like for Deposit / Pickup).
core::AccessContext access(Player* player) { return core::AccessContext{core::Source::Upgrade, player}; }

std::optional<game::ItemLocation> playerLocation(Location const& loc) {
    switch (loc.area) {
    case Area::Inventory:
        if (loc.slot < 0 || loc.slot >= kInventorySlots) return std::nullopt;
        return game::ItemLocation{game::ItemLocation::Kind::Inventory, loc.slot};
    case Area::Armor:
        if (loc.slot < 0 || loc.slot >= 4) return std::nullopt;
        return game::ItemLocation{game::ItemLocation::Kind::Armor, loc.slot};
    case Area::Offhand:
        return game::ItemLocation{game::ItemLocation::Kind::Offhand, 0};
    default:
        return std::nullopt;
    }
}

/// The open backpack screen (also nested ones), if any.
ui::BackpackMenu* openBackpackMenu(Player& player) {
    auto menu = ui::MenuManager::get().current(player);
    return menu ? dynamic_cast<ui::BackpackMenu*>(menu.get()) : nullptr;
}

/// The model of the open vanilla container's own slots (chest, barrel, shulker box, hopper, ...).
std::shared_ptr<ContainerModel> openContainer(Player& player) {
    if (ui::MenuManager::get().current(player)) return nullptr; // our screens have no vanilla container
    auto const& mgr = player.mContainerManager.get();
    if (!mgr) return nullptr;
    using T = SharedTypes::Legacy::ContainerType;
    switch (mgr->getContainerType()) {
    case T::Container:
    case T::MinecartChest:
    case T::Hopper:
    case T::MinecartHopper:
    case T::Dispenser:
    case T::Dropper:
        break;
    default:
        return nullptr;
    }
    for (auto name : {ContainerEnumName::LevelEntityContainer, ContainerEnumName::ShulkerBoxContainer}) {
        if (auto model = mgr->_getContainer(name)) return model;
    }
    return nullptr;
}

/// The player inventory slot that holds the open backpack (SB: its slot can't be picked up).
bool lockedSlot(Player& player, Location const& loc) {
    if (loc.area != Area::Inventory) return false;
    auto menu = ui::MenuManager::get().current(player);
    return menu && menu->lockedInventorySlot && *menu->lockedInventorySlot == loc.slot;
}

ItemStack cursorOf(Player& player) { return player.mPlayerUIContainer->getItem(0); }

void setCursor(Player& player, ItemStack const& item) { player.mPlayerUIContainer->setItem(0, item); }

/// Removes up to `amount` items from the location (SB `slot.safeTake`). Returns how many.
int take(Player& player, Location const& loc, int amount) {
    if (amount <= 0) return 0;
    if (auto pl = playerLocation(loc)) {
        ItemStack item = game::itemAt(player, *pl);
        if (item.isNull()) return 0;
        int n = std::min<int>(amount, item.mCount);
        if (n >= item.mCount) item = ItemStack{};
        else item.set(item.mCount - n);
        game::setItemAt(player, *pl, item);
        return n;
    }
    if (loc.area == Area::Container) {
        auto model = openContainer(player);
        if (!model || loc.slot < 0 || loc.slot >= model->getContainerSize()) return 0;
        ItemStack item = model->getItemStack(loc.slot);
        if (item.isNull()) return 0;
        int n = std::min<int>(amount, item.mCount);
        if (n >= item.mCount) item = ItemStack{};
        else item.set(item.mCount - n);
        model->setItem(loc.slot, item);
        return n;
    }
    if (loc.area == Area::Storage) {
        auto* menu = openBackpackMenu(player);
        if (!menu) return 0;
        auto& inv = menu->backpack()->inventory();
        if (loc.slot < 0 || loc.slot >= inv.size()) return 0;
        auto taken = inv.extract(loc.slot, amount, core::AccessContext{core::Source::Player, &player});
        if (taken.count > 0) menu->backpack()->markDirty();
        return taken.count;
    }
    return 0;
}

/// Puts a (changed) backpack item back where it was (it got a uuid on first use).
void putBack(Player& player, Location const& loc, ItemStack const& item) {
    if (auto pl = playerLocation(loc)) {
        game::setItemAt(player, *pl, item);
    } else if (loc.area == Area::Container) {
        if (auto model = openContainer(player)) model->setItem(loc.slot, item);
    } else if (loc.area == Area::Storage) {
        if (auto* menu = openBackpackMenu(player)) {
            auto data = game::ItemConv::toData(item);
            menu->backpack()->inventory().set(loc.slot, data);
            menu->backpack()->markDirty();
        }
    }
}

bool sameType(core::ItemStackData const& a, core::ItemStackData const& b) { return !a.empty() && !b.empty() && a.id == b.id; }

/// Inserts `item` into the backpack; returns how many went in (or were voided).
int insert(Player* player, game::Backpack& bp, core::ItemStackData const& item, bool simulate) {
    if (item.empty()) return 0;
    auto r = bp.inventory().insert(item, access(player), simulate);
    if (!simulate && (r.inserted > 0 || r.voided > 0)) bp.markDirty();
    return item.count - r.remainder.count;
}

/// SB `overrideStackedOnOther`: the backpack on the cursor takes the slot's items.
int stashSlotIntoCursor(Player& player, Location const& loc) {
    ItemStack cursor = cursorOf(player);
    if (cursor.isNull() || cursor.mCount != 1 || !game::isBackpack(cursor) || lockedSlot(player, loc)) return 0;
    auto content = itemAt(player, loc);
    if (content.empty()) return 0;
    bool changed = false;
    auto bp      = game::Storage::get().forItem(cursor, &changed);
    if (!bp) return 0;
    if (changed) setCursor(player, cursor);
    int fits = insert(&player, *bp, content, true);
    if (fits <= 0) return 0;
    int moved = 0;
    while (fits > 0) {
        int taken = take(player, loc, fits);
        if (taken <= 0) break;
        int in = insert(&player, *bp, content.withCount(taken), false);
        if (in < taken) { // should not happen (simulated first): give the rest back
            log().warn("stash: {} of {} {} did not fit after all", taken - in, taken, content.id);
            auto back = content.withCount(taken - in);
            if (loc.area == Area::Storage) {
                if (auto* menu = openBackpackMenu(player)) menu->backpack()->inventory().insert(back, core::AccessContext{core::Source::Internal});
            } else {
                // Player slots and containers: back into the slot when it is still empty, else to
                // the player (never lost).
                ItemStack s = game::ItemConv::toStack(back, std::min(back.count, 255));
                if (itemAt(player, loc).empty()) putBack(player, loc, s);
                else if (!player.add(s)) player.drop(s, false);
            }
        }
        moved += in;
        fits  -= taken;
        if (in < taken) break;
    }
    return moved;
}

/// SB `overrideOtherStackedOnMe`: the backpack in the slot takes the cursor.
int stashCursorIntoSlot(Player& player, Location const& loc) {
    ItemStack cursor = cursorOf(player);
    if (cursor.isNull() || lockedSlot(player, loc)) return 0;
    ItemStack target;
    if (auto pl = playerLocation(loc)) target = game::itemAt(player, *pl);
    else if (loc.area == Area::Container) {
        auto model = openContainer(player);
        if (!model || loc.slot < 0 || loc.slot >= model->getContainerSize()) return 0;
        target = model->getItemStack(loc.slot);
    } else if (loc.area == Area::Storage) {
        auto d = itemAt(player, loc);
        if (d.empty()) return 0;
        target = game::ItemConv::toStack(d, 1);
    }
    if (target.isNull() || target.mCount != 1 || !game::isBackpack(target)) return 0;
    bool changed = false;
    auto bp      = game::Storage::get().forItem(target, &changed);
    if (!bp) return 0;
    if (changed) putBack(player, loc, target);
    auto data  = game::ItemConv::toData(cursor);
    int  moved = insert(&player, *bp, data, false);
    if (moved <= 0) return 0;
    if (moved >= cursor.mCount) cursor = ItemStack{};
    else cursor.set(cursor.mCount - moved);
    setCursor(player, cursor);
    return moved;
}
} // namespace

std::optional<Location> parseLocation(nlohmann::json const& j) {
    if (!j.is_object()) return std::nullopt;
    auto area = j.value("area", std::string{});
    int  slot = j.contains("slot") && j["slot"].is_number_integer() ? j["slot"].get<int>() : -1;
    if (slot < 0 || slot > 4096) return std::nullopt;
    Location loc;
    loc.slot = slot;
    if (area == "inventory") loc.area = Area::Inventory;
    else if (area == "armor") loc.area = Area::Armor;
    else if (area == "offhand") loc.area = Area::Offhand;
    else if (area == "container") loc.area = Area::Container;
    else if (area == "storage") loc.area = Area::Storage;
    else return std::nullopt;
    return loc;
}

std::string areaName(Location::Area area) {
    switch (area) {
    case Area::Inventory:
        return "inventory";
    case Area::Armor:
        return "armor";
    case Area::Offhand:
        return "offhand";
    case Area::Container:
        return "container";
    case Area::Storage:
        return "storage";
    }
    return "";
}

std::string key(Location const& loc) { return areaName(loc.area) + ":" + std::to_string(loc.slot); }

core::ItemStackData itemAt(Player& player, Location const& loc) {
    if (auto pl = playerLocation(loc)) {
        auto const& s = game::itemAt(player, *pl);
        return s.isNull() ? core::ItemStackData{} : game::ItemConv::toData(s);
    }
    if (loc.area == Area::Container) {
        auto model = openContainer(player);
        if (!model || loc.slot < 0 || loc.slot >= model->getContainerSize()) return {};
        auto const& s = model->getItemStack(loc.slot);
        return s.isNull() ? core::ItemStackData{} : game::ItemConv::toData(s);
    }
    if (loc.area == Area::Storage) {
        auto* menu = openBackpackMenu(player);
        if (!menu) return {};
        auto& inv = menu->backpack()->inventory();
        if (loc.slot < 0 || loc.slot >= inv.size()) return {};
        return inv.at(loc.slot);
    }
    return {};
}

bool inOpenContainer(Player& player, std::string const& uuid) {
    if (uuid.empty()) return false;
    auto model = openContainer(player);
    if (!model) return false;
    int size = std::min(model->getContainerSize(), 256);
    for (int i = 0; i < size; ++i) {
        auto const& s = model->getItemStack(i);
        if (!s.isNull() && game::isBackpack(s) && game::backpackUuid(s) == uuid) return true;
    }
    return false;
}

Result stashable(Player* player, game::Backpack& backpack, core::ItemStackData const& item) {
    if (item.empty()) return Result::NoSpace;
    if (insert(player, backpack, item, true) <= 0) return Result::NoSpace;
    auto& inv = backpack.inventory();
    for (int i = 0; i < inv.size(); ++i) {
        if (sameType(inv.at(i), item)) return Result::MatchAndSpace;
    }
    for (auto const& m : feature::gui::memory(backpack)) {
        if (sameType(m, item)) return Result::MatchAndSpace;
    }
    return Result::Space;
}

Result stashableInto(Player* player, ItemStack const& backpackItem, core::ItemStackData const& item) {
    if (backpackItem.isNull() || !game::isBackpack(backpackItem) || item.empty()) return Result::NoSpace;
    auto uuid = game::backpackUuid(backpackItem);
    // SB: a backpack without contents uuid is assumed empty: any item can be stashed.
    if (!uuid) return Result::Space;
    auto bp = game::Storage::get().find(*uuid);
    if (!bp) return Result::Space;
    return stashable(player, *bp, item);
}

Result overlay(Player& player, Location const& loc) {
    ItemStack held = cursorOf(player);
    if (held.isNull() || lockedSlot(player, loc)) return Result::NoSpace;
    auto inSlot = itemAt(player, loc);
    if (inSlot.empty()) return Result::NoSpace;
    // SC: a backpack (count 1) in the slot first, then the backpack on the cursor.
    if (inSlot.count == 1 && game::isBackpack(inSlot)) {
        return stashableInto(&player, game::ItemConv::toStack(inSlot, 1), game::ItemConv::toData(held));
    }
    if (game::isBackpack(held)) return stashableInto(&player, held, inSlot);
    return Result::NoSpace;
}

int rightClick(Player& player, Location const& loc) {
    if (!game::Storage::get().available()) return 0;
    // Vanilla order (AbstractContainerMenu.doClick): the carried item's override first.
    int moved = stashSlotIntoCursor(player, loc);
    if (moved <= 0) moved = stashCursorIntoSlot(player, loc);
    return moved;
}

nlohmann::json query(Player& player, nlohmann::json const& body) {
    nlohmann::json results = nlohmann::json::object();
    auto const&    slots   = body.contains("slots") && body["slots"].is_array() ? body["slots"] : nlohmann::json::array();
    std::size_t    n       = 0;
    if (game::Storage::get().available() && !cursorOf(player).isNull()) {
        for (auto const& s : slots) {
            if (++n > 256) break;
            auto loc = parseLocation(s);
            if (!loc) continue;
            auto r = overlay(player, *loc);
            if (r != Result::NoSpace) results[key(*loc)] = static_cast<int>(r);
        }
    }
    return nlohmann::json{{"results", std::move(results)}};
}

} // namespace abp::feature::stash
