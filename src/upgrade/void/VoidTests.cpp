// Void upgrade selftests: `/backpack selftest "void.*"`.

#include <memory>

#include "mc/world/PlayerUIContainer.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/Storage.h"
#include "mod/Config.h"
#include "mod/GeneralConfig.h"
#include "selftest/SelfTest.h"
#include "ui/menu/BackpackMenu.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/void/VoidUpgrade.h"

namespace abp::upgrade::voiding {
namespace {

using core::Tier;
using selftest::Context;
using Where  = ui::MenuManager::SlotAddress::Where;
using Action = ui::MenuManager::ActionType;

core::AccessContext const kPickup{core::Source::Pickup};
core::AccessContext const kAutomation{core::Source::Automation};
core::AccessContext const kPlayer{core::Source::Player};
core::AccessContext const kInternal{core::Source::Internal};

core::ItemStackData data(std::string_view id, int count) { return game::ItemConv::toData(game::ItemConv::make(id, count)); }

int countOf(game::Backpack& bp, std::string_view id) {
    return static_cast<int>(bp.inventory().count([&](core::ItemStackData const& s) { return s.id == id; }));
}

int slotsWith(game::Backpack& bp, std::string_view id) {
    int n = 0;
    for (int i = 0; i < bp.inventory().size(); ++i) n += bp.inventory().at(i).id == id ? 1 : 0;
    return n;
}

/// Installs `id` and configures it: filter mode + ghost items, overflow, work in GUI.
UpgradeContext setup(
    Context&                                ctx,
    std::shared_ptr<game::Backpack> const&  bp,
    std::string const&                      id,
    std::vector<std::string_view> const&    ghosts,
    core::FilterMode                        mode      = core::FilterMode::Allow,
    bool                                    overflow  = false,
    bool                                    workInGui = false
) {
    int            slot = ctx.installUpgrade(bp, id);
    UpgradeContext uc(bp, slot);
    core::Filter   f(id == kAdvancedId ? kAdvancedSlots : kBasicSlots);
    f.mode = mode;
    for (int i = 0; i < static_cast<int>(ghosts.size()); ++i) f.setGhost(i, data(ghosts[static_cast<std::size_t>(i)], 1));
    uc.settings()[std::string(kFilterKey)]    = f.toJson();
    uc.settings()[std::string(kOverflowKey)]  = overflow;
    uc.settings()[std::string(kWorkInGuiKey)] = workInGui;
    uc.saveSettings(true);
    return uc;
}

ABP_REGISTER_SELFTEST("void.pickup_voids", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(Tier::Leather);
    setup(ctx, bp, "void", {"minecraft:cobblestone"});
    ctx.require(ctx.give(player, ctx.backpackItem(*bp), 3) == 3, "give backpack");
    co_await ctx.ticks(2);

    auto cobble = data("minecraft:cobblestone", 20);
    ctx.require(!cobble.empty(), "cobblestone item");
    int taken = upgrade::dispatchPickup(player, cobble);
    ctx.check(taken == 20 && cobble.count == 0, "picked up cobblestone voided (taken " + std::to_string(taken) + ")");
    ctx.check(countOf(*bp, "minecraft:cobblestone") == 0, "voided cobblestone not stored");

    auto dirt = data("minecraft:dirt", 7);
    ctx.check(upgrade::dispatchPickup(player, dirt) == 0 && dirt.count == 7, "non-matching item untouched");

    // Every non-GUI insert path voids it too.
    auto r = bp->inventory().insert(data("minecraft:cobblestone", 5), kAutomation);
    ctx.check(r.voided == 5 && r.inserted == 0, "automation insert voided");
    r = bp->inventory().insert(data("minecraft:cobblestone", 5), kPickup);
    ctx.check(r.voided == 5, "pickup insert voided");
    r = bp->inventory().insert(data("minecraft:cobblestone", 5), kInternal);
    ctx.check(r.inserted == 5 && r.voided == 0, "internal moves are never voided");

    // Disabled upgrade does nothing.
    upgrade::setEnabled(bp, 0, false);
    auto more = data("minecraft:cobblestone", 3);
    ctx.check(upgrade::dispatchPickup(player, more) == 0, "disabled void upgrade inactive");
    upgrade::setEnabled(bp, 0, true);
});

ABP_REGISTER_SELFTEST("void.empty_filter_voids_nothing", [](Context& ctx) {
    auto bp = ctx.createBackpack(Tier::Leather);
    setup(ctx, bp, "void", {});
    auto r = bp->inventory().insert(data("minecraft:stone", 10), kPickup);
    ctx.check(r.inserted == 10 && r.voided == 0, "empty allow list voids nothing");

    auto deny = ctx.createBackpack(Tier::Leather);
    setup(ctx, deny, "void", {}, core::FilterMode::Deny);
    r = deny->inventory().insert(data("minecraft:stone", 10), kPickup);
    ctx.check(r.inserted == 10 && r.voided == 0, "empty deny list voids nothing either");
});

ABP_REGISTER_SELFTEST("void.respects_other_denials", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(Tier::Leather);
    setup(ctx, bp, "void", {"minecraft:diamond"}, core::FilterMode::Deny);
    ctx.require(ctx.give(player, ctx.backpackItem(*bp), 3) == 3, "give backpack");
    co_await ctx.ticks(2);
    // Shulker boxes refused by the foundation rule (`general.allowShulkerBoxes` off; SB's default lets
    // them in): that Deny wins.
    struct ShulkerRule {
        bool saved = config::get<GeneralConfig>().allowShulkerBoxes;
        ShulkerRule() { config::mut<GeneralConfig>().allowShulkerBoxes = false; }
        ~ShulkerRule() { config::mut<GeneralConfig>().allowShulkerBoxes = saved; }
    } shulkerRule;
    auto shulker = data("minecraft:undyed_shulker_box", 1);
    if (shulker.empty()) shulker = data("minecraft:shulker_box", 1);
    ctx.require(!shulker.empty(), "shulker item");
    ctx.check(upgrade::dispatchPickup(player, shulker) == 0 && shulker.count == 1, "denied shulker box not voided");
    ctx.check(bp->inventory().insert(shulker, kPickup).voided == 0, "denied shulker insert not voided");
});

ABP_REGISTER_SELFTEST("void.filter_deny", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(Tier::Leather);
    setup(ctx, bp, "void", {"minecraft:diamond"}, core::FilterMode::Deny);
    ctx.require(ctx.give(player, ctx.backpackItem(*bp), 3) == 3, "give backpack");
    co_await ctx.ticks(2);

    auto stone = data("minecraft:stone", 12);
    ctx.check(upgrade::dispatchPickup(player, stone) == 12, "deny list: unlisted item voided");
    auto diamond = data("minecraft:diamond", 4);
    ctx.check(upgrade::dispatchPickup(player, diamond) == 0 && diamond.count == 4, "deny list: listed item kept");
    auto r = bp->inventory().insert(data("minecraft:diamond", 4), kAutomation);
    ctx.check(r.inserted == 4 && r.voided == 0, "listed item stored");

    // Backpacks and upgrades are never voided, even by a deny-everything list.
    auto backpack = game::ItemConv::toData(game::makeBackpackItem(Tier::Leather));
    ctx.require(!backpack.empty(), "backpack item");
    ctx.check(upgrade::dispatchPickup(player, backpack) == 0 && backpack.count == 1, "backpack item not voided");
    auto upgradeItem = data("alleviatedbackpacks:advanced_void_upgrade", 1);
    ctx.require(!upgradeItem.empty(), "upgrade item registered");
    ctx.check(upgrade::dispatchPickup(player, upgradeItem) == 0 && upgradeItem.count == 1, "upgrade item not voided");
    ctx.check(bp->inventory().insert(upgradeItem, kAutomation).voided == 0, "upgrade insert not voided");
});

ABP_REGISTER_SELFTEST("void.overflow", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(Tier::Leather); // 27 slots
    setup(ctx, bp, "advanced_void", {"minecraft:cobblestone"}, core::FilterMode::Allow, true);
    ctx.require(ctx.give(player, ctx.backpackItem(*bp), 3) == 3, "give backpack");
    co_await ctx.ticks(2);

    auto first = data("minecraft:cobblestone", 10);
    ctx.check(upgrade::dispatchPickup(player, first) == 0, "overflow: nothing voided while not stored");
    auto r = bp->inventory().insert(data("minecraft:cobblestone", 10), kPickup);
    ctx.check(r.inserted == 10 && r.voided == 0, "first stack stored");
    r = bp->inventory().insert(data("minecraft:cobblestone", 100), kPickup);
    ctx.check(r.inserted == 54 && r.voided == 46, "fills the stack, voids the rest (" + std::to_string(r.inserted) + "/" + std::to_string(r.voided) + ")");
    ctx.check(countOf(*bp, "minecraft:cobblestone") == 64, "one full stack kept");
    ctx.check(slotsWith(*bp, "minecraft:cobblestone") == 1, "no second stack started");
    ctx.check(r.remainder.empty(), "nothing left over");

    // Stack full: picked-up cobblestone is voided even without a Pickup upgrade.
    auto more = data("minecraft:cobblestone", 5);
    ctx.check(upgrade::dispatchPickup(player, more) == 5 && more.count == 0, "overflowing pickup voided");

    // Partly full: only the excess is voided at pickup, the rest is left for the inventory.
    bp->inventory().set(0, data("minecraft:cobblestone", 60));
    for (int i = 1; i < bp->inventory().size(); ++i) bp->inventory().set(i, {});
    auto part = data("minecraft:cobblestone", 10);
    ctx.check(upgrade::dispatchPickup(player, part) == 6 && part.count == 4, "only the excess voided (left " + std::to_string(part.count) + ")");

    // Full of other items, none stored: what doesn't fit is voided.
    auto full = ctx.createBackpack(Tier::Leather);
    setup(ctx, full, "advanced_void", {"minecraft:cobblestone"}, core::FilterMode::Allow, true);
    for (int i = 0; i < full->inventory().size(); ++i) full->inventory().set(i, data("minecraft:dirt", 64));
    r = full->inventory().insert(data("minecraft:cobblestone", 7), kAutomation);
    ctx.check(r.voided == 7 && r.inserted == 0, "overflow into a full backpack voided");

    // A big first insert keeps exactly one stack.
    auto big = ctx.createBackpack(Tier::Leather);
    setup(ctx, big, "advanced_void", {"minecraft:cobblestone"}, core::FilterMode::Allow, true);
    r = big->inventory().insert(data("minecraft:cobblestone", 150), kAutomation);
    ctx.check(r.inserted == 64 && r.voided == 86 && slotsWith(*big, "minecraft:cobblestone") == 1, "first insert: one stack kept");

    // Putting it into an empty slot by insertInto (not the GUI) when it's stored elsewhere voids it.
    r = bp->inventory().insertInto(5, data("minecraft:cobblestone", 3), kAutomation);
    ctx.check(r.voided == 3 && bp->inventory().at(5).empty(), "no second stack via a specific slot");

    // Other items are untouched.
    r = bp->inventory().insert(data("minecraft:dirt", 100), kPickup);
    ctx.check(r.inserted == 100 && r.voided == 0, "non-matching items stored normally");
});

ABP_REGISTER_SELFTEST("void.basic_has_no_overflow", [](Context& ctx) {
    auto bp = ctx.createBackpack(Tier::Leather);
    setup(ctx, bp, "void", {"minecraft:cobblestone"}, core::FilterMode::Allow, true);
    auto r = bp->inventory().insert(data("minecraft:cobblestone", 10), kPickup);
    ctx.check(r.voided == 10, "basic void ignores the overflow setting");
});

ABP_REGISTER_SELFTEST("void.work_in_gui", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& mm     = ui::MenuManager::get();
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(Tier::Iron);
    auto  uc     = setup(ctx, bp, "void", {"minecraft:stone"});

    auto r = bp->inventory().insert(data("minecraft:stone", 8), kPlayer);
    ctx.check(r.inserted == 8 && r.voided == 0, "GUI insert kept while 'work in GUI' is off");

    ctx.require(ctx.give(player, ctx.backpackItem(*bp), 0) == 0, "give backpack");
    ctx.require(ctx.give(player, game::ItemConv::make("minecraft:stone", 16), 1) == 1, "give stone");
    ctx.require(ui::BackpackMenu::openFor(player, {game::ItemLocation::Kind::Inventory, 0}), "open menu");
    mm.forceShown(player);
    ctx.check(mm.simulate(player, {Action::Place, {Where::Inventory, 1}, {Where::Menu, 10}, 16}), "shift-click stone in");
    ctx.check(countOf(*bp, "minecraft:stone") == 24, "manual insert stored");
    mm.close(player);

    uc.settings()[std::string(kWorkInGuiKey)] = true;
    uc.saveSettings(true);
    r = bp->inventory().insert(data("minecraft:stone", 8), kPlayer);
    ctx.check(r.voided == 8 && r.inserted == 0, "GUI insert voided with 'work in GUI' on");
    ctx.require(ctx.give(player, game::ItemConv::make("minecraft:stone", 16), 1) == 1, "give stone again");
    ctx.require(ui::BackpackMenu::openFor(player, {game::ItemLocation::Kind::Inventory, 0}), "reopen menu");
    mm.forceShown(player);
    (void)mm.simulate(player, {Action::Place, {Where::Inventory, 1}, {Where::Menu, 10}, 16});
    co_await ctx.ticks(1);
    ctx.check(countOf(*bp, "minecraft:stone") == 24, "manual insert voided");
    ctx.check(player.getInventory().getItem(1).isNull(), "stone left the inventory");
    mm.close(player);
});

ABP_REGISTER_SELFTEST("void.settings_menu", [](Context& ctx) {
    auto& mm     = ui::MenuManager::get();
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(Tier::Iron);
    int   slot   = ctx.installUpgrade(bp, "advanced_void");
    UpgradeContext uc(bp, slot, &player);
    auto*          type = upgrade::byId("advanced_void");
    ctx.require(type != nullptr, "advanced void registered");
    auto menu = type->createSettingsMenu(player, uc);
    ctx.require(menu != nullptr, "settings menu");
    ctx.check(menu->size() == 27, "16 filter slots fit a single chest");
    ctx.require(mm.open(player, menu), "open settings");
    mm.forceShown(player);

    // Ghost: put a cobblestone in filter slot 15 (the 16th).
    ctx.require(ctx.give(player, game::ItemConv::make("minecraft:cobblestone", 3), 4) == 4, "give");
    ctx.check(mm.simulate(player, {Action::Take, {Where::Inventory, 4}, {Where::Cursor, 0}, 3}), "to cursor");
    (void)mm.simulate(player, {Action::Place, {Where::Cursor, 0}, {Where::Menu, 15}, 3});
    auto s = VoidSettings::read(uc);
    ctx.check(s.filter.ghosts.size() == 16 && s.filter.ghosts[15].id == "minecraft:cobblestone", "16th ghost set");

    int control = menu->size() - 9;
    (void)mm.simulate(player, {Action::Take, {Where::Menu, control + 5}, {Where::Cursor, 0}, 1});
    ctx.check(VoidSettings::read(uc).overflow, "overflow toggled on");
    (void)mm.simulate(player, {Action::Take, {Where::Menu, control + 6}, {Where::Cursor, 0}, 1});
    ctx.check(VoidSettings::read(uc).workInGui, "work in GUI toggled on");
    mm.close(player);

    // Hooks were rebuilt with the new settings.
    auto r = bp->inventory().insert(data("minecraft:cobblestone", 70), kPlayer);
    ctx.check(r.inserted == 64 && r.voided == 6, "menu settings take effect (" + std::to_string(r.inserted) + "/" + std::to_string(r.voided) + ")");

    // Settings travel with the upgrade item.
    auto removed = upgrade::remove(bp, slot, nullptr);
    ctx.require(removed.has_value(), "remove");
    ctx.require(upgrade::install(bp, 0, *removed, nullptr), "reinstall");
    auto again = VoidSettings::read(UpgradeContext(bp, 0));
    ctx.check(again.overflow && again.workInGui && again.filter.ghosts[15].id == "minecraft:cobblestone", "settings kept");

    ctx.check(!type->describe(player, UpgradeContext(bp, 0)).empty(), "describe lore");
});

ABP_REGISTER_SELFTEST("void.one_per_backpack", [](Context& ctx) {
    auto bp = ctx.createBackpack(Tier::Iron); // 2 upgrade slots
    ctx.installUpgrade(bp, "void");
    auto adv = data("alleviatedbackpacks:advanced_void_upgrade", 1);
    ctx.check(!upgrade::canInstall(bp, 1, adv, nullptr), "basic and advanced void share one slot group");
});

} // namespace
} // namespace abp::upgrade::voiding
