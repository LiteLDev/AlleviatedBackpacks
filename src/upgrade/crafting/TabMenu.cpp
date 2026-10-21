#include "upgrade/crafting/TabMenu.h"

#include <algorithm>

#include "mc/world/PlayerUIContainer.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/Storage.h"
#include "mod/Config.h"
#include "mod/GeneralConfig.h"
#include "mod/I18n.h"
#include "ui/menu/BackpackMenu.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"

namespace abp::upgrade::crafting {

namespace {
constexpr std::string_view kFiller         = "minecraft:gray_stained_glass_pane";
constexpr int              kInventorySlots = 36;

std::shared_ptr<ui::Slot> filler() { return std::make_shared<ui::StaticSlot>(ui::icon(kFiller, " ")); }

/// Registered tabs sorted by order.
std::vector<TabEntry const*> sortedTabs() {
    std::vector<TabEntry const*> out;
    for (auto const& t : TabRegistry::entries()) out.push_back(&t);
    std::stable_sort(out.begin(), out.end(), [](auto* a, auto* b) { return a->order < b->order; });
    return out;
}

/// The first installed tab of `backpack` other than `except`.
TabEntry const* firstInstalledTab(game::Backpack const& backpack, std::string_view except = {}) {
    for (auto const* t : sortedTabs()) {
        if (t->upgradeId != except && findUpgradeSlot(backpack, t->upgradeId) >= 0) return t;
    }
    return nullptr;
}
} // namespace

int findUpgradeSlot(game::Backpack const& backpack, std::string_view upgradeId) {
    auto const& slots = backpack.data().upgrades;
    for (int i = 0; i < static_cast<int>(slots.size()); ++i) {
        auto const& s = slots[static_cast<std::size_t>(i)];
        if (s.empty()) continue;
        if (auto* u = upgradeFor(s.item); u && u->info().id == upgradeId) return i;
    }
    return -1;
}

bool openTab(Player& player, std::shared_ptr<game::Backpack> backpack, std::string_view upgradeId) {
    if (!backpack) return false;
    int slot = findUpgradeSlot(*backpack, upgradeId);
    if (slot < 0) return false;
    for (auto const& t : TabRegistry::entries()) {
        if (t.upgradeId != upgradeId || !t.make) continue;
        auto menu = t.make(player, UpgradeContext(std::move(backpack), slot));
        if (!menu) return false;
        switchMenu(player, [menu](Player& p) { ui::MenuManager::get().open(p, menu); });
        return true;
    }
    return false;
}

void switchMenu(Player& player, std::function<void(Player&)> open) {
    auto& mm = ui::MenuManager::get();
    if (mm.current(player)) mm.closeThen(player, std::move(open));
    else open(player);
}

// ---- moving items -------------------------------------------------------------------------------

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
    // Merge into matching stacks first, then fill empty slots (hotbar first, like vanilla).
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

core::ItemStackData stash(Player& player, game::Backpack& backpack, core::ItemStackData item) {
    if (item.empty()) return {};
    auto r = backpack.inventory().insert(item, core::AccessContext{core::Source::Player, &player});
    item   = r.remainder;
    if (item.empty()) return {};
    int left = addToPlayer(player, game::ItemConv::toStack(item, 1), item.count);
    return item.withCount(left);
}

bool deliver(Player& player, game::Backpack& backpack, ItemStack const& stack, bool intoBackpack, bool simulate) {
    if (stack.isNull() || stack.mCount == 0) return true;
    auto                data  = game::ItemConv::toData(stack);
    int                 total = data.count;
    core::AccessContext access{core::Source::Player, &player};
    auto&               inv        = backpack.inventory();
    auto                backpackIn = [&](int n, bool sim) {
        if (n <= 0) return 0;
        auto r = inv.insert(data.withCount(n), access, sim);
        return r.inserted + r.voided;
    };
    int toBackpack = 0;
    int toPlayer   = 0;
    if (intoBackpack) {
        toBackpack = backpackIn(total, true);
        toPlayer   = total - toBackpack;
        if (toPlayer > playerRoomFor(player, stack)) return false;
    } else {
        toPlayer   = std::min(total, playerRoomFor(player, stack));
        toBackpack = total - toPlayer;
        if (toBackpack > 0 && backpackIn(toBackpack, true) < toBackpack) return false;
    }
    if (simulate) return true;
    int stored = backpackIn(toBackpack, false);
    int left   = (toBackpack - stored) + (toPlayer > 0 ? addToPlayer(player, stack, toPlayer) : 0);
    if (left > 0) {
        // Should not happen (room was checked); never lose items.
        ItemStack rest = stack;
        rest.set(std::min(left, 255));
        player.drop(rest, false);
    }
    return true;
}

core::ItemStackData giveBack(Player* player, game::Backpack& backpack, core::ItemStackData item) {
    if (item.empty()) return {};
    auto r = backpack.inventory().insert(item, core::AccessContext{core::Source::Internal, player});
    item   = r.remainder;
    if (item.empty() || !player) return item;
    int left = addToPlayer(*player, game::ItemConv::toStack(item, 1), item.count);
    int max  = std::max(item.maxStack, 1);
    while (left > 0) {
        int chunk = std::min({left, max, 255});
        player->drop(game::ItemConv::toStack(item, chunk), false);
        left -= chunk;
    }
    return {};
}

bool allowedInUpgradeSlot(game::Backpack const& backpack, core::ItemStackData const& item) {
    if (item.empty()) return true;
    if (game::isBackpack(item)) return false;
    if (game::isShulkerBox(item.id)) {
        return config::get<GeneralConfig>().allowShulkerBoxes || backpack.hasCapability("allow_shulker_boxes");
    }
    return true;
}

// ---- TabMenu ------------------------------------------------------------------------------------

TabMenu::TabMenu(std::string title, upgrade::UpgradeContext ctx, std::string upgradeId)
: Menu(std::move(title), Rows::Six),
  mCtx(ctx.withPlayer(nullptr)),
  mUpgradeId(std::move(upgradeId)) {}

bool TabMenu::valid() const {
    auto const& slots = mCtx.backpack().data().upgrades;
    if (mCtx.slot() < 0 || mCtx.slot() >= static_cast<int>(slots.size())) return false;
    auto const& s = slots[static_cast<std::size_t>(mCtx.slot())];
    if (s.empty()) return false;
    auto* u = upgradeFor(s.item);
    return u && u->info().id == mUpgradeId;
}

bool TabMenu::shiftIntoBackpack() const {
    if (!valid()) return true;
    auto const& j  = mCtx.settings();
    auto        it = j.find(kShiftIntoBackpackKey);
    return it == j.end() || !it->is_boolean() || it->get<bool>();
}

void TabMenu::build(Player& player) {
    // Bind to the carried backpack item (locked while open; the menu closes if it goes away).
    mSource.reset();
    lockedInventorySlot.reset();
    for (auto const& c : game::scanBackpacks(player)) {
        if (c.uuid && *c.uuid == mCtx.backpack().uuid()) {
            mSource = c.where;
            if (c.where.kind == game::ItemLocation::Kind::Inventory) lockedInventorySlot = c.where.slot;
            break;
        }
    }
    for (int i = 0; i < size(); ++i) setSlot(i, filler());
    if (!valid()) return;
    buildShared(player);
    buildTab(player);
}

void TabMenu::buildShared(Player& player) {
    auto bp = mCtx.backpackPtr();
    setSlot(
        kBackSlot,
        std::make_shared<ui::ButtonSlot>(
            ui::icon("minecraft:chest", trFor(player, "abp.crafting.back", "Back to backpack")),
            [bp](ui::ClickContext const& c) {
                switchMenu(c.player, [bp](Player& p) { ui::BackpackMenu::openBackpack(p, bp); });
            }
        )
    );
    bool intoBackpack = shiftIntoBackpack();
    setSlot(
        kShiftSlot,
        std::make_shared<ui::ButtonSlot>(
            ui::icon(
                intoBackpack ? "minecraft:barrel" : "minecraft:armor_stand",
                intoBackpack ? trFor(player, "abp.crafting.shift_backpack", "Shift-click result: into backpack")
                             : trFor(player, "abp.crafting.shift_inventory", "Shift-click result: into inventory"),
                {trFor(player, "abp.crafting.click_to_toggle", "§7Click to change")}
            ),
            [this](ui::ClickContext const&) {
                if (!valid()) return;
                mCtx.settings()[std::string(kShiftIntoBackpackKey)] = !shiftIntoBackpack();
                mCtx.saveSettings();
                invalidate();
            }
        )
    );

    // Another installed tab (crafting <-> stonecutter).
    if (auto const* other = firstInstalledTab(mCtx.backpack(), mUpgradeId)) {
        std::string id = other->upgradeId;
        setSlot(
            kOtherTabSlot,
            std::make_shared<ui::ButtonSlot>(
                ui::icon(
                    other->iconItem,
                    trFor(
                        player,
                        "abp.crafting.open_tab",
                        "Open: {}",
                        trFor(player, other->labelKey, other->labelFallback)
                    )
                ),
                [bp, id](ui::ClickContext const& c) { openTab(c.player, bp, id); }
            )
        );
    }

    // Backpack storage (rows 3-5) and its page button.
    int size     = mCtx.backpack().inventory().size();
    int pages    = std::max(1, (size + kStorageSlots - 1) / kStorageSlots);
    mStoragePage = std::clamp(mStoragePage, 0, pages - 1);
    for (int i = 0; i < kStorageSlots; ++i) {
        int index = mStoragePage * kStorageSlots + i;
        if (index < size) setSlot(kStorageStart + i, std::make_shared<ui::StorageSlot>(bp, index));
    }
    setSlot(
        kPageSlot,
        std::make_shared<ui::ButtonSlot>(
            ui::icon(
                "minecraft:paper",
                trFor(player, "abp.crafting.storage_page", "Backpack page {} of {}", mStoragePage + 1, pages),
                {trFor(player, "abp.crafting.next_page_hint", "§7Click for the next page")},
                mStoragePage + 1
            ),
            [this, pages](ui::ClickContext const&) {
                mStoragePage = (mStoragePage + 1) % pages;
                invalidate();
            }
        )
    );
}

void TabMenu::onTick(Player& player) {
    if (!valid()) {
        ui::MenuManager::get().close(player);
        return;
    }
    if (!mSource) return;
    auto const& item = game::itemAt(player, *mSource);
    auto        uuid = game::backpackUuid(item);
    if (!game::isBackpack(item) || !uuid || *uuid != mCtx.backpack().uuid()) ui::MenuManager::get().close(player);
}

void TabMenu::onDestroyed() { game::Storage::get().save(mCtx.backpack()); }

int TabMenu::quickMoveIn(Player& player, ItemStack const& stack) {
    if (stack.isNull()) return 0;
    auto r = mCtx.backpack().inventory().insert(
        game::ItemConv::toData(stack),
        core::AccessContext{core::Source::Player, &player}
    );
    return r.inserted + r.voided;
}

// ---- StackSlot ----------------------------------------------------------------------------------

ItemStack StackSlot::display(Player&) const {
    if (!mMenu.valid()) return {};
    auto d = mGet();
    return d.empty() ? ItemStack{} : game::ItemConv::toStack(d, std::min(d.count, 255));
}

bool StackSlot::click(ui::ClickContext const& c) {
    if (!mMenu.valid()) return true;
    if (c.type != ui::ClickType::QuickMove) return false;
    auto item = mGet();
    if (!item.empty()) mSet(stash(c.player, mMenu.context().backpack(), item));
    return true;
}

ItemStack StackSlot::take(Player&, int amount) {
    if (!mMenu.valid()) return {};
    auto d = mGet();
    if (d.empty()) return {};
    int n = std::min({amount, d.count, 255});
    if (n <= 0) return {};
    mSet(d.withCount(d.count - n));
    return game::ItemConv::toStack(d, n);
}

int StackSlot::accept(Player&, ItemStack const& stack) {
    if (!mMenu.valid() || stack.isNull()) return 0;
    auto in = game::ItemConv::toData(stack);
    if (!allowedInUpgradeSlot(mMenu.context().backpack(), in)) return 0;
    auto d   = mGet();
    int  max = std::max(in.maxStack, 1);
    if (d.empty()) {
        int n = std::min(in.count, max);
        mSet(in.withCount(n));
        return n;
    }
    if (!d.sameItem(in)) return 0;
    int n = std::min(in.count, max - d.count);
    if (n <= 0) return 0;
    mSet(d.withCount(d.count + n));
    return n;
}

// ---- ResultSlot ---------------------------------------------------------------------------------

ItemStack ResultSlot::display(Player&) const {
    if (!mMenu.valid() || !mOps.preview) return {};
    return mOps.preview();
}

bool ResultSlot::click(ui::ClickContext const& c) {
    if (!mMenu.valid()) return true;
    switch (c.type) {
    case ui::ClickType::Place:
        return true; // nothing goes into a result slot
    case ui::ClickType::QuickMove:
        if (mOps.craftMax) mOps.craftMax(c.player, mMenu.shiftIntoBackpack());
        return true;
    case ui::ClickType::Swap:
        return !c.offered.isNull(); // only into an empty slot
    case ui::ClickType::Take: {
        // Picking up onto a cursor that can't hold the result: refuse before crafting.
        auto      preview = mOps.preview ? mOps.preview() : ItemStack{};
        ItemStack cursor  = c.player.mPlayerUIContainer->getItem(0);
        if (preview.isNull()) return true;
        if (!cursor.isNull() && cursor.mCount > 0
            && (!cursor.isStackable(preview) || cursor.mCount + preview.mCount > cursor.getMaxStackSize())) {
            return true;
        }
        return false;
    }
    case ui::ClickType::Drop:
        return false;
    }
    return true;
}

ItemStack ResultSlot::take(Player& player, int) {
    if (!mMenu.valid() || !mOps.craftOnce) return {};
    return mOps.craftOnce(player);
}

int ResultSlot::accept(Player& player, ItemStack const& stack) {
    if (stack.isNull()) return 0;
    (void)giveBack(&player, mMenu.context().backpack(), game::ItemConv::toData(stack));
    return stack.mCount;
}

// ---- shared backpack button -------------------------------------------------------------------

namespace {
TabEntry const* buttonTab(ui::BackpackMenuContext const& c) { return firstInstalledTab(*c.backpack); }
} // namespace

} // namespace abp::upgrade::crafting

// One storage-page button for all crafting-type tabs: opens the first installed one.
ABP_REGISTER_BACKPACK_BUTTON(
    abp::ui::BackpackButton{
        .id    = "crafting",
        .order = 70,
        .visible =
            [](abp::ui::BackpackMenuContext const& c) { return abp::upgrade::crafting::buttonTab(c) != nullptr; },
        .icon =
            [](abp::ui::BackpackMenuContext const& c) {
                auto const* tab = abp::upgrade::crafting::buttonTab(c);
                if (!tab) return ItemStack{};
                return abp::ui::icon(tab->iconItem, abp::trFor(c.player, tab->labelKey, tab->labelFallback));
            },
        .onClick =
            [](abp::ui::BackpackMenuContext const& c, abp::ui::ClickContext const&) {
                if (auto const* tab = abp::upgrade::crafting::buttonTab(c)) {
                    abp::upgrade::crafting::openTab(c.player, c.backpack, tab->upgradeId);
                }
            },
    }
);
