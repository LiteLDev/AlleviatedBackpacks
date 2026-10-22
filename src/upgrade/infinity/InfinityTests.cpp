// Selftests for the Infinity upgrades: infinity.*

#include <climits>

#include "mc/scripting/modules/minecraft/ScriptFacing.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/world/PlayerUIContainer.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/player/PlayerUISlot.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/GameType.h"

#include "feature/placed/PlacedBackpacks.h"
#include "feature/placed/TestSite.h"
#include "game/ItemConv.h"
#include "net/Companion.h"
#include "net/NativeScreens.h"
#include "selftest/SelfTest.h"
#include "ui/menu/BackpackMenu.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/infinity/Infinity.h"

namespace abp::upgrade::infinity {
namespace {

using selftest::Context;
using Where  = ui::MenuManager::SlotAddress::Where;
using Action = ui::MenuManager::ActionType;

core::ItemStackData data(std::string_view id, int count) { return game::ItemConv::toData(game::ItemConv::make(id, count)); }
core::ItemStackData upgradeItem(std::string_view id) {
    return game::ItemConv::toData(game::ItemConv::make(byId(id)->info().itemId, 1));
}

/// Sets the player's command permission level for the test and restores it afterwards.
struct Permission {
    Player&                player;
    ::CommandPermissionLevel old;
    Permission(Player& p, ::CommandPermissionLevel level) : player(p), old(p.getCommandPermissionLevel()) {
        p.setCommandPermissions(level);
    }
    ~Permission() { player.setCommandPermissions(old); }
};

ABP_REGISTER_SELFTEST("infinity.conflicts_both_ways", [](Context& ctx) {
    auto& p = ctx.player();
    for (auto id : {kAdminId, kSurvivalId}) {
        auto        name = std::string(id);
        auto        bp   = ctx.createBackpack(core::Tier::Iron);
        std::string reason;
        ctx.installUpgrade(bp, "pickup");
        ctx.check(!canInstall(bp, 1, upgradeItem(id), &p, &reason), name + ": refused next to another upgrade");
        ctx.check(reason == "All upgrade slots need to be empty", name + ": SB message, got '" + reason + "'");

        auto empty = ctx.createBackpack(core::Tier::Iron);
        int  slot  = ctx.installUpgrade(empty, name);
        ctx.check(hasActive(*empty, name), name + " installed");
        reason.clear();
        ctx.check(!canInstall(empty, slot + 1, upgradeItem("pickup"), &p, &reason), name + ": no other upgrade can be added");
        ctx.check(reason == "No upgrade can be added", name + ": SB message, got '" + reason + "'");
        auto other = id == kAdminId ? kSurvivalId : kAdminId;
        ctx.check(!canInstall(empty, slot + 1, upgradeItem(other), &p), name + ": not even the other infinity upgrade");
        ctx.check(!byId(name)->info().toggleable && !byId(name)->info().hasSettings, name + ": no switch, no tab");
    }
});

ABP_REGISTER_SELFTEST("infinity.slots_never_deplete", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& mm = ui::MenuManager::get();
    auto& p  = ctx.player();
    Permission op(p, CommandPermissionLevel::GameDirectors);
    auto  bp = ctx.createBackpack(core::Tier::Leather);
    ctx.installUpgrade(bp, std::string(kAdminId));
    auto& inv = bp->inventory();
    core::AccessContext asPlayer{core::Source::Player, &p};
    ctx.require(inv.insertInto(0, data("minecraft:diamond", 1), asPlayer).inserted == 1, "an admin sets an infinite diamond");
    ctx.check(inv.isInfinite(0) && !inv.isInfinite(1), "non-empty slots are infinite, empty ones not");
    ctx.check(inv.slotLimit(0, inv.at(0)) == INT_MAX, "slot limit is Integer.MAX_VALUE");
    ctx.check(inv.insertInto(0, data("minecraft:diamond", 5), asPlayer).inserted == 0, "a non-empty slot takes nothing");
    ctx.check(inv.insert(data("minecraft:diamond", 5), asPlayer).inserted == 5 && inv.at(1).count == 5, "insert goes to an empty slot");

    // Taking a stack through the GUI never depletes the slot.
    ctx.require(ui::BackpackMenu::openBackpack(p, bp), "open backpack");
    mm.forceShown(p);
    for (int i = 0; i < 3; ++i) {
        ctx.check(mm.simulate(p, {Action::Take, {Where::Menu, 0}, {Where::Inventory, 9 + i}, 64}), "take a stack " + std::to_string(i));
    }
    co_await ctx.ticks(1);
    int got = 0;
    for (int i = 0; i < 3; ++i) got += p.getInventory().getItem(9 + i).mCount;
    ctx.check(got == 3 * 64, "three full stacks out of one diamond, got " + std::to_string(got));
    ctx.check(inv.at(0).count == 1 && inv.at(0).id == "minecraft:diamond", "the slot still holds its diamond");
    // Putting the stack back is refused (the slot is not empty), nothing is lost or doubled.
    ctx.check(!mm.simulate(p, {Action::Place, {Where::Inventory, 9}, {Where::Menu, 0}, 64}) || inv.at(0).count == 1, "slot unchanged");
    mm.close(p);
});

ABP_REGISTER_SELFTEST("infinity.permissions", [](Context& ctx) {
    auto& p = ctx.player();
    core::AccessContext asPlayer{core::Source::Player, &p};
    {
        Permission member(p, CommandPermissionLevel::Any);
        auto       bp   = ctx.createBackpack(core::Tier::Leather);
        int        slot = ctx.installUpgrade(bp, std::string(kAdminId));
        ctx.check(bp->inventory().insertInto(0, data("minecraft:stone", 1), asPlayer).inserted == 0, "admin variant: members can't set items");
        ctx.check(bp->inventory().insert(data("minecraft:stone", 1), core::AccessContext{core::Source::Pickup, &p}).inserted == 0, "no pickups");
        ctx.check(bp->inventory().insert(data("minecraft:stone", 1), core::AccessContext{core::Source::Automation}).inserted == 0, "no automation");
        std::string reason;
        ctx.check(!remove(bp, slot, &p, &reason).has_value(), "admin variant: members can't remove it");
        ctx.check(reason == "Only admins can remove Infinity Upgrade", "SB message, got '" + reason + "'");
        Permission op(p, CommandPermissionLevel::GameDirectors);
        ctx.check(bp->inventory().insertInto(0, data("minecraft:stone", 1), asPlayer).inserted == 1, "admins can");
        ctx.check(remove(bp, slot, &p, &reason).has_value(), "admins remove it");
        ctx.check(!bp->inventory().isInfinite(0), "slots are normal again");
    }
    {
        Permission member(p, CommandPermissionLevel::Any);
        auto       bp   = ctx.createBackpack(core::Tier::Leather);
        int        slot = ctx.installUpgrade(bp, std::string(kSurvivalId));
        ctx.check(bp->inventory().insertInto(0, data("minecraft:stone", 1), asPlayer).inserted == 1, "survival variant: everybody sets items");
        ctx.check(bp->inventory().insertInto(1, data("minecraft:stone", 1), core::AccessContext{core::Source::Pickup, &p}).inserted == 0, "still no pickups");
        std::string reason;
        ctx.check(remove(bp, slot, &p, &reason).has_value(), "survival variant: everybody removes it: " + reason);
    }
});

ABP_REGISTER_SELFTEST("infinity.placed_admin_only", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& sp = ctx.spawnPlayer();
    co_await placed::test::toGround(ctx, sp);
    placed::test::Site site(sp);
    auto pos = site.at(0, 1);
    site.set(pos, "minecraft:air");
    auto bp = ctx.createBackpack(core::Tier::Copper);
    ctx.installUpgrade(bp, std::string(kAdminId));
    site.touch(pos);
    auto item = ctx.backpackItem(*bp);
    ctx.require(placed::PlacedBackpacks::get().place(site.region(), pos, item, 2) == placed::PlacedBackpacks::PlaceResult::Ok, "place");

    Permission member(sp, CommandPermissionLevel::Any);
    ctx.check(!mayTakePlaced(sp, *bp), "members may not take it");
    sp.setPlayerGameType(GameType::Creative);
    (void)sp.simulateDestroyBlock(pos, ScriptModuleMinecraft::ScriptFacing::Up);
    co_await ctx.ticks(5);
    ctx.check(placed::PlacedBackpacks::isBackpackBlock(site.region().getBlock(pos)), "a member can't break it");
    ctx.check(placed::PlacedBackpacks::get().findUuid(bp->uuid()).has_value(), "still placed");
    {
        Permission op(sp, CommandPermissionLevel::GameDirectors);
        ctx.check(mayTakePlaced(sp, *bp), "admins may");
        (void)sp.simulateDestroyBlock(pos, ScriptModuleMinecraft::ScriptFacing::Up);
        for (int i = 0; i < 20 && !site.region().getBlock(pos).isAir(); ++i) co_await ctx.ticks(1);
        ctx.check(site.region().getBlock(pos).isAir(), "an admin breaks it");
        ctx.check(site.collectBackpackDrops(pos, bp->uuid()) == 1, "dropped once");
    }
    sp.setPlayerGameType(GameType::Survival);

    auto survival = ctx.createBackpack(core::Tier::Copper);
    ctx.installUpgrade(survival, std::string(kSurvivalId));
    ctx.check(mayTakePlaced(sp, *survival), "survival variant: everybody may take it");
});

ABP_REGISTER_SELFTEST("infinity.native_shows_infinite", [](Context& ctx) {
    auto& p  = ctx.player();
    auto  bp = ctx.createBackpack(core::Tier::Leather);
    int   slot = ctx.installUpgrade(bp, std::string(kSurvivalId));
    bp->inventory().set(0, data("minecraft:apple", 1));
    net::setCompanion(p, net::CompanionInfo{"test", {"native_screen"}});
    net::setTestSink([](Player&, std::string_view, nlohmann::json const&) {});
    ctx.require(ui::BackpackMenu::openBackpack(p, bp), "open backpack");
    ui::MenuManager::get().forceShown(p);
    auto const* m = net::sentModel(p);
    ctx.require(m != nullptr, "native model sent");
    auto const& tab = m->upgrades[static_cast<std::size_t>(slot)].tab;
    ctx.check(tab.value("infinite", false), "the client is told to draw infinite counts: " + tab.dump());
    ctx.check(!m->upgrades[static_cast<std::size_t>(slot)].toggleable, "no on/off switch");
    ui::MenuManager::get().close(p);
    net::setTestSink(nullptr);
    net::setCompanion(p, std::nullopt);
});

} // namespace
} // namespace abp::upgrade::infinity
