// Magnet upgrade selftests: `backpack selftest "magnet.*"`.

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/item/ExperienceOrb.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/phys/AABB.h"

#include "game/ItemConv.h"
#include "game/PlayerScan.h"
#include "mod/Config.h"
#include "selftest/SelfTest.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/magnet/MagnetInternal.h"

namespace abp::upgrade::magnet {
namespace {

using selftest::Context;
using core::Tier;
using Where  = ui::MenuManager::SlotAddress::Where;
using Action = ui::MenuManager::ActionType;

core::ItemStackData data(std::string_view id, int count) { return game::ItemConv::toData(game::ItemConv::make(id, count)); }

int countOf(game::Backpack& bp, std::string_view id) {
    return static_cast<int>(bp.inventory().count([&](core::ItemStackData const& s) { return s.id == id; }));
}

/// Removes every item entity / XP orb near the player (leftovers of other tests).
void clearAround(Player& player, int range = 12) {
    auto& region = player.getDimensionBlockSource();
    for (auto* a : region.getEntities(rangeBox(player, range))) {
        if (a && (a->isType(ActorType::ItemEntity) || a->isType(ActorType::Experience))) a->remove();
    }
}

/// Moves the test player away from the world spawn (where a real client player may stand) and
/// clears loose items there.
ll::coro::CoroTask<> isolate(Context& ctx, Player& player) {
    auto pos = player.getFeetPos();
    pos      = Vec3{pos.x + 24.0f, pos.y, pos.z + 24.0f};
    player.teleport(pos, player.getDimensionId());
    co_await ctx.ticks(3);
    // Stand on the ground (the test player spawns in the air above the world spawn).
    auto ground = player.getDimensionBlockSource().getAboveTopSolidBlock(
        static_cast<int>(std::floor(pos.x)),
        static_cast<int>(std::floor(pos.z)),
        false,
        false
    );
    player.teleport(Vec3{pos.x, static_cast<float>(ground), pos.z}, player.getDimensionId());
    for (int i = 0; i < 40 && !player.isOnGround(); ++i) co_await ctx.ticks(1);
    co_await ctx.ticks(2);
    clearAround(player);
}

/// Spawns an item entity `dx` blocks east (`dz` south) of the player's feet, with no pickup delay.
ActorUniqueID spawnItem(Player& player, std::string_view id, int count, float dx, float dz = 0.0f, Actor* source = nullptr) {
    auto  stack = game::ItemConv::make(id, count);
    auto  feet  = player.getFeetPos();
    auto* item  = player.getLevel().getSpawner().spawnItem(
        player.getDimensionBlockSource(),
        stack,
        nullptr,
        Vec3{feet.x + dx, feet.y + 0.1f, feet.z + dz},
        0
    );
    if (!item) return ActorUniqueID{};
    item->pickupDelay() = 0;
    // spawnItem throws the item in a random direction; keep it where it was put so it neither
    // drifts into the player's vanilla pickup reach nor out of the magnet's range.
    const_cast<Vec3&>(item->getPosDelta()) = Vec3{0.0f, 0.0f, 0.0f};
    if (source) {
        item->setSourceEntity(source);
        item->age() = 0;
    }
    return item->getOrCreateUniqueID();
}

ItemActor* findItem(Player& player, ActorUniqueID id) {
    auto* a = player.getLevel().fetchEntity(id, false);
    if (!a || a->mRemoved || !a->isType(ActorType::ItemEntity)) return nullptr;
    return static_cast<ItemActor*>(a);
}

/// Total count of `id` lying on the ground near the player.
int groundCount(Player& player, std::string_view id, int range = 6) {
    int n = 0;
    for (auto* a : player.getDimensionBlockSource().getEntities(rangeBox(player, range), 2.0f, ActorType::ItemEntity)) {
        if (!a || a->mRemoved || !a->isType(ActorType::ItemEntity)) continue;
        auto const& s = static_cast<ItemActor*>(a)->item();
        if (!s.isNull() && s.getTypeName() == id) n += s.mCount;
    }
    return n;
}

std::vector<ExperienceOrb*> orbsAround(Player& player, int range) {
    std::vector<ExperienceOrb*> out;
    for (auto* a : player.getDimensionBlockSource().getEntities(rangeBox(player, range), 2.0f, ActorType::Experience)) {
        if (a && !a->mRemoved && a->isType(ActorType::Experience)) out.push_back(static_cast<ExperienceOrb*>(a));
    }
    return out;
}

/// Runs the upgrade's tick directly (not carried, so the PlayerTicker doesn't race the test).
void tickMagnet(std::shared_ptr<game::Backpack> const& bp, int slot, Player& player) {
    auto* type = upgradeFor(bp->data().upgrades.at(static_cast<std::size_t>(slot)).item);
    if (!type) return;
    UpgradeContext ctx(bp, slot, &player);
    type->onTick(TickContext{ctx, player, game::ItemLocation{}, 10});
}

ABP_REGISTER_SELFTEST("magnet.registration", [](Context& ctx) {
    auto* basic = byId(kBasicId);
    auto* adv   = byId(kAdvancedId);
    ctx.require(basic && adv, "both magnet upgrades registered");
    ctx.check(basic->info().itemId == "alleviatedbackpacks:magnet_upgrade", "basic item id");
    ctx.check(adv->info().itemId == "alleviatedbackpacks:advanced_magnet_upgrade", "advanced item id");
    ctx.check(!game::ItemConv::make(basic->info().itemId).isNull(), "basic item exists in the behavior pack");
    ctx.check(!game::ItemConv::make(adv->info().itemId).isNull(), "advanced item exists in the behavior pack");
    auto bp = ctx.createBackpack(Tier::Iron); // 2 upgrade slots
    ctx.installUpgrade(bp, std::string(kBasicId));
    auto advItem = data(adv->info().itemId, 1);
    ctx.check(!canInstall(bp, 1, advItem, nullptr), "only one magnet (basic or advanced) per backpack");
    auto& s = bp->data().upgrades[0].settings;
    ctx.check(s.value("pickupItems", false) && s.value("pickupXp", false), "items and XP on by default");
});

ABP_REGISTER_SELFTEST("magnet.settings_parse", [](Context& ctx) {
    auto s = Settings::read(nlohmann::json::object(), kBasicSlots);
    ctx.check(s.pickupItems && s.pickupXp && s.filter.ghosts.size() == kBasicSlots, "defaults");
    core::Filter f(kAdvancedSlots);
    f.mode = core::FilterMode::Deny;
    f.setGhost(3, data("minecraft:dirt", 1));
    auto j = nlohmann::json{{"pickupItems", false}, {"pickupXp", true}, {"filter", f.toJson()}};
    s      = Settings::read(j, kAdvancedSlots);
    ctx.check(!s.pickupItems && s.pickupXp, "toggles read");
    ctx.check(s.filter.mode == core::FilterMode::Deny && s.filter.ghosts.size() == kAdvancedSlots, "filter read");
    ctx.check(!s.filter.passes(data("minecraft:dirt", 1)), "deny list rejects dirt");
});

ABP_REGISTER_SELFTEST("magnet.pulls_items_in_range", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& player = ctx.player();
    co_await isolate(ctx, player);
    auto bp   = ctx.createBackpack(Tier::Leather);
    int  slot = ctx.installUpgrade(bp, std::string(kBasicId));
    auto near = spawnItem(player, "minecraft:cobblestone", 20, 2.3f);
    auto far  = spawnItem(player, "minecraft:cobblestone", 7, 7.0f);
    ctx.require(findItem(player, near) && findItem(player, far), "items spawned");
    tickMagnet(bp, slot, player);
    ctx.check(countOf(*bp, "minecraft:cobblestone") == 20, "near stack pulled into the backpack (got "
                  + std::to_string(countOf(*bp, "minecraft:cobblestone")) + ")");
    ctx.check(findItem(player, near) == nullptr, "near item entity removed");
    ctx.check(findItem(player, far) != nullptr, "item outside range 3 left alone");
    clearAround(player);
});

ABP_REGISTER_SELFTEST("magnet.advanced_range", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& player = ctx.player();
    co_await isolate(ctx, player);
    auto basic = ctx.createBackpack(Tier::Leather);
    auto adv   = ctx.createBackpack(Tier::Leather);
    int  bs    = ctx.installUpgrade(basic, std::string(kBasicId));
    int  as    = ctx.installUpgrade(adv, std::string(kAdvancedId));
    auto item  = spawnItem(player, "minecraft:oak_log", 5, 4.5f);
    ctx.require(findItem(player, item) != nullptr, "item spawned");
    tickMagnet(basic, bs, player);
    ctx.check(countOf(*basic, "minecraft:oak_log") == 0, "basic magnet (range 3) can't reach 4.5 blocks");
    tickMagnet(adv, as, player);
    ctx.check(countOf(*adv, "minecraft:oak_log") == 5, "advanced magnet (range 5) pulls it");
    clearAround(player);
});

ABP_REGISTER_SELFTEST("magnet.filter", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& player = ctx.player();
    co_await isolate(ctx, player);
    auto bp   = ctx.createBackpack(Tier::Leather);
    int  slot = ctx.installUpgrade(bp, std::string(kBasicId));
    UpgradeContext uc(bp, slot);
    core::Filter   f(kBasicSlots);
    f.setGhost(0, data("minecraft:diamond", 1));
    uc.settings()["filter"] = f.toJson();
    uc.saveSettings(true);
    auto dirt    = spawnItem(player, "minecraft:dirt", 3, 2.0f);
    auto diamond = spawnItem(player, "minecraft:diamond", 2, 2.5f);
    tickMagnet(bp, slot, player);
    ctx.check(countOf(*bp, "minecraft:diamond") == 2, "allow list: diamond pulled");
    ctx.check(countOf(*bp, "minecraft:dirt") == 0 && findItem(player, dirt), "allow list: dirt left on the ground");
    // Walking over items uses the same filter.
    auto apples = data("minecraft:apple", 4);
    UpgradeContext withPlayer(bp, slot, &player);
    byId(kBasicId)->onPickup(withPlayer, apples);
    ctx.check(apples.count == 4, "onPickup respects the filter");
    auto diamonds = data("minecraft:diamond", 3);
    byId(kBasicId)->onPickup(withPlayer, diamonds);
    ctx.check(diamonds.count == 0 && countOf(*bp, "minecraft:diamond") == 5, "onPickup inserts matching items");
    (void)diamond;
    clearAround(player);
});

ABP_REGISTER_SELFTEST("magnet.respects_delays", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& player = ctx.player();
    co_await isolate(ctx, player);
    auto bp      = ctx.createBackpack(Tier::Leather);
    int  slot    = ctx.installUpgrade(bp, std::string(kBasicId));
    auto delayed = spawnItem(player, "minecraft:gold_ingot", 1, 2.3f);
    if (auto* d = findItem(player, delayed)) d->pickupDelay() = 1000;
    auto own = spawnItem(player, "minecraft:iron_ingot", 1, -2.3f, 0.0f, &player);
    spawnItem(player, "minecraft:emerald", 1, 0.0f, 2.3f);
    tickMagnet(bp, slot, player);
    ctx.check(countOf(*bp, "minecraft:gold_ingot") == 0, "items with a pickup delay are skipped");
    ctx.check(countOf(*bp, "minecraft:iron_ingot") == 0, "items the carrier just threw are skipped");
    ctx.check(countOf(*bp, "minecraft:emerald") == 1, "other items are pulled");
    if (auto* o = findItem(player, own)) o->age() = config::get<MagnetConfig>().ownDropDelayTicks + 1;
    tickMagnet(bp, slot, player);
    ctx.check(countOf(*bp, "minecraft:iron_ingot") == 1, "own drops are pulled after the delay");
    clearAround(player);
});

ABP_REGISTER_SELFTEST("magnet.full_backpack", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& player = ctx.player();
    co_await isolate(ctx, player);
    auto bp   = ctx.createBackpack(Tier::Leather);
    int  slot = ctx.installUpgrade(bp, std::string(kBasicId));
    for (int i = 0; i < bp->inventory().size() - 1; ++i) bp->inventory().set(i, data("minecraft:dirt", 64));
    bp->inventory().set(bp->inventory().size() - 1, data("minecraft:stone", 60));
    auto stone = spawnItem(player, "minecraft:stone", 10, 2.3f);
    auto sand  = spawnItem(player, "minecraft:sand", 5, -2.3f);
    auto s     = Settings::read(UpgradeContext(bp, slot).settings(), kBasicSlots);
    auto r     = pullItems(UpgradeContext(bp, slot, &player), player, s, 3, config::get<MagnetConfig>());
    ctx.check(r == PullResult::Blocked, "full backpack reports Blocked (longer cooldown)");
    ctx.check(countOf(*bp, "minecraft:stone") == 64, "the part that fits is taken");
    ctx.check(findItem(player, stone) == nullptr, "the partly pulled entity is replaced");
    ctx.check(groundCount(player, "minecraft:stone") == 6, "the rest (6) stays on the ground");
    ctx.check(findItem(player, sand) != nullptr, "items that don't fit stay on the ground");
    clearAround(player);
});

ABP_REGISTER_SELFTEST("magnet.denied_items_dont_block", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& player = ctx.player();
    co_await isolate(ctx, player);
    auto bp    = ctx.createBackpack(Tier::Leather);
    auto other = ctx.createBackpack(Tier::Leather);
    int  slot  = ctx.installUpgrade(bp, std::string(kBasicId));
    auto stack = ctx.backpackItem(*other);
    auto feet  = player.getFeetPos();
    auto* item = player.getLevel().getSpawner().spawnItem(
        player.getDimensionBlockSource(), stack, nullptr, Vec3{feet.x + 2.3f, feet.y + 0.1f, feet.z}, 0);
    ctx.require(item != nullptr, "backpack item spawned");
    item->pickupDelay() = 0;
    auto id = item->getOrCreateUniqueID();
    auto s  = Settings::read(UpgradeContext(bp, slot).settings(), kBasicSlots);
    auto r  = pullItems(UpgradeContext(bp, slot, &player), player, s, 3, config::get<MagnetConfig>());
    ctx.check(r != PullResult::Blocked, "an item the backpack refuses doesn't count as 'full'");
    ctx.check(findItem(player, id) != nullptr, "refused backpack item stays on the ground");
    clearAround(player);
});

ABP_REGISTER_SELFTEST("magnet.toggles_and_disable", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& player = ctx.player();
    co_await isolate(ctx, player);
    auto bp   = ctx.createBackpack(Tier::Leather);
    int  slot = ctx.installUpgrade(bp, std::string(kBasicId));
    UpgradeContext uc(bp, slot);
    uc.settings()["pickupItems"] = false;
    uc.saveSettings();
    auto item = spawnItem(player, "minecraft:bone", 3, 2.6f);
    tickMagnet(bp, slot, player);
    ctx.check(countOf(*bp, "minecraft:bone") == 0, "pickupItems off: nothing pulled");
    auto bones = data("minecraft:bone", 2);
    byId(kBasicId)->onPickup(UpgradeContext(bp, slot, &player), bones);
    ctx.check(bones.count == 2, "pickupItems off: no pickup either");
    uc.settings()["pickupItems"] = true;
    uc.saveSettings();
    tickMagnet(bp, slot, player);
    ctx.check(countOf(*bp, "minecraft:bone") == 3, "pickupItems on again: pulled");
    (void)item;

    // A disabled upgrade isn't ticked by the carried-backpack ticker.
    setEnabled(bp, slot, false);
    ctx.require(ctx.give(player, ctx.backpackItem(*bp), 5) == 5, "give backpack");
    auto more = spawnItem(player, "minecraft:bone", 4, 2.6f);
    co_await ctx.ticks(25);
    ctx.check(countOf(*bp, "minecraft:bone") == 3, "disabled magnet doesn't pull");
    ctx.check(findItem(player, more) != nullptr, "bone still on the ground");
    setEnabled(bp, slot, true);
    co_await ctx.ticks(25);
    ctx.check(countOf(*bp, "minecraft:bone") == 7, "enabled magnet in a carried backpack pulls (got "
                  + std::to_string(countOf(*bp, "minecraft:bone")) + ")");
    clearAround(player);
});

ABP_REGISTER_SELFTEST("magnet.xp_to_player", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& player = ctx.player();
    co_await isolate(ctx, player);
    auto bp     = ctx.createBackpack(Tier::Leather);
    int  slot   = ctx.installUpgrade(bp, std::string(kBasicId));
    auto feet   = player.getFeetPos();
    int  before = player.getXpEarnedAtCurrentLevel();
    ExperienceOrb::_spawnOrb(player.getDimensionBlockSource(), player.getLevel(), Vec3{feet.x + 2.5f, feet.y + 0.2f, feet.z}, 3);
    auto orbs = orbsAround(player, 4);
    ctx.require(orbs.size() == 1, "one orb spawned (" + std::to_string(orbs.size()) + ")");
    ctx.check(orbValue(*orbs[0]) == 3, "orb value read (" + std::to_string(orbValue(*orbs[0])) + ")");
    tickMagnet(bp, slot, player);
    // The orb is now at the player; the vanilla pickup takes it within a few ticks.
    for (int i = 0; i < 40 && !orbsAround(player, 4).empty(); ++i) co_await ctx.ticks(1);
    ctx.check(orbsAround(player, 4).empty(), "orb collected");
    ctx.check(player.getXpEarnedAtCurrentLevel() > before, "player got the XP");
    clearAround(player);
});

ABP_REGISTER_SELFTEST("magnet.xp_consumer_hook", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& player = ctx.player();
    co_await isolate(ctx, player);
    auto bp   = ctx.createBackpack(Tier::Leather);
    int  slot = ctx.installUpgrade(bp, std::string(kBasicId));
    auto took = std::make_shared<int>(0);
    auto cap  = std::make_shared<int>(1000);
    auto uuid = bp->uuid();
    addXpConsumer("selftest", 0, [took, cap, uuid](UpgradeContext const& m, Player*, int amount) {
        if (m.backpack().uuid() != uuid) return 0;
        int n  = std::min(amount, *cap);
        *took += n;
        return n;
    });
    struct Cleanup {
        ~Cleanup() { removeXpConsumers("selftest"); }
    } cleanup;
    int  before = player.getXpEarnedAtCurrentLevel();
    auto feet   = player.getFeetPos();
    ExperienceOrb::_spawnOrb(player.getDimensionBlockSource(), player.getLevel(), Vec3{feet.x - 2.5f, feet.y + 0.2f, feet.z}, 5);
    ctx.require(orbsAround(player, 4).size() == 1, "orb spawned");
    tickMagnet(bp, slot, player);
    ctx.check(*took == 5, "consumer got the orb's 5 XP (got " + std::to_string(*took) + ")");
    co_await ctx.ticks(2);
    ctx.check(orbsAround(player, 4).empty(), "orb removed");
    ctx.check(player.getXpEarnedAtCurrentLevel() == before, "player didn't get XP the consumer took");

    // Partial take: the rest becomes a new orb at the player (vanilla pickup, mending).
    *took = 0;
    *cap  = 2;
    ExperienceOrb::_spawnOrb(player.getDimensionBlockSource(), player.getLevel(), Vec3{feet.x - 2.5f, feet.y + 0.2f, feet.z}, 5);
    tickMagnet(bp, slot, player);
    ctx.check(*took == 2, "consumer took 2 (got " + std::to_string(*took) + ")");
    auto rest = orbsAround(player, 4);
    ctx.check(rest.size() == 1 && orbValue(*rest[0]) == 3, "a 3 XP orb is left for the player");
    for (int i = 0; i < 80 && player.getXpEarnedAtCurrentLevel() < before + 3; ++i) co_await ctx.ticks(1);
    ctx.check(
        player.getXpEarnedAtCurrentLevel() == before + 3,
        "player got the remaining 3 XP (xp " + std::to_string(before) + " -> "
            + std::to_string(player.getXpEarnedAtCurrentLevel()) + ")"
    );
    *cap = 1000;

    // XP off: orbs are left alone by the magnet.
    UpgradeContext uc(bp, slot);
    uc.settings()["pickupXp"] = false;
    uc.saveSettings();
    ExperienceOrb::_spawnOrb(player.getDimensionBlockSource(), player.getLevel(), Vec3{feet.x - 2.5f, feet.y + 0.2f, feet.z}, 2);
    *took = 0;
    tickMagnet(bp, slot, player);
    ctx.check(*took == 0, "pickupXp off: consumer not called");
    clearAround(player);
});

ABP_REGISTER_SELFTEST("magnet.settings_menu", [](Context& ctx) {
    auto& mm     = ui::MenuManager::get();
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(Tier::Leather);
    int   slot   = ctx.installUpgrade(bp, std::string(kAdvancedId));
    UpgradeContext uc(bp, slot, &player);
    auto           menu = byId(kAdvancedId)->createSettingsMenu(player, uc);
    ctx.require(menu != nullptr, "settings menu");
    ctx.require(mm.open(player, menu), "open");
    mm.forceShown(player);
    int control = menu->size() - 9;
    ctx.check(control >= kAdvancedSlots, "16 ghost slots fit");
    (void)mm.simulate(player, {Action::Take, {Where::Menu, control + MagnetMenu::kItemsButton}, {Where::Cursor, 0}, 1});
    ctx.check(uc.settings().value("pickupItems", true) == false, "items toggle");
    (void)mm.simulate(player, {Action::Take, {Where::Menu, control + MagnetMenu::kXpButton}, {Where::Cursor, 0}, 1});
    ctx.check(uc.settings().value("pickupXp", true) == false, "XP toggle");
    // Ghost a filter item.
    ctx.require(ctx.give(player, game::ItemConv::make("minecraft:diamond", 1), 4) == 4, "give");
    (void)mm.simulate(player, {Action::Take, {Where::Inventory, 4}, {Where::Cursor, 0}, 1});
    (void)mm.simulate(player, {Action::Place, {Where::Cursor, 0}, {Where::Menu, 15}, 1});
    auto s = Settings::read(uc.settings(), kAdvancedSlots);
    ctx.check(!s.filter.ghosts[15].empty() && s.filter.ghosts[15].id == "minecraft:diamond", "16th filter slot works");
    mm.close(player);
    auto lore = byId(kAdvancedId)->describe(player, uc);
    ctx.check(lore.size() == 4, "describe() lists range, toggles and filter");
});

} // namespace
} // namespace abp::upgrade::magnet
