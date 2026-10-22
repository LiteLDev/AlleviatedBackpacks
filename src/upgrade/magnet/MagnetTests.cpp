// Magnet upgrade selftests: `backpack selftest "magnet.*"`.

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/item/ExperienceOrb.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/PlayerUIContainer.h"
#include "mc/world/actor/player/PlayerUISlot.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/phys/AABB.h"

#include "api/BackpackCapabilities.h"
#include "game/ItemConv.h"
#include "game/PlayerScan.h"
#include "mod/Config.h"
#include "net/NativeScreens.h"
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

constexpr int kBasicSlots    = 9;
constexpr int kAdvancedSlots = 16;

core::ItemStackData data(std::string_view id, int count) { return game::ItemConv::toData(game::ItemConv::make(id, count)); }

int countOf(game::Backpack& bp, std::string_view id) {
    return static_cast<int>(bp.inventory().count([&](core::ItemStackData const& s) { return s.id == id; }));
}

/// Removes every item entity / XP orb near the player (leftovers of other tests).
void clearAround(Player& player, int range = 12) {
    auto& region = player.getDimensionBlockSource();
    for (auto* a : region.getEntities(rangeBox(carrierPos(player), range))) {
        if (a && (a->isType(ActorType::ItemEntity) || a->isType(ActorType::Experience))) a->remove();
    }
}

/// Moves the test player away from the world spawn (where a real client player may stand) and
/// clears loose items there.
ll::coro::CoroTask<> isolate(Context& ctx, Player& player) {
    auto pos = player.getFeetPos();
    pos      = Vec3{std::floor(pos.x) + 24.5f, pos.y, std::floor(pos.z) + 24.5f};
    player.teleport(pos, player.getDimensionId());
    co_await ctx.ticks(3);
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
ActorUniqueID spawnItem(Player& player, std::string_view id, int count, float dx, float dz = 0.0f) {
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
    // spawnItem throws the item in a random direction; keep it where it was put.
    const_cast<Vec3&>(item->getPosDelta()) = Vec3{0.0f, 0.0f, 0.0f};
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
    for (auto* a : player.getDimensionBlockSource().getEntities(rangeBox(carrierPos(player), range), 2.0f, ActorType::ItemEntity)) {
        if (!a || a->mRemoved || !a->isType(ActorType::ItemEntity)) continue;
        auto const& s = static_cast<ItemActor*>(a)->item();
        if (!s.isNull() && s.getTypeName() == id) n += s.mCount;
    }
    return n;
}

std::vector<ExperienceOrb*> orbsAround(Player& player, int range) {
    std::vector<ExperienceOrb*> out;
    for (auto* a : player.getDimensionBlockSource().getEntities(rangeBox(carrierPos(player), range), 2.0f, ActorType::Experience)) {
        if (a && !a->mRemoved && a->isType(ActorType::Experience)) out.push_back(static_cast<ExperienceOrb*>(a));
    }
    return out;
}

/// One magnet run as the carrier (not carried, so the shared clock doesn't race the test).
int tickMagnet(std::shared_ptr<game::Backpack> const& bp, int slot, Player& player) {
    return runMagnet(UpgradeContext(bp, slot, &player), &player, player.getDimensionBlockSource(), carrierPos(player));
}

int xpInTanks(std::shared_ptr<game::Backpack> const& bp) {
    auto h = api::backpack::fluidHandler(bp);
    if (!h) return 0;
    int n = 0;
    for (int i = 0; i < h->tanks(); ++i) {
        auto f = h->fluidInTank(i);
        if (f.id == api::fluids::Experience) n += f.amount;
    }
    return n;
}

/// The native tab data / actions registered for `group` (what the client tab gets and sends).
nlohmann::json tabData(Player& p, UpgradeContext const& ctx, std::string_view group) {
    auto out = nlohmann::json::object();
    for (auto const& e : net::TabDataRegistry::entries()) {
        if (e.upgrade == group && e.data) out.update(e.data(p, ctx));
    }
    return out;
}
bool tabAction(Player& p, UpgradeContext const& ctx, std::string_view group, nlohmann::json const& data) {
    for (auto const& h : net::TabActionRegistry::entries()) {
        if (h.upgrade == group && h.handle && h.handle(p, ctx, data)) return true;
    }
    return false;
}
void setCursor(Player& p, ItemStack const& item) { p.setPlayerUIItem(PlayerUISlot::CursorSelected, item, false); }

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
    ctx.check(canInstall(bp, 1, data(adv->info().itemId, 1), nullptr), "SB has no magnet limit: basic + advanced together");
    auto& s = bp->data().upgrades[0].settings;
    ctx.check(Settings::read(s, 9, false).pickupItems && Settings::read(s, 9, false).pickupXp, "items and XP on by default");
    auto const& cfg = config::get<MagnetConfig>();
    ctx.check(cfg.magnetUpgrade.filterSlots == 9 && cfg.magnetUpgrade.slotsInRow == 3 && cfg.magnetUpgrade.magnetRange == 3, "basic config = SB");
    ctx.check(
        cfg.advancedMagnetUpgrade.filterSlots == 16 && cfg.advancedMagnetUpgrade.slotsInRow == 4
            && cfg.advancedMagnetUpgrade.magnetRange == 5,
        "advanced config = SB"
    );
});

ABP_REGISTER_SELFTEST("magnet.settings_parse", [](Context& ctx) {
    auto s = Settings::read(nlohmann::json::object(), kBasicSlots, false);
    ctx.check(s.pickupItems && s.pickupXp && s.filter.ghosts.size() == kBasicSlots, "defaults");
    ctx.check(s.filter.mode == core::FilterMode::Deny && s.filter.passes(data("minecraft:dirt", 1)), "default: empty block list matches all");
    core::Filter f(kAdvancedSlots);
    f.mode = core::FilterMode::Allow;
    auto j = nlohmann::json{{"pickupItems", false}, {"pickupXp", true}, {"filter", f.toJson()}};
    s      = Settings::read(j, kAdvancedSlots, true);
    ctx.check(!s.pickupItems && s.pickupXp, "toggles read");
    ctx.check(!s.filter.passes(data("minecraft:dirt", 1)), "SC: an empty allow list matches nothing");
    f.setGhost(3, data("minecraft:dirt", 1));
    s = Settings::read(nlohmann::json{{"filter", f.toJson()}}, kAdvancedSlots, true);
    ctx.check(s.filter.passes(data("minecraft:dirt", 1)) && !s.filter.passes(data("minecraft:stone", 1)), "allow list");
});

ABP_REGISTER_SELFTEST("magnet.shared_clock", [](Context& ctx) {
    resetSharedClock();
    std::vector<int> fired;
    for (int t = 1000; t < 1040; ++t) {
        if (advanceSharedClock(static_cast<std::uint64_t>(t))) fired.push_back(t);
    }
    resetSharedClock();
    ctx.require(fired.size() >= 3, "clock fires");
    ctx.check(fired[0] == 1010 && fired[1] == 1021 && fired[2] == 1032, "SC clock: first after 10 ticks, then every 11 ticks");
});

ABP_REGISTER_SELFTEST("magnet.pulls_items_in_range", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& player = ctx.player();
    co_await isolate(ctx, player);
    auto bp   = ctx.createBackpack(Tier::Leather);
    int  slot = ctx.installUpgrade(bp, std::string(kBasicId));
    auto near = spawnItem(player, "minecraft:cobblestone", 20, 2.3f);
    auto edge = spawnItem(player, "minecraft:cobblestone", 3, 3.3f, 3.3f); // corner of the cube: inside
    auto far  = spawnItem(player, "minecraft:cobblestone", 7, 7.0f);
    ctx.require(findItem(player, near) && findItem(player, far) && findItem(player, edge), "items spawned");
    int cd = tickMagnet(bp, slot, player);
    ctx.check(countOf(*bp, "minecraft:cobblestone") == 23, "items in the range cube pulled (got "
                  + std::to_string(countOf(*bp, "minecraft:cobblestone")) + ")");
    ctx.check(cd == kCooldownTicks, "cooldown 10 after pulling");
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
    auto item  = spawnItem(player, "minecraft:oak_log", 5, 4.6f);
    ctx.require(findItem(player, item) != nullptr, "item spawned");
    tickMagnet(basic, bs, player);
    ctx.check(countOf(*basic, "minecraft:oak_log") == 0, "basic magnet (range 3) can't reach 4.6 blocks");
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
    f.mode = core::FilterMode::Allow;
    f.setGhost(0, data("minecraft:diamond", 1));
    uc.settings()["filter"] = f.toJson();
    uc.saveSettings(true);
    auto dirt = spawnItem(player, "minecraft:dirt", 3, 2.0f);
    spawnItem(player, "minecraft:diamond", 2, 2.5f);
    tickMagnet(bp, slot, player);
    ctx.check(countOf(*bp, "minecraft:diamond") == 2, "allow list: diamond pulled");
    ctx.check(countOf(*bp, "minecraft:dirt") == 0 && findItem(player, dirt), "allow list: dirt left on the ground");
    // Walking over items uses the same filter.
    auto           apples = data("minecraft:apple", 4);
    UpgradeContext withPlayer(bp, slot, &player);
    byId(kBasicId)->onPickup(withPlayer, apples);
    ctx.check(apples.count == 4, "onPickup respects the filter");
    auto diamonds = data("minecraft:diamond", 3);
    byId(kBasicId)->onPickup(withPlayer, diamonds);
    ctx.check(diamonds.count == 0 && countOf(*bp, "minecraft:diamond") == 5, "onPickup inserts matching items");

    // "Match Backpack Contents": only items already stored.
    uc.settings()["filter"]["storage"] = true;
    uc.saveSettings(true);
    clearAround(player);
    spawnItem(player, "minecraft:diamond", 1, 2.0f);
    auto stone = spawnItem(player, "minecraft:stone", 1, -2.0f);
    tickMagnet(bp, slot, player);
    ctx.check(countOf(*bp, "minecraft:diamond") == 6, "contents filter: stored item pulled");
    ctx.check(findItem(player, stone) != nullptr, "contents filter: other items stay");
    clearAround(player);
});

ABP_REGISTER_SELFTEST("magnet.pulls_thrown_items_but_respects_tags", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& player = ctx.player();
    co_await isolate(ctx, player);
    auto bp   = ctx.createBackpack(Tier::Leather);
    int  slot = ctx.installUpgrade(bp, std::string(kBasicId));
    auto own  = spawnItem(player, "minecraft:iron_ingot", 1, -2.3f);
    if (auto* o = findItem(player, own)) {
        o->setSourceEntity(&player);
        o->age()         = 0;
        o->pickupDelay() = 40; // a freshly thrown item
    }
    auto never = spawnItem(player, "minecraft:gold_ingot", 1, 2.3f);
    if (auto* n = findItem(player, never)) n->pickupDelay() = 32767;
    auto tagged = spawnItem(player, "minecraft:emerald", 1, 0.0f, 2.3f);
    if (auto* t = findItem(player, tagged)) t->addTag(kPreventRemoteMovement);
    auto machine = spawnItem(player, "minecraft:coal", 1, 0.0f, -2.3f);
    if (auto* m = findItem(player, machine)) {
        m->addTag(kPreventRemoteMovement);
        m->addTag(kAllowMachineMovement);
    }
    tickMagnet(bp, slot, player);
    ctx.check(countOf(*bp, "minecraft:iron_ingot") == 1, "items the carrier just threw are pulled (SB)");
    ctx.check(countOf(*bp, "minecraft:gold_ingot") == 0, "infinite pickup delay is skipped");
    ctx.check(countOf(*bp, "minecraft:emerald") == 0, "PreventRemoteMovement is skipped");
    ctx.check(countOf(*bp, "minecraft:coal") == 0, "a carried magnet ignores AllowMachineRemoteMovement");
    // A placed backpack (no carrier) honours AllowMachineRemoteMovement.
    auto pos = carrierPos(player);
    (void)runMagnet(UpgradeContext(bp, slot), nullptr, player.getDimensionBlockSource(), pos);
    ctx.check(countOf(*bp, "minecraft:coal") == 1, "placed magnet takes AllowMachineRemoteMovement items");
    ctx.check(countOf(*bp, "minecraft:emerald") == 0, "placed magnet skips PreventRemoteMovement items");
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
    int  cd    = tickMagnet(bp, slot, player);
    ctx.check(cd == kCooldownTicks, "something went in: cooldown 10");
    ctx.check(countOf(*bp, "minecraft:stone") == 64, "the part that fits is taken");
    ctx.check(findItem(player, stone) == nullptr && groundCount(player, "minecraft:stone") == 6, "the rest (6) stays on the ground");
    cd = tickMagnet(bp, slot, player);
    ctx.check(cd == kFullCooldownTicks, "nothing fits: cooldown 40");
    UpgradeContext uc(bp, slot);
    uc.settings()["pickupItems"] = false;
    uc.saveSettings();
    clearAround(player);
    ctx.check(tickMagnet(bp, slot, player) == kFullCooldownTicks, "pickupItems off: cooldown 40");
    clearAround(player);
});

ABP_REGISTER_SELFTEST("magnet.carried_and_disable", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& player = ctx.player();
    co_await isolate(ctx, player);
    auto bp   = ctx.createBackpack(Tier::Leather);
    int  slot = ctx.installUpgrade(bp, std::string(kBasicId));
    setEnabled(bp, slot, false);
    ctx.require(ctx.give(player, ctx.backpackItem(*bp), 5) == 5, "give backpack");
    auto more = spawnItem(player, "minecraft:bone", 4, 2.6f);
    co_await ctx.ticks(25);
    ctx.check(countOf(*bp, "minecraft:bone") == 0, "disabled magnet doesn't pull");
    ctx.check(findItem(player, more) != nullptr, "bone still on the ground");
    setEnabled(bp, slot, true);
    co_await ctx.ticks(25);
    ctx.check(countOf(*bp, "minecraft:bone") == 4, "enabled magnet in a carried backpack pulls (got "
                  + std::to_string(countOf(*bp, "minecraft:bone")) + ")");
    clearAround(player);
});

ABP_REGISTER_SELFTEST("magnet.placed_cooldown", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& player = ctx.player();
    co_await isolate(ctx, player);
    auto  bp   = ctx.createBackpack(Tier::Leather);
    int   slot = ctx.installUpgrade(bp, std::string(kBasicId));
    auto* type = byId(kBasicId);
    auto  pos  = carrierPos(player);
    auto& reg  = player.getDimensionBlockSource();
    PlacedTickContext tick{UpgradeContext(bp, slot), reg, static_cast<int>(player.getDimensionId()), pos.x, pos.y, pos.z, 2, 2};
    spawnItem(player, "minecraft:stick", 2, 1.5f);
    type->onPlacedTick(tick);
    ctx.check(countOf(*bp, "minecraft:stick") == 2, "placed magnet pulls");
    spawnItem(player, "minecraft:stick", 3, 1.5f);
    type->onPlacedTick(tick);
    ctx.check(countOf(*bp, "minecraft:stick") == 2, "placed magnet waits for its cooldown");
    co_await ctx.ticks(11);
    type->onPlacedTick(tick);
    ctx.check(countOf(*bp, "minecraft:stick") == 5, "after 10 ticks it pulls again");
    clearAround(player);
});

ABP_REGISTER_SELFTEST("magnet.xp_needs_tank", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& player = ctx.player();
    co_await isolate(ctx, player);
    auto bp   = ctx.createBackpack(Tier::Iron); // 2 upgrade slots
    int  slot = ctx.installUpgrade(bp, std::string(kBasicId));
    auto feet = player.getFeetPos();
    ExperienceOrb::_spawnOrb(player.getDimensionBlockSource(), player.getLevel(), Vec3{feet.x + 2.5f, feet.y + 0.2f, feet.z}, 3);
    auto orbs = orbsAround(player, 4);
    ctx.require(orbs.size() == 1, "one orb spawned (" + std::to_string(orbs.size()) + ")");
    ctx.check(orbValue(*orbs[0]) == 3, "orb value read");
    ctx.check(!canFillStorageWithXp(bp), "no tank: no XP");
    tickMagnet(bp, slot, player);
    ctx.check(orbsAround(player, 4).size() == 1, "without a tank orbs are left alone");
    ctx.installUpgrade(bp, "tank");
    ctx.require(canFillStorageWithXp(bp), "a tank accepts liquid experience");
    tickMagnet(bp, slot, player);
    ctx.check(orbsAround(player, 4).empty(), "orb taken");
    ctx.check(xpInTanks(bp) == 60, "3 points = 60 mB in the tank (got " + std::to_string(xpInTanks(bp)) + ")");

    // pickupXp off: orbs stay.
    UpgradeContext uc(bp, slot);
    uc.settings()["pickupXp"] = false;
    uc.saveSettings();
    ExperienceOrb::_spawnOrb(player.getDimensionBlockSource(), player.getLevel(), Vec3{feet.x - 2.5f, feet.y + 0.2f, feet.z}, 2);
    tickMagnet(bp, slot, player);
    ctx.check(orbsAround(player, 4).size() == 1, "pickupXp off: orb left alone");
    clearAround(player);
});

ABP_REGISTER_SELFTEST("magnet.xp_overflow_respawns", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& player = ctx.player();
    co_await isolate(ctx, player);
    auto bp   = ctx.createBackpack(Tier::Iron);
    int  slot = ctx.installUpgrade(bp, std::string(kBasicId));
    ctx.installUpgrade(bp, "tank");
    auto h = api::backpack::fluidHandler(bp);
    ctx.require(h != nullptr, "tank handler");
    int cap = h->tankCapacity(0);
    h->fill(api::FluidStack{std::string(api::fluids::Experience), cap - 40, {}}, false); // room for 2 points
    auto feet = player.getFeetPos();
    ExperienceOrb::_spawnOrb(player.getDimensionBlockSource(), player.getLevel(), Vec3{feet.x + 2.5f, feet.y + 0.2f, feet.z}, 7);
    tickMagnet(bp, slot, player);
    ctx.check(xpInTanks(bp) == cap, "tank filled up");
    auto rest = orbsAround(player, 4);
    int  sum  = 0;
    for (auto* o : rest) sum += orbValue(*o);
    ctx.check(sum == 5, "the 5 points that did not fit are spawned again (got " + std::to_string(sum) + ")");
    ctx.check(!canFillStorageWithXp(bp), "full tank: no more XP pickup");
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
    ctx.require(ctx.give(player, game::ItemConv::make("minecraft:diamond", 1), 4) == 4, "give");
    (void)mm.simulate(player, {Action::Take, {Where::Inventory, 4}, {Where::Cursor, 0}, 1});
    (void)mm.simulate(player, {Action::Place, {Where::Cursor, 0}, {Where::Menu, 15}, 1});
    auto s = Settings::read(uc.settings(), kAdvancedSlots, true);
    ctx.check(!s.filter.ghosts[15].empty() && s.filter.ghosts[15].id == "minecraft:diamond", "16th filter slot works");
    mm.close(player);
});

ABP_REGISTER_SELFTEST("magnet.native_tab", [](Context& ctx) {
    auto& p   = ctx.player();
    auto  bp  = ctx.createBackpack(Tier::Iron);
    int   b   = ctx.installUpgrade(bp, std::string(kBasicId));
    int   a   = ctx.installUpgrade(bp, std::string(kAdvancedId));
    UpgradeContext basic(bp, b, &p), adv(bp, a, &p);
    auto           t = tabData(p, basic, "magnet");
    ctx.check(t.value("title", "") == "Magnet" && t.value("tooltip", "") == "Magnet Settings", "SB tab texts: " + t.value("title", ""));
    ctx.check(tabData(p, adv, "magnet").value("title", "") == "Adv. Magnet", "advanced title");
    ctx.check(t.value("pickupItems", false) && t.value("pickupXp", false), "toggles on");
    auto const& f = t["filter"];
    ctx.check(f.value("type", "") == "contents" && f.value("slots", 0) == 9 && f.value("perRow", 0) == 3 && !f.value("advanced", true),
              "basic contents filter, 9 slots in rows of 3");
    auto fa = tabData(p, adv, "magnet")["filter"];
    ctx.check(fa.value("slots", 0) == 16 && fa.value("perRow", 0) == 4 && fa.value("advanced", false), "advanced: 16 slots, rows of 4");
    ctx.check(f.value("typeState", -1) == 1, "default: block list");
    ctx.check(tabAction(p, basic, "magnet", {{"op", "pickupItems"}}), "toggle items");
    ctx.check(!Settings::read(basic.settings(), 9, false).pickupItems, "items off");
    ctx.check(tabAction(p, basic, "magnet", {{"op", "pickupXp"}}), "toggle xp");
    ctx.check(!Settings::read(basic.settings(), 9, false).pickupXp, "xp off");
    ctx.check(tabAction(p, basic, "magnet", {{"filter", "filter"}, {"op", "type"}}), "type button");
    ctx.check(tabData(p, basic, "magnet")["filter"].value("typeState", -1) == 2, "BLOCK -> MATCH BACKPACK CONTENTS");
    ctx.check(Settings::read(basic.settings(), 9, false).filter.filterByStorage, "stored as contents filter");
    tabAction(p, basic, "magnet", {{"filter", "filter"}, {"op", "type"}});
    setCursor(p, game::ItemConv::make("minecraft:diamond", 3));
    ctx.check(tabAction(p, basic, "magnet", {{"filter", "filter"}, {"op", "slot"}, {"index", 2}}), "ghost slot");
    setCursor(p, ItemStack::EMPTY_ITEM());
    auto s = Settings::read(basic.settings(), 9, false);
    ctx.check(s.filter.ghosts[2].id == "minecraft:diamond" && s.filter.mode == core::FilterMode::Allow, "allow list with a diamond");
});

} // namespace
} // namespace abp::upgrade::magnet
