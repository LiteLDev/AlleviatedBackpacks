// Selftests for the Filter upgrade: `backpack selftest "filter.*"`.

#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

#include "game/ItemConv.h"
#include "game/Storage.h"
#include "selftest/SelfTest.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/filter/FilterCommon.h"
#include "upgrade/filter/FilterUpgrade.h"

namespace abp::upgrade::filter {
namespace {

using core::Tier;
using selftest::Context;
using Where  = ui::MenuManager::SlotAddress::Where;
using Action = ui::MenuManager::ActionType;

core::AccessContext const kAutomation{core::Source::Automation};
core::AccessContext const kPlayer{core::Source::Player};
core::AccessContext const kPickup{core::Source::Pickup};
core::AccessContext const kInternal{core::Source::Internal};

core::ItemStackData data(std::string_view id, int count) { return game::ItemConv::toData(game::ItemConv::make(id, count)); }

int inserted(game::Backpack& bp, std::string_view id, core::AccessContext const& who, int count = 1) {
    return bp.inventory().insert(data(id, count), who).inserted;
}

/// Extracts one `id` item from wherever it is stored.
int extracted(game::Backpack& bp, std::string_view id, core::AccessContext const& who) {
    return bp.inventory().extractMatching([&](core::ItemStackData const& s) { return s.id == id; }, 1, who).count;
}

core::Filter allowOnly(int slots, std::initializer_list<std::string_view> ids) {
    core::Filter f(slots);
    int          i = 0;
    for (auto id : ids) f.setGhost(i++, data(id, 1));
    return f;
}

ABP_REGISTER_SELFTEST("filter.registered", [](Context& ctx) {
    for (auto id : {kBasicId, kAdvancedId}) {
        auto* u = byId(id);
        ctx.require(u != nullptr, "upgrade " + std::string(id) + " registered");
        ctx.check(!game::ItemConv::make(u->info().itemId, 1).isNull(), "item " + u->info().itemId + " exists");
        ctx.check(u->info().hasSettings && u->info().groupOrId() == "filter", "info");
    }
});

ABP_REGISTER_SELFTEST("filter.empty_allows_everything", [](Context& ctx) {
    auto bp = ctx.createBackpack(Tier::Leather);
    ctx.installUpgrade(bp, "filter");
    ctx.check(inserted(*bp, "minecraft:dirt", kAutomation) == 1, "empty allow list: automation inserts");
    ctx.check(extracted(*bp, "minecraft:dirt", kAutomation) == 1, "empty allow list: automation extracts");
});

ABP_REGISTER_SELFTEST("filter.allow_list_automation_only", [](Context& ctx) {
    auto bp   = ctx.createBackpack(Tier::Leather);
    int  slot = ctx.installUpgrade(bp, "filter");
    filtering::store(UpgradeContext(bp, slot), UpgradeTier::Basic, allowOnly(9, {"minecraft:apple"}));
    ctx.check(inserted(*bp, "minecraft:apple", kAutomation, 5) == 5, "allowed item inserted by automation");
    ctx.check(inserted(*bp, "minecraft:dirt", kAutomation, 5) == 0, "other items refused for automation");
    // Players, pickups, upgrades and internal moves are not filtered.
    ctx.check(inserted(*bp, "minecraft:dirt", kPlayer) == 1, "player not filtered");
    ctx.check(inserted(*bp, "minecraft:dirt", kPickup) == 1, "pickup not filtered");
    ctx.check(inserted(*bp, "minecraft:dirt", core::AccessContext{core::Source::Upgrade}) == 1, "upgrade not filtered");
    ctx.check(inserted(*bp, "minecraft:dirt", kInternal) == 1, "internal not filtered");
    // Extraction (direction both by default).
    ctx.check(extracted(*bp, "minecraft:dirt", kAutomation) == 0, "automation can't extract filtered-out items");
    ctx.check(extracted(*bp, "minecraft:apple", kAutomation) == 1, "automation extracts allowed items");
    ctx.check(extracted(*bp, "minecraft:dirt", kPlayer) == 1, "player extracts anything");
});

ABP_REGISTER_SELFTEST("filter.deny_list", [](Context& ctx) {
    auto bp   = ctx.createBackpack(Tier::Leather);
    int  slot = ctx.installUpgrade(bp, "filter");
    auto f    = allowOnly(9, {"minecraft:dirt"});
    f.mode    = core::FilterMode::Deny;
    filtering::store(UpgradeContext(bp, slot), UpgradeTier::Basic, f);
    ctx.check(inserted(*bp, "minecraft:dirt", kAutomation) == 0, "denied item refused");
    ctx.check(inserted(*bp, "minecraft:stone", kAutomation) == 1, "other items accepted");
});

ABP_REGISTER_SELFTEST("filter.directions", [](Context& ctx) {
    auto           bp   = ctx.createBackpack(Tier::Leather);
    int            slot = ctx.installUpgrade(bp, "filter");
    UpgradeContext uctx(bp, slot);
    filtering::store(uctx, UpgradeTier::Basic, allowOnly(9, {"minecraft:apple"}));
    bp->inventory().insert(data("minecraft:dirt", 10), kInternal);
    bp->inventory().insert(data("minecraft:apple", 10), kInternal);
    ctx.check(directionOf(uctx) == Direction::Both, "default: both");

    setDirection(uctx, Direction::Input);
    ctx.check(inserted(*bp, "minecraft:dirt", kAutomation) == 0, "input: inserts filtered");
    ctx.check(extracted(*bp, "minecraft:dirt", kAutomation) == 1, "input: extracts not filtered");

    setDirection(uctx, Direction::Output);
    ctx.check(inserted(*bp, "minecraft:dirt", kAutomation) == 1, "output: inserts not filtered");
    ctx.check(extracted(*bp, "minecraft:dirt", kAutomation) == 0, "output: extracts filtered");
    ctx.check(extracted(*bp, "minecraft:apple", kAutomation) == 1, "output: allowed extracts");
    ctx.check(bp->inventory().canExtract(0, 1, kPlayer), "output: players unaffected");

    setDirection(uctx, Direction::Both);
    ctx.check(inserted(*bp, "minecraft:dirt", kAutomation) == 0, "both: inserts filtered");
    ctx.check(extracted(*bp, "minecraft:dirt", kAutomation) == 0, "both: extracts filtered");

    // Disabled: no filtering at all.
    setEnabled(bp, slot, false);
    ctx.check(inserted(*bp, "minecraft:dirt", kAutomation) == 1, "disabled: inserts pass");
    ctx.check(extracted(*bp, "minecraft:dirt", kAutomation) == 1, "disabled: extracts pass");
});

ABP_REGISTER_SELFTEST("filter.multiple_filters", [](Context& ctx) {
    auto bp = ctx.createBackpack(Tier::Iron); // 2 upgrade slots
    int  a  = ctx.installUpgrade(bp, "filter");
    int  b  = ctx.installUpgrade(bp, "advanced_filter");
    // A: input allows apples and dirt; B: input denies dirt -> only apples get in.
    UpgradeContext ca(bp, a), cb(bp, b);
    filtering::store(ca, UpgradeTier::Basic, allowOnly(9, {"minecraft:apple", "minecraft:dirt"}));
    auto deny = allowOnly(16, {"minecraft:dirt"});
    deny.mode = core::FilterMode::Deny;
    filtering::store(cb, UpgradeTier::Advanced, deny);
    ctx.check(inserted(*bp, "minecraft:apple", kAutomation) == 1, "apples pass both");
    ctx.check(inserted(*bp, "minecraft:dirt", kAutomation) == 0, "dirt stopped by the second filter");
    ctx.check(inserted(*bp, "minecraft:stone", kAutomation) == 0, "stone stopped by the first filter");
});

ABP_REGISTER_SELFTEST("filter.advanced_match_options", [](Context& ctx) {
    auto           bp   = ctx.createBackpack(Tier::Leather);
    int            slot = ctx.installUpgrade(bp, "advanced_filter");
    UpgradeContext uctx(bp, slot);
    core::Filter   f(16);
    f.matchBy = core::MatchBy::Tag;
    f.tags    = {"minecraft:logs"};
    filtering::store(uctx, UpgradeTier::Advanced, f);
    ctx.check(inserted(*bp, "minecraft:birch_log", kAutomation) == 1, "tag: logs pass");
    ctx.check(inserted(*bp, "minecraft:stone", kAutomation) == 0, "tag: stone refused");

    core::Filter ns(16);
    ns.matchBy = core::MatchBy::Namespace;
    ns.setGhost(3, data("alleviatedbackpacks:upgrade_base", 1));
    filtering::store(uctx, UpgradeTier::Advanced, ns);
    ctx.check(inserted(*bp, "alleviatedbackpacks:upgrade_base", kAutomation) == 1, "namespace: same mod passes");
    ctx.check(inserted(*bp, "minecraft:apple", kAutomation) == 0, "namespace: vanilla refused");

    core::Filter dur = allowOnly(16, {"minecraft:iron_sword"});
    dur.matchDurability = true;
    filtering::store(uctx, UpgradeTier::Advanced, dur);
    auto sword = game::ItemConv::make("minecraft:iron_sword", 1);
    sword.setDamageValue(12);
    ctx.check(bp->inventory().insert(game::ItemConv::toData(sword), kAutomation).inserted == 0, "durability must match");
    ctx.check(inserted(*bp, "minecraft:iron_sword", kAutomation) == 1, "undamaged passes");

    // The same settings on a basic filter are reduced to item matching.
    auto           basic = ctx.createBackpack(Tier::Leather);
    int            bslot = ctx.installUpgrade(basic, "filter");
    UpgradeContext bctx(basic, bslot);
    filtering::store(bctx, UpgradeTier::Basic, f); // tag filter with no ghosts
    auto loaded = filtering::load(bctx, UpgradeTier::Basic);
    ctx.check(loaded.matchBy == core::MatchBy::Item && loaded.tags.empty() && loaded.ghosts.size() == 9, "basic normalized");
    ctx.check(inserted(*basic, "minecraft:stone", kAutomation) == 1, "basic: empty item filter allows all");
});

ABP_REGISTER_SELFTEST("filter.priority_over_void", [](Context& ctx) {
    // Another upgrade that voids everything with a higher priority number must not see items the
    // filter refused (the filter runs first and denies).
    auto bp   = ctx.createBackpack(Tier::Leather);
    int  slot = ctx.installUpgrade(bp, "filter");
    filtering::store(UpgradeContext(bp, slot), UpgradeTier::Basic, allowOnly(9, {"minecraft:apple"}));
    bp->inventory().addInsertFilter("selftest:void", 0, [](core::InsertQuery const&) { return core::InsertVerdict::Void; });
    auto r = bp->inventory().insert(data("minecraft:dirt", 4), kAutomation);
    ctx.check(r.voided == 0 && r.inserted == 0 && r.remainder.count == 4, "refused, not voided");
    bp->inventory().removeHooks("selftest:void");
});

ABP_REGISTER_SELFTEST("filter.settings_menu", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto&          mm   = ui::MenuManager::get();
    auto&          p    = ctx.player();
    auto           bp   = ctx.createBackpack(Tier::Leather);
    int            slot = ctx.installUpgrade(bp, "filter");
    UpgradeContext uctx(bp, slot, &p);
    auto*          type = byId(kBasicId);
    auto           menu = type->createSettingsMenu(p, uctx);
    ctx.require(menu != nullptr, "settings menu");
    ctx.require(mm.open(p, menu), "open");
    mm.forceShown(p);
    co_await ctx.ticks(2);
    int control = menu->size() - 9;
    // Ghost an apple into slot 8 (the last of 9).
    ctx.require(ctx.give(p, game::ItemConv::make("minecraft:apple", 2), 4) == 4, "give");
    ctx.check(mm.simulate(p, {Action::Take, {Where::Inventory, 4}, {Where::Cursor, 0}, 2}), "to cursor");
    (void)mm.simulate(p, {Action::Place, {Where::Cursor, 0}, {Where::Menu, 8}, 2});
    (void)mm.simulate(p, {Action::Place, {Where::Cursor, 0}, {Where::Inventory, 4}, 2});
    ctx.check(filtering::load(uctx, UpgradeTier::Basic).ghosts[8].id == "minecraft:apple", "ghost saved");
    // Slot 9 is a filler in a basic filter (only 9 ghost slots).
    (void)mm.simulate(p, {Action::Take, {Where::Inventory, 4}, {Where::Cursor, 0}, 2});
    (void)mm.simulate(p, {Action::Place, {Where::Cursor, 0}, {Where::Menu, 9}, 2});
    (void)mm.simulate(p, {Action::Place, {Where::Cursor, 0}, {Where::Inventory, 4}, 2});
    ctx.check(filtering::load(uctx, UpgradeTier::Basic).ghosts.size() == 9, "still 9 ghosts");
    // Direction button cycles both -> input -> output -> both.
    (void)mm.simulate(p, {Action::Take, {Where::Menu, control + kDirectionButton}, {Where::Cursor, 0}, 1});
    ctx.check(directionOf(uctx) == Direction::Input, "direction: input");
    co_await ctx.ticks(2);
    ctx.check(mm.shownItem(p, control + kDirectionButton).getTypeName() == "minecraft:hopper", "input icon shown");
    (void)mm.simulate(p, {Action::Take, {Where::Menu, control + kDirectionButton}, {Where::Cursor, 0}, 1});
    ctx.check(directionOf(uctx) == Direction::Output, "direction: output");
    // Hooks follow the menu.
    ctx.check(inserted(*bp, "minecraft:dirt", kAutomation) == 1, "output: inserts pass");
    ctx.check(extracted(*bp, "minecraft:dirt", kAutomation) == 0, "output: dirt can't be extracted");
    (void)mm.simulate(p, {Action::Take, {Where::Menu, control + kDirectionButton}, {Where::Cursor, 0}, 1});
    ctx.check(directionOf(uctx) == Direction::Both, "direction: both");
    auto lore = type->describe(p, uctx);
    ctx.check(lore.size() >= 2, "lore lists filter and direction");
    mm.close(p);
    int apples = 0;
    for (int i = 0; i < 36; ++i) {
        auto const& it = p.getInventory().getItem(i);
        if (!it.isNull() && it.getTypeName() == "minecraft:apple") apples += it.mCount;
    }
    ctx.check(apples == 2, "ghost slots consume nothing");

    // Settings travel with the upgrade item.
    auto item = remove(bp, slot, nullptr);
    ctx.require(item.has_value(), "remove");
    auto other = ctx.createBackpack(Tier::Leather);
    std::string reason;
    ctx.require(install(other, 0, *item, nullptr, &reason), "reinstall: " + reason);
    ctx.check(inserted(*other, "minecraft:dirt", kAutomation) == 0, "filter kept after moving the upgrade");
});

} // namespace
} // namespace abp::upgrade::filter
