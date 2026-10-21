// Selftests for U3 (gui.*): `/backpack selftest "gui.*"`.

#include <algorithm>

#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/world/PlayerUIContainer.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

#include "feature/gui/Gui.h"
#include "game/Backpack.h"
#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/PlayerScan.h"
#include "game/Storage.h"
#include "selftest/SelfTest.h"
#include "ui/menu/BackpackMenu.h"
#include "ui/menu/MenuManager.h"

namespace abp::feature::gui {
namespace {

using core::Tier;
using selftest::Context;
using Where  = ui::MenuManager::SlotAddress::Where;
using Action = ui::MenuManager::ActionType;

core::AccessContext const kPlayerSrc{core::Source::Player};

core::ItemStackData data(std::string_view id, int count) { return game::ItemConv::toData(game::ItemConv::make(id, count)); }

std::string idAt(game::Backpack& bp, int slot) { return bp.inventory().at(slot).empty() ? "-" : bp.inventory().at(slot).id; }

int invCount(Player& p, std::string_view id) {
    int n = 0;
    for (int i = 0; i < 36; ++i) {
        auto const& it = p.getInventory().getItem(i);
        if (!it.isNull() && it.getTypeName() == id) n += it.mCount;
    }
    return n;
}

int bpCount(game::Backpack& bp, std::string_view id) {
    return static_cast<int>(bp.inventory().count([&](core::ItemStackData const& s) { return s.id == id; }));
}

/// Resets the player's global options (they are stored in the world).
struct PlayerOptionsGuard {
    Player& p;
    ~PlayerOptionsGuard() {
        for (auto const* key : {kSortBy, kOpenTabFirst, kNotifications, kKeepHotbar}) setPlayerOption(p, key, std::nullopt);
    }
};

/// Menu slot of a registered backpack button (pages through "More" when needed). -1 if hidden.
int buttonSlot(Context& ctx, Player& player, std::string const& id) {
    auto& mm   = ui::MenuManager::get();
    auto  menu = std::dynamic_pointer_cast<ui::BackpackMenu>(mm.current(player));
    if (!menu) return -1;
    ui::BackpackMenuContext bctx{player, *menu, menu->backpack()};
    std::vector<ui::BackpackButton const*> list;
    for (auto const& b : ui::BackpackButtonRegistry::entries()) {
        if (b.visible && !b.visible(bctx)) continue;
        list.push_back(&b);
    }
    std::stable_sort(list.begin(), list.end(), [](auto* a, auto* b) { return a->order < b->order; });
    auto it = std::find_if(list.begin(), list.end(), [&](auto* b) { return b->id == id; });
    if (it == list.end()) return -1;
    int idx = static_cast<int>(it - list.begin());
    if (list.size() <= 4) return 48 + idx;
    // Paged: 3 per page, "More" in slot 51. Click it until the wanted page is shown (fresh menu = page 0).
    for (int i = 0; i < idx / 3; ++i) {
        (void)mm.simulate(player, {Action::Take, {Where::Menu, 51}, {Where::Cursor, 0}, 1});
        menu->ensureBuilt(player);
    }
    ctx.info("button " + id + " is on button page " + std::to_string(idx / 3 + 1));
    return 48 + idx % 3;
}

void click(Player& player, int slot, bool shift = false) {
    auto& mm = ui::MenuManager::get();
    if (shift) (void)mm.simulate(player, {Action::Take, {Where::Menu, slot}, {Where::Inventory, 20}, 1});
    else (void)mm.simulate(player, {Action::Take, {Where::Menu, slot}, {Where::Cursor, 0}, 1});
    if (auto m = mm.current(player)) m->ensureBuilt(player);
}

// ---- sorting ------------------------------------------------------------------------------------

ABP_REGISTER_SELFTEST("gui.sort_modes", [](Context& ctx) {
    auto  bp  = ctx.createBackpack(Tier::Leather);
    auto& inv = bp->inventory();
    inv.set(0, data("minecraft:stone", 10));
    inv.set(1, data("minecraft:apple", 3));
    inv.set(2, data("minecraft:oak_log", 20));
    inv.set(5, data("minecraft:stone", 60));
    ctx.require(sortBackpack(*bp, core::SortBy::Name), "sort by name");
    ctx.check(idAt(*bp, 0) == "minecraft:apple" && idAt(*bp, 1) == "minecraft:oak_log" && idAt(*bp, 2) == "minecraft:stone",
              "name order: " + idAt(*bp, 0) + "," + idAt(*bp, 1) + "," + idAt(*bp, 2));
    ctx.check(inv.at(2).count == 64 && inv.at(3).count == 6, "stone merged into full stacks");
    ctx.require(sortBackpack(*bp, core::SortBy::Count), "sort by count");
    ctx.check(idAt(*bp, 0) == "minecraft:stone" && idAt(*bp, 2) == "minecraft:oak_log", "count order (most first)");
    inv.set(10, data("alleviatedbackpacks:upgrade_base", 1));
    ctx.require(sortBackpack(*bp, core::SortBy::Mod), "sort by mod");
    ctx.check(idAt(*bp, 0) == "alleviatedbackpacks:upgrade_base", "mod order: " + idAt(*bp, 0));
    ctx.require(sortBackpack(*bp, core::SortBy::Tag), "sort by tag");
    ctx.check(bpCount(*bp, "minecraft:stone") == 70 && bpCount(*bp, "minecraft:apple") == 3, "no items lost");
    ctx.check(idAt(*bp, 5) == "-" && idAt(*bp, 4) != "-", "tag sort packs items into 5 slots");
});

ABP_REGISTER_SELFTEST("gui.sort_respects_slots", [](Context& ctx) {
    auto  bp  = ctx.createBackpack(Tier::Leather);
    auto& inv = bp->inventory();
    inv.set(0, data("minecraft:stone", 5));
    inv.set(3, data("minecraft:apple", 5));
    inv.set(7, data("minecraft:diamond", 2));
    setNoSort(*bp, 0, true);
    setMemory(*bp, 20, data("minecraft:diamond", 1));
    ctx.require(sortBackpack(*bp, core::SortBy::Name), "sort");
    ctx.check(idAt(*bp, 0) == "minecraft:stone", "no-sort slot untouched");
    ctx.check(idAt(*bp, 20) == "minecraft:diamond" && inv.at(20).count == 2, "memory slot got its item");
    ctx.check(idAt(*bp, 1) == "minecraft:apple", "others laid out after the no-sort slot");
    ctx.check(noSortCount(*bp) == 1 && memoryCount(*bp) == 1, "counts");
});

ABP_REGISTER_SELFTEST("gui.sort_button", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& mm     = ui::MenuManager::get();
    auto& player = ctx.player();
    PlayerOptionsGuard guard{player};
    auto  bp     = ctx.createBackpack(Tier::Leather);
    bp->inventory().set(9, data("minecraft:stone", 3));
    bp->inventory().set(2, data("minecraft:apple", 1));
    ctx.require(ui::BackpackMenu::openBackpack(player, bp), "open");
    mm.forceShown(player);
    int slot = buttonSlot(ctx, player, "gui.sort");
    ctx.require(slot >= 48, "sort button shown");
    co_await ctx.ticks(2);
    ctx.check(mm.shownItem(player, slot).getTypeName() == "minecraft:hopper", "sort icon");
    click(player, slot);
    ctx.check(idAt(*bp, 0) == "minecraft:apple" && idAt(*bp, 1) == "minecraft:stone", "click sorts");
    ctx.check(player.mPlayerUIContainer->getItem(0).isNull(), "button gives nothing");
    // Shift-click cycles the mode and saves it on the backpack.
    click(player, slot, true);
    ctx.check(effective(&player, *bp).sortBy == core::SortBy::Count, "mode -> count");
    ctx.check(backpackOverrides(*bp).value(kSortBy, "") == "count", "persisted per backpack");
    ctx.check(invCount(player, "minecraft:hopper") == 0, "shift-click gives nothing");
    mm.close(player);
});

// ---- transfer -----------------------------------------------------------------------------------

ABP_REGISTER_SELFTEST("gui.transfer_to_backpack", [](Context& ctx) {
    auto& player = ctx.player();
    PlayerOptionsGuard guard{player};
    auto  bp = ctx.createBackpack(Tier::Leather);
    ctx.give(player, game::ItemConv::make("minecraft:stone", 32), 0);  // hotbar
    ctx.give(player, game::ItemConv::make("minecraft:stone", 16), 12); // main
    ctx.give(player, game::ItemConv::make("minecraft:apple", 5), 13);
    ctx.give(player, game::ItemConv::make("minecraft:dirt", 7), 14);
    bp->inventory().set(0, data("minecraft:apple", 1));
    // Matching only: just the apples.
    TransferOptions opt;
    opt.matchingOnly = true;
    int n            = transferToBackpack(player, *bp, opt);
    ctx.check(n == 5 && bpCount(*bp, "minecraft:apple") == 6, "matching moved apples (" + std::to_string(n) + ")");
    ctx.check(invCount(player, "minecraft:dirt") == 7, "dirt stayed");
    // All, keeping the hotbar.
    opt.matchingOnly = false;
    n                = transferToBackpack(player, *bp, opt);
    ctx.check(n == 23, "moved 16 stone + 7 dirt (" + std::to_string(n) + ")");
    ctx.check(player.getInventory().getItem(0).mCount == 32, "hotbar kept");
    // Including the hotbar, with a locked slot.
    ctx.give(player, game::ItemConv::make("minecraft:dirt", 3), 1);
    opt.keepHotbar = false;
    opt.lockedSlot = 1;
    n              = transferToBackpack(player, *bp, opt);
    ctx.check(n == 32 && player.getInventory().getItem(0).isNull(), "hotbar moved");
    ctx.check(player.getInventory().getItem(1).mCount == 3, "locked slot untouched");
    // Filters apply: another backpack is refused (no nesting).
    auto other = ctx.createBackpack(Tier::Leather);
    ctx.give(player, ctx.backpackItem(*other), 20);
    opt.lockedSlot.reset();
    transferToBackpack(player, *bp, opt);
    ctx.check(!player.getInventory().getItem(20).isNull(), "backpack item refused by the backpack's filters");
});

ABP_REGISTER_SELFTEST("gui.transfer_to_player", [](Context& ctx) {
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(Tier::Leather);
    bp->inventory().set(0, data("minecraft:stone", 64));
    bp->inventory().set(1, data("minecraft:apple", 10));
    ctx.give(player, game::ItemConv::make("minecraft:apple", 1), 9);
    TransferOptions opt;
    opt.matchingOnly = true;
    int n            = transferToPlayer(player, *bp, opt);
    ctx.check(n == 10 && invCount(player, "minecraft:apple") == 11, "matching: apples only");
    ctx.check(bpCount(*bp, "minecraft:stone") == 64, "stone stays");
    opt.matchingOnly = false;
    n                = transferToPlayer(player, *bp, opt);
    ctx.check(n == 64 && bp->inventory().isEmpty(), "all moved");
    ctx.check(player.getInventory().getItem(0).isNull(), "hotbar left empty (keep hotbar)");
    // Fill the main inventory: what doesn't fit stays in the backpack.
    for (int i = 9; i < 36; ++i) ctx.give(player, game::ItemConv::make("minecraft:dirt", 64), i);
    bp->inventory().set(3, data("minecraft:diamond", 5));
    n = transferToPlayer(player, *bp, opt);
    ctx.check(n == 0 && bpCount(*bp, "minecraft:diamond") == 5, "no room: nothing lost");
});

ABP_REGISTER_SELFTEST("gui.transfer_buttons", [](Context& ctx) {
    auto& mm     = ui::MenuManager::get();
    auto& player = ctx.player();
    PlayerOptionsGuard guard{player};
    auto  bp = ctx.createBackpack(Tier::Leather);
    ctx.require(ctx.give(player, ctx.backpackItem(*bp), 10) == 10, "give backpack");
    ctx.give(player, game::ItemConv::make("minecraft:iron_ingot", 20), 11);
    ctx.require(ui::BackpackMenu::openFor(player, {game::ItemLocation::Kind::Inventory, 10}), "open");
    mm.forceShown(player);
    int toBp = buttonSlot(ctx, player, "gui.to_backpack");
    ctx.require(toBp >= 48, "to-backpack button");
    click(player, toBp);
    ctx.check(bpCount(*bp, "minecraft:iron_ingot") == 20, "moved into the backpack");
    ctx.check(game::isBackpack(player.getInventory().getItem(10)), "the open backpack itself stays");
    int toPl = buttonSlot(ctx, player, "gui.to_player");
    ctx.require(toPl >= 48, "to-player button");
    click(player, toPl);
    ctx.check(invCount(player, "minecraft:iron_ingot") == 20 && bp->inventory().isEmpty(), "moved back");
    mm.close(player);
});

// ---- memory / no-sort -------------------------------------------------------------------------

ABP_REGISTER_SELFTEST("gui.memory_slots", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& mm     = ui::MenuManager::get();
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(Tier::Leather);
    auto& inv    = bp->inventory();
    setMemory(*bp, 5, data("minecraft:diamond", 3));
    ctx.check(memory(*bp)[5].id == "minecraft:diamond" && memory(*bp)[5].count == 1, "remembered");
    auto r = inv.insert(data("minecraft:diamond", 4), kPlayerSrc);
    ctx.check(r.inserted == 4 && inv.at(5).count == 4, "insert goes to the memory slot first");
    ctx.check(inv.insertInto(5, data("minecraft:stone", 1), kPlayerSrc).inserted == 0, "other items refused");
    inv.set(5, {});
    ctx.check(inv.insert(data("minecraft:stone", 1).withCount(64 * 27), kPlayerSrc).inserted == 64 * 26, "memory slot stays reserved");
    ctx.check(inv.at(5).empty(), "slot 5 still empty");
    // Persisted with the backpack.
    auto uuid = bp->uuid();
    bp.reset();
    game::Storage::get().unload(uuid);
    bp = game::Storage::get().find(uuid);
    ctx.require(bp != nullptr, "reload");
    ctx.check(memory(*bp)[5].id == "minecraft:diamond", "memory persisted");
    ctx.check(bp->inventory().insertInto(5, data("minecraft:stone", 1), kPlayerSrc).inserted == 0, "filter rebuilt after load");
    // The empty memory slot shows a ghost of its item.
    ctx.require(ui::BackpackMenu::openBackpack(player, bp), "open");
    mm.forceShown(player);
    co_await ctx.ticks(2);
    auto shown = mm.shownItem(player, 5);
    ctx.check(shown.getTypeName() == "minecraft:diamond", "ghost shown (" + shown.getTypeName() + ")");
    // Clicking the ghost gives nothing.
    (void)mm.simulate(player, {Action::Take, {Where::Menu, 5}, {Where::Cursor, 0}, 1});
    ctx.check(player.mPlayerUIContainer->getItem(0).isNull(), "ghost can't be taken");
    mm.close(player);
    clearMemory(*bp);
    ctx.check(memoryCount(*bp) == 0, "cleared");
    ctx.check(bp->inventory().insertInto(5, data("minecraft:dirt", 1), kPlayerSrc).inserted == 1, "filter gone");
});

ABP_REGISTER_SELFTEST("gui.slot_picker", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& mm     = ui::MenuManager::get();
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(Tier::Iron); // 54 slots -> 2 pages
    bp->inventory().set(2, data("minecraft:emerald", 7));
    ctx.require(openSlotPicker(player, bp, PickMode::Memory), "open memory picker");
    mm.forceShown(player);
    (void)mm.simulate(player, {Action::Take, {Where::Menu, 2}, {Where::Cursor, 0}, 1});
    ctx.check(memory(*bp)[2].id == "minecraft:emerald", "click remembers the slot's item");
    ctx.check(bp->inventory().at(2).count == 7 && player.mPlayerUIContainer->getItem(0).isNull(), "nothing moved");
    // Drop an item from the cursor onto an empty slot: remembered, not consumed.
    ctx.give(player, game::ItemConv::make("minecraft:gold_ingot", 4), 15);
    (void)mm.simulate(player, {Action::Take, {Where::Inventory, 15}, {Where::Cursor, 0}, 4});
    (void)mm.simulate(player, {Action::Place, {Where::Cursor, 0}, {Where::Menu, 8}, 4});
    ctx.check(memory(*bp)[8].id == "minecraft:gold_ingot", "cursor item remembered");
    ctx.check(player.mPlayerUIContainer->getItem(0).mCount == 4, "cursor kept");
    (void)mm.simulate(player, {Action::Place, {Where::Cursor, 0}, {Where::Inventory, 15}, 4});
    (void)mm.simulate(player, {Action::Take, {Where::Menu, 2}, {Where::Cursor, 0}, 1});
    ctx.check(memory(*bp)[2].empty(), "click again forgets");
    (void)mm.simulate(player, {Action::Take, {Where::Menu, 49}, {Where::Cursor, 0}, 1});
    ctx.check(memoryCount(*bp) == 0, "forget all");
    // No-sort picker, page 2.
    ctx.require(openSlotPicker(player, bp, PickMode::NoSort), "open no-sort picker");
    co_await ctx.ticks(1);
    mm.forceShown(player);
    (void)mm.simulate(player, {Action::Take, {Where::Menu, 47}, {Where::Cursor, 0}, 1}); // next page
    if (auto m = mm.current(player)) m->ensureBuilt(player);
    (void)mm.simulate(player, {Action::Take, {Where::Menu, 3}, {Where::Cursor, 0}, 1});  // slot 48
    ctx.check(noSort(*bp)[48], "slot 48 excluded from sorting");
    (void)mm.simulate(player, {Action::Take, {Where::Menu, 48}, {Where::Cursor, 0}, 1}); // all
    ctx.check(noSortCount(*bp) == 54, "all slots excluded");
    (void)mm.simulate(player, {Action::Take, {Where::Menu, 49}, {Where::Cursor, 0}, 1}); // none
    ctx.check(noSortCount(*bp) == 0, "none excluded");
    // Back to the backpack.
    (void)mm.simulate(player, {Action::Take, {Where::Menu, 53}, {Where::Cursor, 0}, 1});
    co_await ctx.ticks(2);
    ctx.check(std::dynamic_pointer_cast<ui::BackpackMenu>(mm.current(player)) != nullptr, "back to the backpack menu");
    mm.close(player);
});

// ---- settings -------------------------------------------------------------------------------------

ABP_REGISTER_SELFTEST("gui.settings_resolution", [](Context& ctx) {
    auto& player = ctx.player();
    PlayerOptionsGuard guard{player};
    auto  bp = ctx.createBackpack(Tier::Leather);
    auto  d  = serverDefaults();
    auto  e  = effective(&player, *bp);
    ctx.check(e.sortBy == d.sortBy && e.keepHotbar == d.keepHotbar, "defaults");
    setPlayerOption(player, kSortBy, std::string("tag"));
    setPlayerOption(player, kNotifications, false);
    ctx.check(effective(&player, *bp).sortBy == core::SortBy::Tag, "player global applies");
    ctx.check(!effective(&player, *bp).notifications, "player global flag applies");
    setBackpackOption(*bp, kSortBy, std::string("mod"));
    ctx.check(effective(&player, *bp).sortBy == core::SortBy::Mod, "backpack override wins");
    auto other = ctx.createBackpack(Tier::Leather);
    ctx.check(effective(&player, *other).sortBy == core::SortBy::Tag, "other backpacks keep the global");
    setBackpackOption(*bp, kSortBy, std::nullopt);
    ctx.check(effective(&player, *bp).sortBy == core::SortBy::Tag, "cleared override falls back");
    ctx.check(playerOverrides(player).value(kSortBy, "") == "tag", "player settings stored");
    ctx.check(effective(nullptr, *bp).sortBy == d.sortBy, "no player: server defaults");
});

ABP_REGISTER_SELFTEST("gui.shift_click_open_tab", [](Context& ctx) {
    auto& mm     = ui::MenuManager::get();
    auto& player = ctx.player();
    PlayerOptionsGuard guard{player};
    auto  bp = ctx.createBackpack(Tier::Leather);
    ctx.give(player, game::ItemConv::make("minecraft:stone", 10), 12);
    ctx.require(ui::BackpackMenu::openBackpack(player, bp, {}, ui::BackpackMenu::View::Upgrades), "open upgrades page");
    mm.forceShown(player);
    // Not an upgrade: the open tab can't take it, so it goes into storage.
    ctx.check(mm.simulate(player, {Action::Place, {Where::Inventory, 12}, {Where::Menu, 10}, 10}), "shift-click in");
    ctx.check(bpCount(*bp, "minecraft:stone") == 10, "went into storage from the upgrades page");
    // Storage first: same result for plain items.
    setBackpackOption(*bp, kOpenTabFirst, false);
    ctx.give(player, game::ItemConv::make("minecraft:dirt", 3), 13);
    ctx.check(mm.simulate(player, {Action::Place, {Where::Inventory, 13}, {Where::Menu, 10}, 3}), "shift-click in (storage first)");
    ctx.check(bpCount(*bp, "minecraft:dirt") == 3, "storage first");
    mm.close(player);
});

ABP_REGISTER_SELFTEST("gui.notifications_and_hints", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& mm     = ui::MenuManager::get();
    auto& player = ctx.player();
    PlayerOptionsGuard guard{player};
    auto  bp = ctx.createBackpack(Tier::Leather);
    bp->inventory().set(4, data("minecraft:stone", 3));
    setNoSort(*bp, 4, true);
    ctx.require(ui::BackpackMenu::openBackpack(player, bp), "open");
    mm.forceShown(player);
    co_await ctx.ticks(2);
    auto lore = mm.shownItem(player, 4).getCustomLore();
    ctx.check(std::any_of(lore.begin(), lore.end(), [](std::string const& l) { return l.find("No-sort") != std::string::npos; }), "no-sort hint in lore");
    mm.close(player);
    setBackpackOption(*bp, kNotifications, false);
    ctx.require(ui::BackpackMenu::openBackpack(player, bp), "reopen");
    mm.forceShown(player);
    co_await ctx.ticks(2);
    ctx.check(mm.shownItem(player, 4).getCustomLore().empty(), "hints off with notifications off");
    mm.close(player);
});

// ---- opening ----------------------------------------------------------------------------------------

ABP_REGISTER_SELFTEST("gui.open_carried", [](Context& ctx) {
    auto& mm     = ui::MenuManager::get();
    auto& player = ctx.player();
    auto  worn   = ctx.createBackpack(Tier::Iron);
    auto  held   = ctx.createBackpack(Tier::Leather);
    auto  off    = ctx.createBackpack(Tier::Copper);
    ctx.check(!ui::BackpackMenu::openCarried(player), "nothing carried");
    ctx.give(player, ctx.backpackItem(*held), 6);
    ctx.require(ui::BackpackMenu::openCarried(player), "inventory backpack");
    auto menu = std::dynamic_pointer_cast<ui::BackpackMenu>(mm.current(player));
    ctx.check(menu && menu->backpack() == held, "opened the inventory one");
    mm.close(player);
    game::setItemAt(player, {game::ItemLocation::Kind::Offhand, 0}, ctx.backpackItem(*off));
    ctx.require(ui::BackpackMenu::openCarried(player), "offhand");
    menu = std::dynamic_pointer_cast<ui::BackpackMenu>(mm.current(player));
    ctx.check(menu && menu->backpack() == off, "offhand beats inventory");
    mm.close(player);
    game::ItemLocation chest{game::ItemLocation::Kind::Armor, static_cast<int>(SharedTypes::Legacy::ArmorSlot::Torso)};
    game::setItemAt(player, chest, ctx.backpackItem(*worn));
    ctx.require(ui::BackpackMenu::openCarried(player), "worn");
    menu = std::dynamic_pointer_cast<ui::BackpackMenu>(mm.current(player));
    ctx.check(menu && menu->backpack() == worn, "chest slot first");
    ctx.check(menu && menu->source() && menu->source()->kind == game::ItemLocation::Kind::Armor, "bound to the chest slot");
    mm.close(player);
    game::setItemAt(player, chest, ItemStack::EMPTY_ITEM());
    game::setItemAt(player, {game::ItemLocation::Kind::Offhand, 0}, ItemStack::EMPTY_ITEM());
});

ABP_REGISTER_SELFTEST("gui.more_buttons", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& mm = ui::MenuManager::get();
    if (mm.sessionCount() > 0) {
        ctx.info("skipped: other menus are open (the test adds buttons to the registry)");
        co_return;
    }
    auto& player  = ctx.player();
    auto& entries = ui::BackpackButtonRegistry::entries();
    auto  before  = entries.size();
    int   clicks  = 0;
    for (int i = 0; i < 3; ++i) {
        entries.push_back(ui::BackpackButton{
            .id      = "selftest.extra" + std::to_string(i),
            .order   = 900 + i,
            .icon    = [i](ui::BackpackMenuContext const&) { return ui::icon("minecraft:stick", "Extra " + std::to_string(i)); },
            .onClick = [&clicks](ui::BackpackMenuContext const&, ui::ClickContext const&) { ++clicks; },
        });
    }
    auto bp = ctx.createBackpack(Tier::Leather);
    if (ui::BackpackMenu::openBackpack(player, bp)) {
        mm.forceShown(player);
        co_await ctx.ticks(2);
        ctx.check(mm.shownItem(player, 51).getTypeName() == "minecraft:hopper_minecart", "More button shown");
        int slot = buttonSlot(ctx, player, "selftest.extra2");
        co_await ctx.ticks(2);
        ctx.check(mm.shownItem(player, slot).getTypeName() == "minecraft:stick", "paged to the extra button");
        click(player, slot);
        ctx.check(clicks == 1, "extra button works on page 2");
        // Paging wraps around to the first page.
        click(player, 51);
        co_await ctx.ticks(2);
        ctx.check(mm.shownItem(player, 48).getTypeName() != "minecraft:stick", "back on the first button page");
        mm.close(player);
    } else {
        ctx.check(false, "open");
    }
    entries.resize(before);
});

} // namespace
} // namespace abp::feature::gui
