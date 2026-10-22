// Foundation selftests: core.*, storage.*, upgrade.*, menu.*
// Run in-game with `/backpack selftest` (or `backpack selftest core.*` on the server console).

#include "mc/world/PlayerUIContainer.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/player/PlayerUISlot.h"
#include "mc/world/item/ItemStack.h"

#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/PlayerScan.h"
#include "game/Storage.h"
#include "mod/Config.h"
#include "mod/GeneralConfig.h"
#include "selftest/SelfTest.h"
#include "ui/menu/BackpackMenu.h"
#include "ui/menu/FilterMenu.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"

namespace abp::selftest {
namespace {

using core::Tier;
using Where  = ui::MenuManager::SlotAddress::Where;
using Action = ui::MenuManager::ActionType;

core::AccessContext const kInternal{core::Source::Internal};

core::ItemStackData data(std::string_view id, int count) { return game::ItemConv::toData(game::ItemConv::make(id, count)); }

int countOf(game::Backpack& bp, std::string_view id) {
    return static_cast<int>(bp.inventory().count([&](core::ItemStackData const& s) { return s.id == id; }));
}

ItemStack cursorOf(Player& p) { return p.mPlayerUIContainer->getItem(0); }

// ---- core ------------------------------------------------------------------------------------

ABP_REGISTER_SELFTEST("core.create_backpack", [](Context& ctx) {
    for (auto const& info : core::kTiers) {
        auto bp = ctx.createBackpack(info.tier);
        ctx.check(bp->inventory().size() == info.slots, std::string(info.id) + ": wrong slot count");
        ctx.check(static_cast<int>(bp->data().upgrades.size()) == info.upgradeSlots, std::string(info.id) + ": wrong upgrade slots");
        auto item = game::makeBackpackItem(info.tier, bp->uuid());
        ctx.require(!item.isNull(), "backpack item " + game::backpackItemId(info.tier) + " is not registered");
        ctx.check(game::backpackTierOf(item) == info.tier, "tier of item");
        ctx.check(game::backpackUuid(item) == bp->uuid(), "uuid in item NBT");
        ctx.check(item.getMaxStackSize() == 1, "backpacks must not stack");
    }
});

ABP_REGISTER_SELFTEST("core.item_roundtrip", [](Context& ctx) {
    auto sword = game::ItemConv::make("minecraft:diamond_sword", 1);
    game::ItemConv::setDisplay(sword, "Blade", {"lore line"});
    sword.setDamageValue(17);
    auto d = game::ItemConv::toData(sword);
    ctx.check(d.id == "minecraft:diamond_sword", "id");
    ctx.check(d.damage == 17, "damage = " + std::to_string(d.damage));
    ctx.check(d.maxStack == 1, "max stack");
    auto back = game::ItemConv::toStack(d);
    ctx.check(back.matchesItem(sword) && back.hasSameUserData(sword), "round trip keeps the item");
    ctx.check(back.getCustomName() == sword.getCustomName(), "custom name kept");

    auto stoneA = data("minecraft:stone", 3);
    auto stoneB = data("minecraft:stone", 40);
    ctx.check(stoneA.sameItem(stoneB), "plain stacks have equal identity");
    ctx.check(!stoneA.sameItem(d), "different items differ");
});

ABP_REGISTER_SELFTEST("core.insert_extract", [](Context& ctx) {
    auto  bp  = ctx.createBackpack(Tier::Leather);
    auto& inv = bp->inventory();
    auto  r   = inv.insert(data("minecraft:cobblestone", 64), kInternal);
    ctx.check(r.inserted == 64, "inserted 64");
    r = inv.insert(data("minecraft:cobblestone", 40), kInternal);
    ctx.check(inv.at(0).count == 64 && inv.at(1).count == 40, "merge then fill");
    auto got = inv.extract(1, 15, kInternal);
    ctx.check(got.count == 15 && inv.at(1).count == 25, "extract 15");
    ctx.check(bp->dirty(), "inventory changes mark the backpack dirty");
    // Nesting: other backpacks are refused by default; shulker boxes too when the server says so
    // (SB lets them in unless `containerItemsDisallowed`; `general.allowShulkerBoxes` is the switch).
    auto other  = ctx.createBackpack(Tier::Leather);
    auto nested = game::ItemConv::toData(ctx.backpackItem(*other));
    ctx.check(inv.insert(nested, core::AccessContext{core::Source::Player}).inserted == 0, "backpack nesting refused");
    auto self = game::ItemConv::toData(ctx.backpackItem(*bp));
    ctx.check(inv.insert(self, core::AccessContext{core::Source::Player}).inserted == 0, "backpack can't hold itself");
    auto& general      = config::mut<GeneralConfig>();
    bool  savedShulker = general.allowShulkerBoxes;
    general.allowShulkerBoxes = false;
    ctx.check(inv.insert(data("minecraft:shulker_box", 1), core::AccessContext{core::Source::Player}).inserted == 0, "shulker refused");
    general.allowShulkerBoxes = savedShulker;
});

// ---- storage ---------------------------------------------------------------------------------

ABP_REGISTER_SELFTEST("storage.persist", [](Context& ctx) {
    auto& storage = game::Storage::get();
    auto  bp      = ctx.createBackpack(Tier::Gold);
    auto  uuid    = bp->uuid();
    bp->inventory().insert(data("minecraft:diamond", 30), kInternal);
    bp->inventory().set(80, data("minecraft:oak_log", 12));
    bp->data().settings["selftest"] = 42;
    bp->markDirty();
    bp.reset();
    storage.unload(uuid); // save + drop from cache
    auto again = storage.find(uuid);
    ctx.require(again != nullptr, "backpack reloads from the world DB");
    ctx.check(again->tier() == Tier::Gold, "tier persisted");
    ctx.check(countOf(*again, "minecraft:diamond") == 30, "diamonds persisted");
    ctx.check(again->inventory().at(80).count == 12, "last slot persisted");
    ctx.check(again->data().settings.value("selftest", 0) == 42, "settings persisted");
});

ABP_REGISTER_SELFTEST("storage.item_binding", [](Context& ctx) {
    auto& storage = game::Storage::get();
    auto  item    = game::makeBackpackItem(Tier::Copper);
    ctx.require(!game::backpackUuid(item), "fresh item has no uuid");
    bool changed = false;
    auto bp      = storage.forItem(item, &changed);
    ctx.require(bp != nullptr, "forItem creates storage");
    ctx.check(changed, "item changed (uuid written)");
    ctx.check(game::backpackUuid(item) == bp->uuid(), "uuid bound");
    auto again = storage.forItem(item, &changed);
    ctx.check(again == bp && !changed, "second lookup returns the same backpack");
    storage.erase(bp->uuid());
});

// ---- upgrades --------------------------------------------------------------------------------

class TestUpgrade : public upgrade::IUpgrade {
public:
    upgrade::UpgradeInfo mInfo{
        .id           = "selftest_upgrade",
        .itemId       = "minecraft:structure_void",
        .nameKey      = "abp.selftest.upgrade",
        .nameFallback = "Selftest Upgrade",
        .hasSettings  = true,
    };
    int ticks = 0;
    [[nodiscard]] upgrade::UpgradeInfo const& info() const override { return mInfo; }
    void attach(upgrade::UpgradeContext const& ctx, core::BackpackInventory& inv) override {
        inv.addInsertFilter(ctx.hookOwner(), 0, [](core::InsertQuery const& q) {
            return q.stack.id == "minecraft:dirt" ? core::InsertVerdict::Deny : core::InsertVerdict::Allow;
        });
    }
    void onTick(upgrade::TickContext const&) override { ++ticks; }
    void onPickup(upgrade::UpgradeContext const& ctx, core::ItemStackData& item) override {
        if (item.id != "minecraft:apple") return;
        auto r = ctx.backpack().inventory().insert(item, core::AccessContext{core::Source::Pickup, ctx.player()});
        item.count -= r.inserted + r.voided;
    }
};

ABP_REGISTER_SELFTEST("upgrade.install_rules", [](Context& ctx) {
    auto                   test = std::make_shared<TestUpgrade>();
    upgrade::ScopedUpgrade scoped(test);
    auto                   bp   = ctx.createBackpack(Tier::Iron); // 2 upgrade slots
    auto                   item = data("minecraft:structure_void", 1);
    ctx.require(!item.empty(), "test upgrade item exists");
    ctx.check(!upgrade::canInstall(bp, 0, data("minecraft:stone", 1), nullptr), "non-upgrades refused");
    std::string reason;
    ctx.require(upgrade::install(bp, 0, item, nullptr, &reason), "install: " + reason);
    ctx.check(!upgrade::canInstall(bp, 1, item, nullptr), "max one per backpack");
    ctx.check(bp->inventory().insert(data("minecraft:dirt", 1), kInternal).inserted == 0, "attach() filter active");
    upgrade::setEnabled(bp, 0, false);
    ctx.check(bp->inventory().insert(data("minecraft:dirt", 1), kInternal).inserted == 1, "disabled upgrade inactive");
    upgrade::setEnabled(bp, 0, true);
    upgrade::UpgradeContext(bp, 0).settings()["value"] = 7;
    auto removed = upgrade::remove(bp, 0, nullptr);
    ctx.require(removed.has_value(), "remove returns the item");
    ctx.check(bp->data().upgrades[0].empty(), "slot empty after removal");
    ctx.check(bp->inventory().insert(data("minecraft:dirt", 1), kInternal).inserted == 1, "hooks removed");
    ctx.require(upgrade::install(bp, 1, *removed, nullptr, &reason), "reinstall: " + reason);
    ctx.check(bp->data().upgrades[1].settings.value("value", 0) == 7, "settings travel with the item");
});

ABP_REGISTER_SELFTEST("upgrade.tick_and_pickup", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto                   test = std::make_shared<TestUpgrade>();
    upgrade::ScopedUpgrade scoped(test);
    auto&                  player = ctx.player();
    auto                   bp     = ctx.createBackpack(Tier::Leather);
    ctx.installUpgrade(bp, "selftest_upgrade");
    ctx.require(ctx.give(player, ctx.backpackItem(*bp), 3) == 3, "give backpack");
    co_await ctx.ticks(25);
    ctx.check(test->ticks > 0, "onTick called for a carried backpack");
    auto apples = data("minecraft:apple", 5);
    int  taken  = upgrade::dispatchPickup(player, apples);
    ctx.check(taken == 5 && apples.count == 0, "onPickup took the apples");
    ctx.check(countOf(*bp, "minecraft:apple") == 5, "apples in backpack");
});

class NestingUpgrade : public upgrade::IUpgrade {
public:
    upgrade::UpgradeInfo mInfo{
        .id           = "selftest_nesting",
        .itemId       = "minecraft:structure_void",
        .nameKey      = "abp.selftest.upgrade",
        .nameFallback = "Selftest Upgrade",
    };
    [[nodiscard]] upgrade::UpgradeInfo const& info() const override { return mInfo; }
    void attach(upgrade::UpgradeContext const& ctx, core::BackpackInventory&) override {
        ctx.backpack().addCapability("allow_nesting");
    }
};

ABP_REGISTER_SELFTEST("upgrade.nesting_cycles", [](Context& ctx) {
    upgrade::ScopedUpgrade scoped(std::make_shared<NestingUpgrade>());
    auto                   a = ctx.createBackpack(Tier::Leather);
    auto                   b = ctx.createBackpack(Tier::Leather);
    ctx.installUpgrade(a, "selftest_nesting");
    ctx.installUpgrade(b, "selftest_nesting");
    core::AccessContext player{core::Source::Player};
    auto                itemA = game::ItemConv::toData(ctx.backpackItem(*a));
    auto                itemB = game::ItemConv::toData(ctx.backpackItem(*b));
    ctx.check(a->inventory().insert(itemA, player).inserted == 0, "a backpack never holds itself");
    ctx.check(a->inventory().insert(itemB, player).inserted == 1, "allow_nesting capability lets B into A");
    ctx.check(b->inventory().insert(itemA, player).inserted == 0, "A into B would be a cycle");
});

// ---- menu ------------------------------------------------------------------------------------

ABP_REGISTER_SELFTEST("menu.open_transfer_close", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& mm     = ui::MenuManager::get();
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(Tier::Leather);
    ctx.require(ctx.give(player, ctx.backpackItem(*bp), 0) == 0, "give backpack");
    ctx.require(ctx.give(player, game::ItemConv::make("minecraft:stone", 64), 1) == 1, "give stone");
    ctx.require(ui::BackpackMenu::openFor(player, {game::ItemLocation::Kind::Inventory, 0}), "open menu");
    ctx.require(mm.current(player) != nullptr, "session exists");
    co_await ctx.ticks(5);
    ctx.check(mm.isShown(player), "menu shown after the open delay");
    mm.forceShown(player);

    // Shift-click stone into the backpack.
    ctx.check(mm.simulate(player, {Action::Place, {Where::Inventory, 1}, {Where::Menu, 10}, 64}), "quick move in");
    ctx.check(countOf(*bp, "minecraft:stone") == 64, "64 stone stored");
    ctx.check(player.getInventory().getItem(1).isNull(), "stone left the inventory");
    // Take half to the cursor, place it into another slot.
    ctx.check(mm.simulate(player, {Action::Take, {Where::Menu, 0}, {Where::Cursor, 0}, 32}), "take to cursor");
    ctx.check(cursorOf(player).mCount == 32, "cursor holds 32");
    ctx.check(mm.simulate(player, {Action::Place, {Where::Cursor, 0}, {Where::Menu, 5}, 32}), "place from cursor");
    ctx.check(bp->inventory().at(5).count == 32 && cursorOf(player).isNull(), "placed into slot 5");
    // The open backpack's own inventory slot is locked.
    ctx.check(!mm.simulate(player, {Action::Place, {Where::Inventory, 0}, {Where::Menu, 8}, 1}), "locked slot refused");
    // Displays follow the model.
    co_await ctx.ticks(3);
    ctx.check(mm.shownItem(player, 5).mCount == 32, "display synced");
    // Take back out to the inventory (shift-click).
    ctx.check(mm.simulate(player, {Action::Take, {Where::Menu, 5}, {Where::Inventory, 2}, 32}), "quick move out");
    ctx.check(player.getInventory().getItem(2).mCount == 32, "stone back in inventory");
    // Leave something on the cursor; closing returns it.
    ctx.check(mm.simulate(player, {Action::Take, {Where::Menu, 0}, {Where::Cursor, 0}, 10}), "take 10");
    mm.close(player);
    ctx.check(mm.current(player) == nullptr, "closed");
    ctx.check(cursorOf(player).isNull(), "cursor emptied on close");
    int stone = 0;
    for (int i = 0; i < 36; ++i) {
        auto const& it = player.getInventory().getItem(i);
        if (!it.isNull() && it.getTypeName() == "minecraft:stone") stone += it.mCount;
    }
    ctx.check(stone == 42, "no items lost or duplicated (stone in inventory = " + std::to_string(stone) + ")");
    ctx.check(countOf(*bp, "minecraft:stone") == 22, "22 left in backpack");
});

ABP_REGISTER_SELFTEST("menu.buttons_and_pages", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& mm     = ui::MenuManager::get();
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(Tier::Diamond); // 108 slots -> 3 pages
    ctx.require(ui::BackpackMenu::openBackpack(player, bp), "open");
    mm.forceShown(player);
    auto menu = std::dynamic_pointer_cast<ui::BackpackMenu>(mm.current(player));
    ctx.require(menu != nullptr, "backpack menu");
    ctx.check(menu->pageCount() == 3, "3 pages");
    ctx.check(!mm.simulate(player, {Action::Take, {Where::Menu, 47}, {Where::Cursor, 0}, 1}), "button click is not a move");
    ctx.check(menu->page() == 1, "next page");
    bp->inventory().set(50, data("minecraft:emerald", 9)); // page 2, slot 5
    co_await ctx.ticks(3);
    ctx.check(mm.shownItem(player, 5).mCount == 9, "page 2 shows slot 50");
    ctx.check(mm.shownItem(player, 46).mCount == 2, "page indicator");
    (void)mm.simulate(player, {Action::Take, {Where::Menu, 52}, {Where::Cursor, 0}, 1});
    ctx.check(menu->view() == ui::BackpackMenu::View::Upgrades, "upgrades view");
    ctx.check(cursorOf(player).isNull(), "buttons never give items");
    mm.close(player);
});

// World stopping with an open menu and an item on the cursor (MenuManager::closeAll(true), what
// ServerStoppingEvent runs): the item goes to the inventory, else the backpack, else stays on the
// cursor (saved with the player); never dropped, never duplicated.
ABP_REGISTER_SELFTEST("menu.shutdown_keeps_cursor", [](Context& ctx) {
    auto& mm     = ui::MenuManager::get();
    auto& player = ctx.player();
    auto& inv    = player.getInventory();
    auto  bp     = ctx.createBackpack(Tier::Leather);
    auto  open   = [&] {
        ctx.require(ui::BackpackMenu::openFor(player, {game::ItemLocation::Kind::Inventory, 0}), "open menu");
        mm.forceShown(player);
        player.setPlayerUIItem(PlayerUISlot::CursorSelected, game::ItemConv::make("minecraft:diamond", 5), false);
    };
    auto diamondsInInventory = [&] {
        int n = 0;
        for (int i = 0; i < 36; ++i) {
            auto const& it = inv.getItem(i);
            if (!it.isNull() && it.getTypeName() == "minecraft:diamond") n += it.mCount;
        }
        return n;
    };
    ctx.require(ctx.give(player, ctx.backpackItem(*bp), 0) == 0, "give backpack");

    // 1. Room in the inventory.
    open();
    mm.closeAll(true);
    ctx.check(mm.current(player) == nullptr, "session closed");
    ctx.check(cursorOf(player).isNull(), "cursor emptied");
    ctx.check(diamondsInInventory() == 5, "5 diamonds in the inventory, got " + std::to_string(diamondsInInventory()));
    for (int i = 1; i < 36; ++i) inv.setItem(i, ItemStack::EMPTY_ITEM());

    // 2. Full inventory: into the backpack.
    for (int i = 1; i < 36; ++i) inv.setItem(i, game::ItemConv::make("minecraft:dirt", 64));
    open();
    mm.closeAll(true);
    ctx.check(cursorOf(player).isNull(), "cursor emptied (backpack)");
    ctx.check(countOf(*bp, "minecraft:diamond") == 5, "5 diamonds in the backpack, got " + std::to_string(countOf(*bp, "minecraft:diamond")));
    ctx.check(diamondsInInventory() == 0, "none in the inventory");

    // 3. Inventory and backpack full: stays on the cursor (saved with the player).
    for (int i = 0; i < bp->inventory().size(); ++i) bp->inventory().set(i, data("minecraft:dirt", 64));
    open();
    mm.closeAll(true);
    ctx.check(mm.current(player) == nullptr, "session closed (full)");
    ctx.check(cursorOf(player).mCount == 5 && cursorOf(player).getTypeName() == "minecraft:diamond", "5 diamonds kept on the cursor");
    ctx.check(countOf(*bp, "minecraft:diamond") == 0, "nothing duplicated into the backpack");
    player.setPlayerUIItem(PlayerUISlot::CursorSelected, ItemStack::EMPTY_ITEM(), false);
});

ABP_REGISTER_SELFTEST("menu.filter_ghosts", [](Context& ctx) {
    auto& mm     = ui::MenuManager::get();
    auto& player = ctx.player();
    auto  filter = std::make_shared<core::Filter>(9);
    auto  menu   = std::make_shared<ui::FilterMenu>(
        "Filter",
        [filter] { return *filter; },
        [filter](core::Filter const& f) { *filter = f; },
        ui::FilterMenu::Options{}
    );
    ctx.require(mm.open(player, menu), "open filter menu");
    mm.forceShown(player);
    ctx.require(ctx.give(player, game::ItemConv::make("minecraft:iron_ingot", 5), 4) == 4, "give");
    ctx.check(mm.simulate(player, {Action::Take, {Where::Inventory, 4}, {Where::Cursor, 0}, 5}), "to cursor");
    (void)mm.simulate(player, {Action::Place, {Where::Cursor, 0}, {Where::Menu, 2}, 5});
    ctx.check(!filter->ghosts[2].empty() && filter->ghosts[2].id == "minecraft:iron_ingot", "ghost set");
    ctx.check(cursorOf(player).mCount == 5, "ghost slots don't consume items");
    (void)mm.simulate(player, {Action::Take, {Where::Menu, 18}, {Where::Cursor, 0}, 1}); // allow/deny toggle
    ctx.check(filter->mode == core::FilterMode::Deny, "mode toggled");
    (void)mm.simulate(player, {Action::Take, {Where::Menu, 2}, {Where::Cursor, 0}, 1});
    ctx.check(filter->ghosts[2].empty(), "ghost cleared");
    mm.close(player);
    ctx.check(cursorOf(player).isNull(), "cursor returned");
});

} // namespace
} // namespace abp::selftest
