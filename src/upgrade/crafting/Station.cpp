#include "upgrade/crafting/Station.h"

#include <algorithm>
#include <climits>
#include <unordered_map>

#include "mc/world/PlayerUIContainer.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

#include "feature/gui/Gui.h"
#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "mod/Config.h"
#include "mod/GeneralConfig.h"
#include "net/NativeScreens.h"
#include "ui/menu/BackpackMenu.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"

namespace abp::upgrade::crafting {

namespace {
constexpr int kInventorySlots = 36;

using Where      = ui::MenuManager::SlotAddress::Where;
using ActionType = ui::MenuManager::ActionType;

ui::MenuManager::SimAction act(ActionType type, ui::MenuManager::SlotAddress src, ui::MenuManager::SlotAddress dst, int amount) {
    ui::MenuManager::SimAction a;
    a.type   = type;
    a.src    = src;
    a.dst    = dst;
    a.amount = amount;
    return a;
}
} // namespace

// ---- items --------------------------------------------------------------------------------------

ItemStack cursorOf(Player& player) { return player.mPlayerUIContainer->getItem(0); }

int findUpgradeSlot(game::Backpack const& backpack, std::string_view upgradeId) {
    auto const& slots = backpack.data().upgrades;
    for (int i = 0; i < static_cast<int>(slots.size()); ++i) {
        auto const& s = slots[static_cast<std::size_t>(i)];
        if (s.empty()) continue;
        if (auto* u = upgradeFor(s.item); u && u->info().id == upgradeId) return i;
    }
    return -1;
}

int playerRoomFor(Player& player, ItemStack const& stack) {
    if (stack.isNull()) return 0;
    int   max  = std::max<int>(stack.getMaxStackSize(), 1);
    int   room = 0;
    auto& inv  = player.getInventory();
    int   n    = std::min(inv.getContainerSize(), kInventorySlots);
    for (int i = 0; i < n; ++i) {
        auto const& it = inv.getItem(i);
        if (it.isNull() || it.mCount == 0) room += max;
        else if (it.isStackable(stack)) room += std::max(0, max - static_cast<int>(it.mCount));
    }
    return room;
}

int addToPlayer(Player& player, ItemStack const& stack, int count) {
    if (stack.isNull()) return count;
    int   max     = std::max<int>(stack.getMaxStackSize(), 1);
    auto& inv     = player.getInventory();
    int   n       = std::min(inv.getContainerSize(), kInventorySlots);
    bool  changed = false;
    auto  put     = [&](int slot, ItemStack item) {
        item.serverInitNetId();
        inv.setItem(slot, item);
        changed = true;
    };
    // Vanilla Inventory.add: merge into matching stacks first, then fill empty slots.
    for (int i = 0; i < n && count > 0; ++i) {
        auto const& it = inv.getItem(i);
        if (it.isNull() || it.mCount == 0 || !it.isStackable(stack) || it.mCount >= max) continue;
        int       add  = std::min(count, max - static_cast<int>(it.mCount));
        ItemStack copy = it;
        copy.set(it.mCount + add);
        put(i, copy);
        count -= add;
    }
    for (int i = 0; i < n && count > 0; ++i) {
        auto const& it = inv.getItem(i);
        if (!it.isNull() && it.mCount > 0) continue;
        int       add  = std::min(count, max);
        ItemStack copy = stack;
        copy.set(add);
        put(i, copy);
        count -= add;
    }
    if (changed) player.sendInventory(false);
    return count;
}

core::ItemStackData moveOut(Player& player, game::Backpack& backpack, core::ItemStackData item, bool storageFirst) {
    auto toStorage = [&] {
        if (item.empty()) return;
        auto r = backpack.inventory().insert(item, core::AccessContext{core::Source::Player, &player});
        item   = r.remainder;
    };
    auto toPlayer = [&] {
        if (item.empty()) return;
        int left = addToPlayer(player, game::ItemConv::toStack(item, 1), item.count);
        item     = item.withCount(left);
    };
    if (storageFirst) {
        toStorage();
        toPlayer();
    } else {
        toPlayer();
        toStorage();
    }
    return item;
}

void dropAt(Player& player, core::ItemStackData const& item) {
    if (item.empty()) return;
    int left = item.count;
    int max  = std::max(item.maxStack, 1);
    while (left > 0) {
        int chunk = std::min({left, max, 255});
        player.drop(game::ItemConv::toStack(item, chunk), false);
        left -= chunk;
    }
}

core::ItemStackData giveBack(Player* player, game::Backpack& backpack, core::ItemStackData item) {
    if (item.empty()) return {};
    auto r = backpack.inventory().insert(item, core::AccessContext{core::Source::Internal, player});
    item   = r.remainder;
    if (item.empty() || !player) return item;
    int left = addToPlayer(*player, game::ItemConv::toStack(item, 1), item.count);
    dropAt(*player, item.withCount(left));
    return {};
}

bool extractFromStorageOrPlayer(Player& player, game::Backpack& backpack, core::ItemStackData const& like, int count) {
    if (like.empty() || count <= 0) return false;
    core::AccessContext access{core::Source::Upgrade, &player};
    auto&               inv  = backpack.inventory();
    auto                same = [&](core::ItemStackData const& d) { return d.sameItem(like); };
    if (inv.extractMatching(same, count, access, true).count >= count) {
        (void)inv.extractMatching(same, count, access, false);
        return true;
    }
    // SC Inventory.findSlotMatchingItem + removeItem.
    auto& pinv = player.getInventory();
    for (int i = 0; i < kInventorySlots && i < pinv.getContainerSize(); ++i) {
        auto const& it = pinv.getItem(i);
        if (it.isNull() || it.mCount < count || !game::ItemConv::toData(it).sameItem(like)) continue;
        ItemStack copy = it;
        copy.set(it.mCount - count);
        if (copy.mCount == 0) copy = ItemStack::EMPTY_ITEM();
        else copy.serverInitNetId();
        pinv.setItem(i, copy);
        player.sendInventory(false);
        return true;
    }
    return false;
}

bool allowedInUpgradeSlot(game::Backpack const& backpack, core::ItemStackData const& item) {
    if (item.empty()) return true;
    if (game::isBackpack(item)) return false;
    if (game::isShulkerBox(item.id)) {
        return config::get<GeneralConfig>().allowShulkerBoxes || backpack.hasCapability("allow_shulker_boxes");
    }
    return true;
}

// ---- StateSlot ----------------------------------------------------------------------------------

ItemStack StateSlot::display(Player&) const {
    if (!alive()) return {};
    auto d = mOps.get();
    return d.empty() ? ItemStack{} : game::ItemConv::toStack(d, std::min(d.count, 255));
}

bool StateSlot::click(ui::ClickContext const& c) {
    if (!alive()) return true;
    if ((c.type == ui::ClickType::Place || c.type == ui::ClickType::Swap) && !c.offered.isNull() && mOps.mayPlace) {
        if (!mOps.mayPlace(game::ItemConv::toData(c.offered))) return true; // refused: nothing moves
    }
    return false;
}

ItemStack StateSlot::take(Player&, int amount) {
    if (!alive()) return {};
    auto d = mOps.get();
    if (d.empty()) return {};
    int n = std::min({amount, d.count, 255});
    if (n <= 0) return {};
    mOps.set(d.withCount(d.count - n));
    if (!mTaken.empty() && mTaken.sameItem(d)) mTaken.count += n;
    else mTaken = d.withCount(n);
    return game::ItemConv::toStack(d, n);
}

int StateSlot::accept(Player&, ItemStack const& stack) {
    if (!alive() || stack.isNull()) return 0;
    auto offered = game::ItemConv::toData(stack);
    // Items just taken from this slot always fit back (rolled-back moves never lose items).
    bool restoring = !mTaken.empty() && mTaken.sameItem(offered) && offered.count <= mTaken.count;
    if (!restoring && mOps.mayPlace && !mOps.mayPlace(offered)) return 0;
    auto cur = mOps.get();
    if (!cur.empty() && !cur.sameItem(offered)) return 0;
    int have = cur.empty() ? 0 : cur.count;
    int max  = mOps.maxCount ? mOps.maxCount(offered) : std::max(offered.maxStack, 1);
    int room = restoring ? offered.count : max - have;
    int n    = std::min(room, offered.count);
    if (n <= 0) return 0;
    mOps.set(offered.withCount(have + n));
    if (restoring) mTaken = mTaken.withCount(mTaken.count - n);
    return n;
}

// ---- ResultSlot ---------------------------------------------------------------------------------

ItemStack ResultSlot::display(Player&) const {
    if ((mOps.valid && !mOps.valid()) || !mOps.preview) return {};
    return mOps.preview();
}

bool ResultSlot::click(ui::ClickContext const& c) {
    if (mOps.valid && !mOps.valid()) return true;
    switch (c.type) {
    case ui::ClickType::Place:
        return true; // nothing goes into a result slot
    case ui::ClickType::Swap:
        return !c.offered.isNull();
    case ui::ClickType::Take: {
        // Vanilla Slot.tryRemove: a result is only picked up when all of it fits on the cursor.
        auto      preview = mOps.preview ? mOps.preview() : ItemStack{};
        ItemStack cursor  = cursorOf(c.player);
        if (preview.isNull()) return true;
        if (!cursor.isNull() && cursor.mCount > 0
            && (!cursor.isStackable(preview) || cursor.mCount + preview.mCount > cursor.getMaxStackSize())) {
            return true;
        }
        return false;
    }
    default:
        return false;
    }
}

ItemStack ResultSlot::take(Player& player, int) {
    if ((mOps.valid && !mOps.valid()) || !mOps.craftOnce) return {};
    return mOps.craftOnce(player);
}

int ResultSlot::accept(Player& player, ItemStack const& stack) {
    if (stack.isNull()) return 0;
    (void)giveBack(&player, mBackpack, game::ItemConv::toData(stack));
    return stack.mCount;
}

// ---- clicks -------------------------------------------------------------------------------------

bool clickSlot(Player& player, std::shared_ptr<ui::Slot> slot, int button, bool output) {
    if (!slot) return false;
    auto&                    mm     = ui::MenuManager::get();
    ItemStack                shown  = slot->display(player);
    ItemStack                cursor = cursorOf(player);
    ui::MenuManager::SlotAddress here{Where::Menu, 0};
    ui::MenuManager::SlotAddress cur{Where::Cursor, 0};
    bool                     ok = false;
    if (cursor.isNull()) {
        if (shown.isNull()) return false;
        int have   = shown.mCount;
        int amount = button == 1 ? (have + 1) / 2 : have;
        ok         = mm.simulateOn(player, act(ActionType::Take, here, cur, amount), slot);
    } else if (shown.isNull() || shown.matchesItem(cursor)) {
        ok = mm.simulateOn(player, act(ActionType::Place, cur, here, button == 1 ? 1 : static_cast<int>(cursor.mCount)), slot);
        if (!ok && output && !shown.isNull()) {
            // The slot doesn't take the cursor item (an output): pick up into the cursor instead.
            int room = cursor.getMaxStackSize() - cursor.mCount;
            if (room > 0) ok = mm.simulateOn(player, act(ActionType::Take, here, cur, std::min<int>(room, shown.mCount)), slot);
        }
    } else {
        ok = mm.simulateOn(player, act(ActionType::Swap, here, cur, 0), slot);
    }
    mm.resyncPlayer(player);
    return ok;
}

// ---- open tab -----------------------------------------------------------------------------------

namespace {
struct OpenTab {
    std::string   uuid;
    int           slot    = -1;
    std::uint32_t session = 0;
};
std::unordered_map<std::int64_t, OpenTab> gOpenTabs; // server thread

std::int64_t keyOf(Player const& p) { return p.getOrCreateUniqueID().rawID; }

StationTab const* stationFor(game::Backpack const& backpack, int slot) {
    auto const& slots = backpack.data().upgrades;
    if (slot < 0 || slot >= static_cast<int>(slots.size())) return nullptr;
    auto const& s = slots[static_cast<std::size_t>(slot)];
    if (s.empty()) return nullptr;
    auto* type = upgradeFor(s.item);
    if (!type) return nullptr;
    for (auto const& t : StationTabRegistry::entries()) {
        if (t.upgradeId == type->info().id && t.quickMoveIn) return &t;
    }
    return nullptr;
}

/// The open station tab of `backpack` and its shift-click handler.
std::optional<std::pair<int, StationTab const*>> openTabOf(Player& player, game::Backpack const& backpack) {
    auto slot = openStationTab(player, backpack);
    if (!slot) return std::nullopt;
    auto const* tab = stationFor(backpack, *slot);
    if (!tab) return std::nullopt;
    return std::make_pair(*slot, tab);
}
} // namespace

bool handleTabOpen(Player& player, UpgradeContext const& ctx, nlohmann::json const& data) {
    auto it = data.find("tabOpen");
    if (it == data.end() || !it->is_boolean()) return false;
    if (data.value("uuid", std::string{}) != ctx.backpack().uuid()) return true; // a stale message
    if (it->get<bool>()) {
        setOpenStationTab(player, ctx.backpack(), ctx.slot());
    } else if (auto cur = gOpenTabs.find(keyOf(player)); cur != gOpenTabs.end() && cur->second.uuid == ctx.backpack().uuid()
                                                          && cur->second.slot == ctx.slot()) {
        gOpenTabs.erase(cur);
    }
    return true;
}

void setOpenStationTab(Player& player, game::Backpack const& backpack, std::optional<int> slot) {
    if (!slot) {
        gOpenTabs.erase(keyOf(player));
        return;
    }
    if (gOpenTabs.size() > 512) gOpenTabs.clear();
    gOpenTabs[keyOf(player)] = OpenTab{backpack.uuid(), *slot, net::nativeSession(player)};
}

std::optional<int> openStationTab(Player& player, game::Backpack const& backpack) {
    auto it = gOpenTabs.find(keyOf(player));
    if (it == gOpenTabs.end() || it->second.uuid != backpack.uuid()) return std::nullopt;
    if (it->second.session != net::nativeSession(player)) return std::nullopt; // another screen since
    return it->second.slot;
}

} // namespace abp::upgrade::crafting

// SC StorageContainerMenuBase.mergeSlotStack, player inventory -> backpack: with "Shift Click Into
// Open Tab First" the open station tab (furnace input / fuel, crafting grid, stonecutter input)
// comes before the storage; otherwise it is the last resort.
ABP_REGISTER_BACKPACK_QUICK_MOVE(abp::ui::BackpackQuickMoveHandler{
    .id    = "station.open_tab",
    .order = 50,
    .handle = [](abp::ui::BackpackMenuContext const& c, ItemStack const& stack) -> std::optional<int> {
        using namespace abp::upgrade::crafting;
        auto tab = openTabOf(c.player, *c.backpack);
        if (!tab || stack.isNull()) return std::nullopt;
        abp::upgrade::UpgradeContext ctx(c.backpack, tab->first, &c.player);
        auto                         item = abp::game::ItemConv::toData(stack);
        // SC repeats the quick move of a player inventory slot while items are left: what the
        // first target can't take goes to the next one.
        auto rest = [&](int moved) {
            ItemStack left = stack;
            left.set(static_cast<uchar>(std::max(0, static_cast<int>(stack.mCount) - moved)));
            return left;
        };
        if (abp::feature::gui::effective(&c.player, *c.backpack).openTabFirst) {
            int n = tab->second->quickMoveIn(c.player, ctx, item);
            if (n <= 0) return std::nullopt;
            if (n < stack.mCount) n += c.menu.quickMoveDefault(c.player, rest(n));
            return n;
        }
        int n = c.menu.quickMoveDefault(c.player, stack);
        if (n < stack.mCount) n += tab->second->quickMoveIn(c.player, ctx, item.withCount(item.count - n));
        return n;
    },
});

// Storage -> open station tab first (SC mergeSlotStack for storage slots with the setting on).
ABP_REGISTER_STORAGE_CLICK([](abp::ui::ClickContext const& c, std::shared_ptr<abp::game::Backpack> const& bp, int index) {
    using namespace abp::upgrade::crafting;
    if (c.type != abp::ui::ClickType::QuickMove || !c.offered.isNull() || !bp) return false;
    auto tab = openTabOf(c.player, *bp);
    if (!tab || !abp::feature::gui::effective(&c.player, *bp).openTabFirst) return false;
    auto& inv = bp->inventory();
    if (index < 0 || index >= inv.size() || inv.at(index).empty()) return false;
    auto const&                  d    = inv.at(index);
    auto                         item = d.withCount(std::min(d.count, std::max(d.maxStack, 1)));
    abp::upgrade::UpgradeContext ctx(bp, tab->first, &c.player);
    if (!inv.canExtract(index, item.count, abp::core::AccessContext{abp::core::Source::Player, &c.player})) return false;
    int n = tab->second->quickMoveIn(c.player, ctx, item);
    if (n <= 0) return false; // nothing fits: the usual move into the player inventory
    (void)inv.extract(index, n, abp::core::AccessContext{abp::core::Source::Player, &c.player});
    return true;
});
