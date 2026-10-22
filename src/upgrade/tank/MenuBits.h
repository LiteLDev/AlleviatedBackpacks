#pragma once

// Helpers shared by the Tank and Battery upgrades: item slots kept in an upgrade's settings (they
// travel with the upgrade item, like SB's component item handlers) and the small server menu that
// exposes them to the native tab (`menuClick` through the tab bridge, see net/NativeScreens.cpp).

#include <algorithm>
#include <functional>
#include <memory>
#include <string>

#include "mc/world/PlayerUIContainer.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/player/PlayerUISlot.h"
#include "mc/world/item/ItemStack.h"

#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/PlayerScan.h"
#include "ui/menu/Menu.h"
#include "ui/menu/MenuManager.h"
#include "ui/menu/Slot.h"
#include "upgrade/IUpgrade.h"

namespace abp::upgrade::fluidui {

/// The item on the player's cursor (the open screen's carried item).
inline ItemStack cursorOf(Player& player) { return player.mPlayerUIContainer->getItem(0); }

/// Replaces the cursor item and syncs the inventory and cursor to the client.
inline void setCursor(Player& player, ItemStack item) {
    if (item.isNull() || item.mCount == 0) item = ItemStack{};
    else item.serverInitNetId();
    player.setPlayerUIItem(PlayerUISlot::CursorSelected, item, false);
    ui::MenuManager::get().resyncPlayer(player);
}

/// The item stored under `key` in the upgrade's settings.
inline core::ItemStackData slotItem(UpgradeContext const& ctx, std::string_view key) {
    auto const& s = ctx.settings();
    if (!s.is_object()) return {};
    auto it = s.find(key);
    if (it == s.end() || !it->is_object()) return {};
    return core::ItemStackData::fromJson(*it);
}

inline void setSlotItem(UpgradeContext const& ctx, std::string_view key, core::ItemStackData const& item) {
    auto& s = ctx.settings();
    if (!s.is_object()) s = nlohmann::json::object();
    if (item.empty()) s.erase(std::string(key));
    else s[std::string(key)] = item.toJson();
    ctx.saveSettings();
}

/// ComponentItemHandler.insertItem into a settings slot: merges into the same item up to
/// min(`limit`, max stack). Returns what is left (count 0 = everything went in).
inline int insertCount(UpgradeContext const& ctx, std::string_view key, core::ItemStackData const& item, int limit, bool simulate) {
    if (item.empty()) return 0;
    auto cur  = slotItem(ctx, key);
    int  cap  = std::max(1, std::min(limit, std::max(1, item.maxStack)));
    if (!cur.empty() && !cur.sameItem(item)) return item.count;
    int have = cur.empty() ? 0 : cur.count;
    int n    = std::clamp(cap - have, 0, item.count);
    if (n > 0 && !simulate) setSlotItem(ctx, key, cur.empty() ? item.withCount(n) : cur.withCount(have + n));
    return item.count - n;
}

/// A menu slot showing a settings slot. `valid` decides what may be put in (empty = take only);
/// `limit` caps the count (the item's max stack size applies too).
class SettingsItemSlot : public ui::Slot {
public:
    using Valid = std::function<bool(core::ItemStackData const&)>;
    using Alive = std::function<bool()>;
    SettingsItemSlot(UpgradeContext ctx, std::string key, Valid valid, int limit, Alive alive)
    : mCtx(std::move(ctx)),
      mKey(std::move(key)),
      mValid(std::move(valid)),
      mLimit(limit),
      mAlive(std::move(alive)) {}

    [[nodiscard]] ItemStack display(Player&) const override {
        if (!alive()) return {};
        auto c = slotItem(mCtx, mKey);
        return c.empty() ? ItemStack{} : game::ItemConv::toStack(c, std::min(c.count, 255));
    }

    ItemStack take(Player&, int amount) override {
        if (!alive()) return {};
        auto c = slotItem(mCtx, mKey);
        if (c.empty() || amount <= 0) return {};
        int  n   = std::min({amount, c.count, 255});
        auto out = game::ItemConv::toStack(c, n);
        c.count -= n;
        setSlotItem(mCtx, mKey, c.count > 0 ? c : core::ItemStackData{});
        mTaken = c.withCount(n);
        return out;
    }

    int accept(Player&, ItemStack const& stack) override {
        if (!alive() || stack.isNull()) return 0;
        auto d = game::ItemConv::toData(stack);
        // The framework may hand back exactly what it just took (a failed swap): allowed even
        // where the item would no longer be valid; anything else must pass `valid`.
        bool handBack = !mTaken.empty() && mTaken.sameItem(d) && d.count <= mTaken.count;
        mTaken.clear();
        if (!handBack && (!mValid || !mValid(d))) return 0;
        return d.count - insertCount(mCtx, mKey, d, mLimit, false);
    }

private:
    [[nodiscard]] bool alive() const { return !mAlive || mAlive(); }

    UpgradeContext      mCtx;
    std::string         mKey;
    Valid               mValid;
    int                 mLimit;
    Alive               mAlive;
    core::ItemStackData mTaken; ///< what the last take() removed (may be handed back once)
};

/// True if `item` is the backpack item of `ctx`'s backpack (it must never go into its own slots).
inline bool isOwnBackpack(UpgradeContext const& ctx, core::ItemStackData const& item) {
    if (item.empty() || !game::isBackpack(item)) return false;
    auto uuid = game::backpackUuid(item);
    return uuid && *uuid == ctx.backpack().uuid();
}

/// Locks the backpack item (when carried in the main inventory) while `menu` is open, like the
/// backpack GUI does.
inline void lockBackpackItem(ui::Menu& menu, Player& player, game::Backpack const& backpack) {
    for (auto const& c : game::scanBackpacks(player)) {
        if (c.uuid && *c.uuid == backpack.uuid() && c.where.kind == game::ItemLocation::Kind::Inventory) {
            menu.lockedInventorySlot = c.where.slot;
            return;
        }
    }
}

/// A small menu holding the upgrade's item slots (slot i = `slots[i]`), used as the tab's server
/// menu and as the chest view in selftests.
class ItemSlotsMenu : public ui::Menu {
public:
    using Factory = std::function<std::vector<std::shared_ptr<ui::Slot>>()>;
    ItemSlotsMenu(std::string title, UpgradeContext ctx, Factory slots, std::function<bool()> alive)
    : Menu(std::move(title), Rows::Three),
      mCtx(std::move(ctx)),
      mSlots(std::move(slots)),
      mAlive(std::move(alive)) {}

    void onOpen(Player& player) override { lockBackpackItem(*this, player, mCtx.backpack()); }

    void build(Player&) override {
        auto slots = mSlots();
        for (int i = 0; i < size(); ++i) {
            if (i < static_cast<int>(slots.size())) setSlot(i, slots[static_cast<std::size_t>(i)]);
            else setSlot(i, std::make_shared<ui::StaticSlot>(ui::icon("minecraft:gray_stained_glass_pane", " ")));
        }
    }
    void onTick(Player& player) override {
        if (mAlive && !mAlive()) ui::MenuManager::get().close(player);
    }

private:
    UpgradeContext        mCtx;
    Factory               mSlots;
    std::function<bool()> mAlive;
};

} // namespace abp::upgrade::fluidui
