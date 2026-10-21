// Selftests for the Pickup upgrade: `backpack selftest "pickup.*"`.

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerPickUpItemEvent.h"

#include "mc/world/PlayerUIContainer.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"

#include "game/ItemConv.h"
#include "game/Storage.h"
#include "selftest/SelfTest.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/filter/FilterCommon.h"
#include "upgrade/pickup/PickupUpgrade.h"

namespace abp::upgrade::pickup {
namespace {

using core::Tier;
using selftest::Context;
using Where  = ui::MenuManager::SlotAddress::Where;
using Action = ui::MenuManager::ActionType;

core::ItemStackData data(std::string_view id, int count) { return game::ItemConv::toData(game::ItemConv::make(id, count)); }

int stored(game::Backpack& bp, std::string_view id) {
    return static_cast<int>(bp.inventory().count([&](core::ItemStackData const& s) { return s.id == id; }));
}

int inInventory(Player& p, std::string_view id) {
    int  n   = 0;
    auto& inv = p.getInventory();
    for (int i = 0; i < inv.getContainerSize(); ++i) {
        auto const& it = inv.getItem(i);
        if (!it.isNull() && it.getTypeName() == id) n += it.mCount;
    }
    return n;
}

/// A carried backpack with one pickup upgrade (`id`) in inventory slot `slot`.
std::shared_ptr<game::Backpack>
carried(Context& ctx, Tier tier, std::string const& id, int invSlot, int* upgradeSlot = nullptr) {
    auto bp = ctx.createBackpack(tier);
    int  s  = ctx.installUpgrade(bp, id);
    if (upgradeSlot) *upgradeSlot = s;
    ctx.require(ctx.give(ctx.player(), ctx.backpackItem(*bp), invSlot) == invSlot, "give backpack");
    return bp;
}

void setFilter(std::shared_ptr<game::Backpack> const& bp, int slot, UpgradeTier tier, core::Filter const& f) {
    filtering::store(UpgradeContext(bp, slot), tier, f);
}

int pickup(Player& p, std::string_view id, int count) {
    auto item = data(id, count);
    return dispatchPickup(p, item);
}

// ---- basic ---------------------------------------------------------------------------------------

ABP_REGISTER_SELFTEST("pickup.registered", [](Context& ctx) {
    for (auto id : {kBasicId, kAdvancedId}) {
        auto* u = byId(id);
        ctx.require(u != nullptr, "upgrade " + std::string(id) + " registered");
        auto item = game::ItemConv::make(u->info().itemId, 1);
        ctx.check(!item.isNull(), "item " + u->info().itemId + " exists in the behavior pack");
        ctx.check(u->info().hasSettings, "has a settings menu");
        ctx.check(u->info().groupOrId() == "pickup", "group");
    }
    ctx.check(byId(kBasicId)->info().tier == UpgradeTier::Basic, "basic tier");
    ctx.check(byId(kAdvancedId)->info().tier == UpgradeTier::Advanced, "advanced tier");
});

ABP_REGISTER_SELFTEST("pickup.empty_filter_takes_all", [](Context& ctx) {
    auto& p  = ctx.player();
    auto  bp = carried(ctx, Tier::Leather, "pickup", 0);
    ctx.check(pickup(p, "minecraft:cobblestone", 20) == 20, "all 20 cobblestone taken");
    ctx.check(pickup(p, "minecraft:apple", 3) == 3, "all apples taken");
    ctx.check(stored(*bp, "minecraft:cobblestone") == 20 && stored(*bp, "minecraft:apple") == 3, "stored in the backpack");
});

ABP_REGISTER_SELFTEST("pickup.allow_list", [](Context& ctx) {
    auto& p    = ctx.player();
    int   slot = 0;
    auto  bp   = carried(ctx, Tier::Leather, "pickup", 0, &slot);
    core::Filter f(filtering::kBasicSlots);
    f.setGhost(4, data("minecraft:apple", 1));
    setFilter(bp, slot, UpgradeTier::Basic, f);
    ctx.check(pickup(p, "minecraft:apple", 5) == 5, "allowed apples taken");
    auto dirt = data("minecraft:dirt", 7);
    ctx.check(dispatchPickup(p, dirt) == 0 && dirt.count == 7, "dirt left for the player inventory");
    ctx.check(stored(*bp, "minecraft:dirt") == 0, "no dirt stored");
});

ABP_REGISTER_SELFTEST("pickup.deny_list", [](Context& ctx) {
    auto& p    = ctx.player();
    int   slot = 0;
    auto  bp   = carried(ctx, Tier::Leather, "pickup", 0, &slot);
    core::Filter f(filtering::kBasicSlots);
    f.mode = core::FilterMode::Deny;
    f.setGhost(0, data("minecraft:dirt", 1));
    setFilter(bp, slot, UpgradeTier::Basic, f);
    ctx.check(pickup(p, "minecraft:dirt", 7) == 0, "denied dirt not taken");
    ctx.check(pickup(p, "minecraft:apple", 5) == 5, "everything else taken");
});

ABP_REGISTER_SELFTEST("pickup.basic_matches_items_only", [](Context& ctx) {
    auto& p    = ctx.player();
    int   slot = 0;
    auto  bp   = carried(ctx, Tier::Leather, "pickup", 0, &slot);
    // A basic upgrade can't match by tag / NBT / durability: such settings are normalized away.
    core::Filter f(filtering::kBasicSlots);
    f.matchBy         = core::MatchBy::Tag;
    f.matchNbt        = true;
    f.matchDurability = true;
    f.tags            = {"minecraft:logs"};
    f.setGhost(0, data("minecraft:oak_log", 1));
    f.setGhost(1, data("minecraft:iron_sword", 1));
    setFilter(bp, slot, UpgradeTier::Basic, f);
    auto loaded = filtering::load(UpgradeContext(bp, slot), UpgradeTier::Basic);
    ctx.check(loaded.matchBy == core::MatchBy::Item && !loaded.matchNbt && !loaded.matchDurability, "options normalized");
    ctx.check(static_cast<int>(loaded.ghosts.size()) == 9, "9 filter slots");
    ctx.check(pickup(p, "minecraft:birch_log", 1) == 0, "no tag matching for basic");
    ctx.check(pickup(p, "minecraft:oak_log", 1) == 1, "item match");
    auto sword = game::ItemConv::make("minecraft:iron_sword", 1);
    sword.setDamageValue(30);
    game::ItemConv::setDisplay(sword, "Renamed");
    auto swordData = game::ItemConv::toData(sword);
    ctx.check(dispatchPickup(p, swordData) == 1, "damaged, renamed sword still matches (NBT/durability ignored)");
});

ABP_REGISTER_SELFTEST("pickup.overflow_to_inventory", [](Context& ctx) {
    auto& p  = ctx.player();
    auto  bp = carried(ctx, Tier::Leather, "pickup", 0);
    auto& inv = bp->inventory();
    for (int i = 0; i < inv.size() - 1; ++i) inv.set(i, data("minecraft:dirt", 64));
    inv.set(inv.size() - 1, data("minecraft:cobblestone", 60));
    auto cobble = data("minecraft:cobblestone", 20);
    int  taken  = dispatchPickup(p, cobble);
    ctx.check(taken == 4, "only what fits is taken (" + std::to_string(taken) + ")");
    ctx.check(cobble.count == 16, "the rest stays for the player inventory");
    ctx.check(stored(*bp, "minecraft:cobblestone") == 64, "slot filled up");
});

ABP_REGISTER_SELFTEST("pickup.multiple_backpacks", [](Context& ctx) {
    auto& p     = ctx.player();
    int   slotA = 0;
    auto  a     = carried(ctx, Tier::Leather, "pickup", 0, &slotA);
    auto  b     = carried(ctx, Tier::Leather, "pickup", 1);
    // A only takes apples; B takes everything.
    core::Filter f(filtering::kBasicSlots);
    f.setGhost(0, data("minecraft:apple", 1));
    setFilter(a, slotA, UpgradeTier::Basic, f);
    ctx.check(pickup(p, "minecraft:apple", 10) == 10, "apples taken");
    ctx.check(stored(*a, "minecraft:apple") == 10 && stored(*b, "minecraft:apple") == 0, "first backpack first");
    ctx.check(pickup(p, "minecraft:stone", 10) == 10, "stone taken");
    ctx.check(stored(*b, "minecraft:stone") == 10, "filtered out by A, taken by B");
    // A full: overflow goes on to B.
    for (int i = 0; i < a->inventory().size(); ++i) a->inventory().set(i, data("minecraft:apple", 64));
    ctx.check(pickup(p, "minecraft:apple", 5) == 5, "apples taken while A is full");
    ctx.check(stored(*b, "minecraft:apple") == 5, "B got A's overflow");
});

ABP_REGISTER_SELFTEST("pickup.disabled_and_removed", [](Context& ctx) {
    auto& p    = ctx.player();
    int   slot = 0;
    auto  bp   = carried(ctx, Tier::Leather, "pickup", 0, &slot);
    setEnabled(bp, slot, false);
    ctx.check(pickup(p, "minecraft:apple", 5) == 0, "disabled upgrade takes nothing");
    setEnabled(bp, slot, true);
    ctx.check(pickup(p, "minecraft:apple", 5) == 5, "re-enabled");
    ctx.require(remove(bp, slot, nullptr).has_value(), "remove");
    ctx.check(pickup(p, "minecraft:apple", 5) == 0, "removed upgrade takes nothing");
});

ABP_REGISTER_SELFTEST("pickup.not_carried", [](Context& ctx) {
    auto& p  = ctx.player();
    auto  bp = ctx.createBackpack(Tier::Leather);
    ctx.installUpgrade(bp, "pickup");
    ctx.check(pickup(p, "minecraft:apple", 5) == 0, "a backpack that isn't carried picks up nothing");
    ctx.check(stored(*bp, "minecraft:apple") == 0, "nothing stored");
});

// ---- advanced ------------------------------------------------------------------------------------

ABP_REGISTER_SELFTEST("pickup.advanced_match_options", [](Context& ctx) {
    auto& p    = ctx.player();
    int   slot = 0;
    auto  bp   = carried(ctx, Tier::Iron, "advanced_pickup", 0, &slot);
    auto  set  = [&](core::Filter f) { setFilter(bp, slot, UpgradeTier::Advanced, f); };
    ctx.check(static_cast<int>(filtering::load(UpgradeContext(bp, slot), UpgradeTier::Advanced).ghosts.size()) == 16, "16 slots");

    // Tag of a ghost item.
    core::Filter tag(filtering::kAdvancedSlots);
    tag.matchBy = core::MatchBy::Tag;
    tag.setGhost(15, data("minecraft:oak_log", 1));
    set(tag);
    ctx.check(pickup(p, "minecraft:birch_log", 2) == 2, "tag of ghost: birch log is a log");
    ctx.check(pickup(p, "minecraft:stone", 2) == 0, "tag of ghost: stone is not");
    // Explicit tags.
    tag.ghosts.assign(16, {});
    tag.tags = {"minecraft:logs"};
    set(tag);
    ctx.check(pickup(p, "minecraft:spruce_log", 1) == 1, "explicit tag");
    ctx.check(pickup(p, "minecraft:dirt", 1) == 0, "explicit tag rejects dirt");

    // Namespace.
    core::Filter ns(filtering::kAdvancedSlots);
    ns.matchBy = core::MatchBy::Namespace;
    ns.setGhost(0, data("alleviatedbackpacks:upgrade_base", 1));
    set(ns);
    ctx.check(pickup(p, "alleviatedbackpacks:upgrade_base", 1) == 1, "same namespace");
    ctx.check(pickup(p, "minecraft:apple", 1) == 0, "other namespace");

    // Durability.
    core::Filter dur(filtering::kAdvancedSlots);
    dur.matchDurability = true;
    dur.setGhost(0, data("minecraft:iron_sword", 1));
    set(dur);
    auto damaged = game::ItemConv::make("minecraft:iron_sword", 1);
    damaged.setDamageValue(40);
    auto damagedData = game::ItemConv::toData(damaged);
    ctx.check(dispatchPickup(p, damagedData) == 0, "durability must match");
    ctx.check(pickup(p, "minecraft:iron_sword", 1) == 1, "undamaged sword matches");
    dur.matchDurability = false;
    set(dur);
    damagedData = game::ItemConv::toData(damaged);
    ctx.check(dispatchPickup(p, damagedData) == 1, "durability ignored when off");

    // NBT.
    core::Filter nbt(filtering::kAdvancedSlots);
    nbt.matchNbt = true;
    nbt.setGhost(0, data("minecraft:diamond", 1));
    set(nbt);
    auto named = game::ItemConv::make("minecraft:diamond", 3);
    game::ItemConv::setDisplay(named, "Shiny");
    auto namedData = game::ItemConv::toData(named);
    ctx.check(dispatchPickup(p, namedData) == 0, "NBT must match");
    ctx.check(pickup(p, "minecraft:diamond", 3) == 3, "plain diamonds match");
    nbt.matchNbt = false;
    set(nbt);
    namedData = game::ItemConv::toData(named);
    ctx.check(dispatchPickup(p, namedData) == 3, "NBT ignored when off");
});

// ---- real item entities ----------------------------------------------------------------------------

/// Drops `stack` on the player (pickable right away). Returns the item entity's id.
ActorUniqueID dropOn(Player& p, ItemStack const& stack) {
    auto* actor = p.getLevel().getSpawner().spawnItem(p.getDimensionBlockSource(), stack, nullptr, p.getPosition(), 0);
    if (!actor) return {};
    actor->mPickupDelay = 0;
    return actor->getOrCreateUniqueID();
}

/// Removes a dropped item entity if it is still there (so later tests don't pick it up).
void discard(Player& p, ActorUniqueID id) {
    if (auto* actor = p.getLevel().fetchEntity(id, false)) actor->remove();
}

ABP_REGISTER_SELFTEST("pickup.world_item_entity", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& p  = ctx.player();
    auto  bp = carried(ctx, Tier::Leather, "pickup", 0);
    co_await ctx.ticks(2);
    dropOn(p, game::ItemConv::make("minecraft:apple", 12));
    for (int i = 0; i < 40 && stored(*bp, "minecraft:apple") < 12; ++i) co_await ctx.ticks(1);
    ctx.check(stored(*bp, "minecraft:apple") == 12, "picked up into the backpack (" + std::to_string(stored(*bp, "minecraft:apple")) + ")");
    ctx.check(inInventory(p, "minecraft:apple") == 0, "nothing went to the player inventory");

    // Partial: the backpack has room for 10 only; the rest goes to the player.
    auto& inv = bp->inventory();
    for (int i = 1; i < inv.size(); ++i) inv.set(i, data("minecraft:dirt", 64));
    inv.set(0, data("minecraft:cobblestone", 54));
    dropOn(p, game::ItemConv::make("minecraft:cobblestone", 30));
    for (int i = 0; i < 40 && inInventory(p, "minecraft:cobblestone") < 20; ++i) co_await ctx.ticks(1);
    co_await ctx.ticks(5);
    int inBp  = stored(*bp, "minecraft:cobblestone") - 54;
    int inInv = inInventory(p, "minecraft:cobblestone");
    ctx.check(inBp == 10, "10 into the backpack (" + std::to_string(inBp) + ")");
    ctx.check(inInv == 20, "20 into the inventory (" + std::to_string(inInv) + ")");
    ctx.check(inBp + inInv == 30, "no duplication or loss");
});

ABP_REGISTER_SELFTEST("pickup.world_full_inventory", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& p  = ctx.player();
    auto  bp = carried(ctx, Tier::Leather, "pickup", 0);
    for (int i = 1; i < 36; ++i) ctx.give(p, game::ItemConv::make("minecraft:stone", 64), i);
    co_await ctx.ticks(2);
    dropOn(p, game::ItemConv::make("minecraft:apple", 6));
    for (int i = 0; i < 40 && stored(*bp, "minecraft:apple") < 6; ++i) co_await ctx.ticks(1);
    ctx.check(stored(*bp, "minecraft:apple") == 6, "picked up with a full inventory (" + std::to_string(stored(*bp, "minecraft:apple")) + ")");
    ctx.check(inInventory(p, "minecraft:stone") == 35 * 64, "inventory untouched");
});

ABP_REGISTER_SELFTEST("pickup.world_full_inventory_cancelled", [](Context& ctx) -> ll::coro::CoroTask<> {
    // Another mod cancelling PlayerPickUpItemEvent (e.g. a protected area) also stops the
    // full-inventory pickup.
    auto& p  = ctx.player();
    auto  bp = carried(ctx, Tier::Leather, "pickup", 0);
    for (int i = 1; i < 36; ++i) ctx.give(p, game::ItemConv::make("minecraft:stone", 64), i);
    auto* who      = &p;
    auto  listener = ll::event::EventBus::getInstance().emplaceListener<ll::event::PlayerPickUpItemEvent>(
        [who](ll::event::PlayerPickUpItemEvent& ev) {
            if (&ev.self() == who) ev.cancel();
        },
        ll::event::EventPriority::High
    );
    co_await ctx.ticks(2);
    auto id = dropOn(p, game::ItemConv::make("minecraft:apple", 6));
    co_await ctx.ticks(20);
    ll::event::EventBus::getInstance().removeListener(listener);
    discard(p, id);
    ctx.check(stored(*bp, "minecraft:apple") == 0, "cancelled pickup stays on the ground");
});

ABP_REGISTER_SELFTEST("pickup.world_filtered_out",[](Context& ctx) -> ll::coro::CoroTask<> {
    auto& p    = ctx.player();
    int   slot = 0;
    auto  bp   = carried(ctx, Tier::Leather, "pickup", 0, &slot);
    core::Filter f(filtering::kBasicSlots);
    f.setGhost(0, data("minecraft:apple", 1));
    setFilter(bp, slot, UpgradeTier::Basic, f);
    co_await ctx.ticks(2);
    dropOn(p, game::ItemConv::make("minecraft:dirt", 5));
    for (int i = 0; i < 40 && inInventory(p, "minecraft:dirt") < 5; ++i) co_await ctx.ticks(1);
    ctx.check(inInventory(p, "minecraft:dirt") == 5, "filtered item goes to the inventory");
    ctx.check(stored(*bp, "minecraft:dirt") == 0, "not into the backpack");
});

// ---- settings menu -------------------------------------------------------------------------------

ABP_REGISTER_SELFTEST("pickup.settings_menu", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& mm   = ui::MenuManager::get();
    auto& p    = ctx.player();
    int   slot = 0;
    auto  bp   = carried(ctx, Tier::Leather, "advanced_pickup", 0, &slot);
    UpgradeContext uctx(bp, slot, &p);
    auto*          type = byId(kAdvancedId);
    auto           menu = type->createSettingsMenu(p, uctx);
    ctx.require(menu != nullptr, "settings menu");
    ctx.check(menu->size() == 27, "single chest for 16 ghost slots");
    ctx.require(mm.open(p, menu), "open");
    mm.forceShown(p);
    co_await ctx.ticks(2);
    ctx.require(ctx.give(p, game::ItemConv::make("minecraft:apple", 3), 5) == 5, "give apples");
    ctx.check(mm.simulate(p, {Action::Take, {Where::Inventory, 5}, {Where::Cursor, 0}, 3}), "to cursor");
    (void)mm.simulate(p, {Action::Place, {Where::Cursor, 0}, {Where::Menu, 15}, 3});
    auto f = filtering::load(uctx, UpgradeTier::Advanced);
    ctx.check(!f.ghosts[15].empty() && f.ghosts[15].id == "minecraft:apple", "ghost stored in the upgrade settings");
    (void)mm.simulate(p, {Action::Place, {Where::Cursor, 0}, {Where::Inventory, 5}, 3});
    // Match-by button (control slot 1) cycles item -> namespace.
    (void)mm.simulate(p, {Action::Take, {Where::Menu, 19}, {Where::Cursor, 0}, 1});
    ctx.check(filtering::load(uctx, UpgradeTier::Advanced).matchBy == core::MatchBy::Namespace, "match by namespace");
    (void)mm.simulate(p, {Action::Take, {Where::Menu, 19}, {Where::Cursor, 0}, 1});
    (void)mm.simulate(p, {Action::Take, {Where::Menu, 19}, {Where::Cursor, 0}, 1});
    ctx.check(filtering::load(uctx, UpgradeTier::Advanced).matchBy == core::MatchBy::Item, "back to item");
    co_await ctx.ticks(2);
    ctx.check(pickup(p, "minecraft:apple", 2) == 2, "filter from the menu is used");
    ctx.check(pickup(p, "minecraft:dirt", 2) == 0, "non-matching refused");
    auto lore = type->describe(p, uctx);
    ctx.check(!lore.empty() && lore[0].find('1') != std::string::npos, "lore shows the filter size");
    mm.close(p);
    ctx.check(inInventory(p, "minecraft:apple") == 3, "ghost slots consume nothing");

    // The basic menu has no match options: control slot 1 is a filler.
    int  bslot = 0;
    auto basic = carried(ctx, Tier::Leather, "pickup", 1, &bslot);
    UpgradeContext bctx(basic, bslot, &p);
    auto bmenu = byId(kBasicId)->createSettingsMenu(p, bctx);
    ctx.require(bmenu && mm.open(p, bmenu), "open basic");
    mm.forceShown(p);
    (void)mm.simulate(p, {Action::Take, {Where::Menu, 19}, {Where::Cursor, 0}, 1});
    ctx.check(filtering::load(bctx, UpgradeTier::Basic).matchBy == core::MatchBy::Item, "basic stays item match");
    (void)mm.simulate(p, {Action::Take, {Where::Menu, 18}, {Where::Cursor, 0}, 1});
    ctx.check(filtering::load(bctx, UpgradeTier::Basic).mode == core::FilterMode::Deny, "basic allow/deny toggle");
    mm.close(p);
});

} // namespace
} // namespace abp::upgrade::pickup
