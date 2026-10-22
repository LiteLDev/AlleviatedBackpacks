// Selftests of the Mob Catcher upgrades (`/backpack selftest "mobcatcher.*"`).

#include <algorithm>
#include <cmath>

#include "mc/deps/shared_types/legacy/Difficulty.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/PlayerUIContainer.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/player/PlayerUISlot.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/phys/HitResult.h"
#include "mc/world/phys/HitResultType.h"
#include "mc/scripting/modules/minecraft/ScriptFacing.h"

#include "feature/tiers/Tiers.h"
#include "game/Backpack.h"
#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "mod/Config.h"
#include "net/Companion.h"
#include "net/NativeScreens.h"
#include "selftest/SelfTest.h"
#include "ui/menu/BackpackMenu.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/IUpgrade.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/mobcatcher/MobCatcher.h"

namespace abp::upgrade::mobcatcher {
namespace {

using core::Tier;
using selftest::Context;

struct ConfigGuard {
    MobCatcherConfig saved = abp::config::get<MobCatcherConfig>();
    ConfigGuard()          = default;
    ConfigGuard(ConfigGuard const&)            = delete;
    ConfigGuard& operator=(ConfigGuard const&) = delete;
    ~ConfigGuard() { abp::config::mut<MobCatcherConfig>() = saved; }
    MobCatcherConfig& cfg() { return abp::config::mut<MobCatcherConfig>(); }
};

/// Hostile mobs can't spawn in peaceful: raises the difficulty for the test, restores it after.
struct DifficultyGuard {
    Level&                          level;
    SharedTypes::Legacy::Difficulty saved;
    explicit DifficultyGuard(Level& l) : level(l), saved(l.getDifficulty()) {
        if (saved == SharedTypes::Legacy::Difficulty::Peaceful) level.setDifficulty(SharedTypes::Legacy::Difficulty::Normal);
    }
    DifficultyGuard(DifficultyGuard const&)            = delete;
    DifficultyGuard& operator=(DifficultyGuard const&) = delete;
    ~DifficultyGuard() { level.setDifficulty(saved); }
};

/// Moves the test player onto the ground (test players may spawn above an unset world spawn).
ll::coro::CoroTask<> groundPlayer(Context& ctx) {
    auto&    p   = ctx.player();
    auto&    bs  = p.getDimensionBlockSource();
    auto     pos = p.getPosition();
    BlockPos column{static_cast<int>(std::floor(pos.x)), 0, static_cast<int>(std::floor(pos.z))};
    if (pos.y < bs.getMaxHeight() && bs.hasChunksAt(column, 2, false)) co_return;
    p.teleport(Vec3{pos.x, 0.0f, pos.z}, p.getDimensionId());
    for (int i = 0; i < 100 && !bs.hasChunksAt(column, 2, false); ++i) co_await ctx.ticks(2);
    ctx.require(bs.hasChunksAt(column, 2, false), "chunks around the test player did not load");
    auto y = bs.getAboveTopSolidBlock(column.x, column.z, false, false);
    p.teleport(Vec3{pos.x, static_cast<float>(y), pos.z}, p.getDimensionId());
    co_await ctx.ticks(2);
}

/// Moves the test player `dx` blocks east onto the ground (away from what other tests left behind).
ll::coro::CoroTask<> isolatePlayer(Context& ctx, float dx) {
    auto&    p   = ctx.player();
    auto&    bs  = p.getDimensionBlockSource();
    auto     pos = p.getPosition();
    BlockPos column{static_cast<int>(std::floor(pos.x + dx)), 0, static_cast<int>(std::floor(pos.z))};
    p.teleport(Vec3{pos.x + dx, pos.y, pos.z}, p.getDimensionId());
    for (int i = 0; i < 100 && !bs.hasChunksAt(column, 2, false); ++i) co_await ctx.ticks(2);
    ctx.require(bs.hasChunksAt(column, 2, false), "chunks around the isolated spot did not load");
    auto y = bs.getAboveTopSolidBlock(column.x, column.z, false, false);
    p.teleport(Vec3{pos.x + dx, static_cast<float>(y), pos.z}, p.getDimensionId());
    co_await ctx.ticks(2);
}

Mob* spawnMob(Context& ctx, std::string const& id, float dx) {
    auto& player = ctx.player();
    auto  p      = player.getPosition();
    auto& bs     = player.getDimensionBlockSource();
    float x      = p.x + dx;
    auto  y      = bs.getAboveTopSolidBlock(static_cast<int>(std::floor(x)), static_cast<int>(std::floor(p.z)), false, false);
    auto* mob    = player.getLevel().getSpawner().spawnMob(bs, ActorDefinitionIdentifier(id), nullptr, Vec3{x, static_cast<float>(y), p.z}, false, true, false);
    ctx.require(mob != nullptr, "could not spawn " + id);
    return mob;
}

int install(Context& ctx, std::shared_ptr<game::Backpack> const& bp, char const* id) {
    int slot = ctx.installUpgrade(bp, id);
    ctx.require(slot >= 0, std::string("install ") + id);
    return slot;
}

bool mobExists(Player& player, ActorUniqueID id) { return player.getLevel().fetchEntity(id, false) != nullptr; }

ABP_REGISTER_SELFTEST("mobcatcher.registered", [](Context& ctx) {
    auto* basic    = upgrade::byId(kBasicId);
    auto* advanced = upgrade::byId(kAdvancedId);
    ctx.require(basic && advanced, "both catchers registered");
    ctx.check(basic->info().itemId == "alleviatedbackpacks:mob_catcher_upgrade", "basic item id");
    ctx.check(advanced->info().itemId == "alleviatedbackpacks:advanced_mob_catcher_upgrade", "advanced item id");
    ctx.check(basic->info().groupOrId() == advanced->info().groupOrId(), "one group");
    ctx.check(!basic->info().toggleable && !advanced->info().toggleable, "can't be switched off (SB canBeDisabled false)");
    ctx.check(!basic->info().hasSettings && !advanced->info().hasSettings, "no settings tab (SB hideSettingsTab)");
    ctx.check(config().basicMaxSlotCost == 18 && config().advancedMaxSlotCost == 72, "SB default limits");
    ctx.check(
        tiers::isNextTierUpgrade("alleviatedbackpacks:mob_catcher_upgrade", "alleviatedbackpacks:advanced_mob_catcher_upgrade"),
        "the advanced recipe keeps the basic catcher's components (SC upgrade_next_tier)"
    );
});

ABP_REGISTER_SELFTEST("mobcatcher.one_per_backpack", [](Context& ctx) {
    auto bp = ctx.createBackpack(Tier::Netherite);
    install(ctx, bp, kBasicId);
    std::string why;
    auto        item = game::ItemConv::toData(game::ItemConv::make(upgrade::byId(kAdvancedId)->info().itemId, 1));
    ctx.check(!upgrade::canInstall(bp, 1, item, &ctx.player(), &why), "a second catcher is refused");
    ctx.check(why.find("Only one Mob Catcher Upgrade") != std::string::npos, "SB message: " + why);
    ctx.check(upgrade::replace(bp, 0, item, &ctx.player(), &why).has_value(), "but basic -> advanced swaps in place");
    ctx.check(bestCatcher(*bp) == std::optional<bool>(true), "advanced now");
});

ABP_REGISTER_SELFTEST("mobcatcher.capture_cow", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await groundPlayer(ctx);
    auto& p  = ctx.player();
    auto  bp = ctx.createBackpack(Tier::Iron); // 54 slots, 9 columns, 6 rows
    install(ctx, bp, kBasicId);
    auto* cow = spawnMob(ctx, "minecraft:cow", 2);
    auto  id  = cow->getOrCreateUniqueID();
    auto  r   = capture(p, bp, *cow, false);
    ctx.require(r.success, "captured: " + r.message);
    ctx.check(r.message == "Captured Cow", "message names the mob: " + r.message);
    auto mobs = capturedMobs(*bp);
    ctx.require(mobs.size() == 1, "one mob stored");
    auto const& m = mobs[0];
    ctx.check(m.entityType == "minecraft:cow" && !m.hostile, "passive cow");
    ctx.check(m.slotCost == 10, "cost = 10 (10/2 + 10/2), got " + std::to_string(m.slotCost));
    ctx.check(m.width * m.height >= 10 && m.slot == 0, "footprint at the first empty rectangle: " + std::to_string(m.width) + "x" + std::to_string(m.height));
    ctx.check(m.currentHealth == 10 && m.maxHealth == 10, "health recorded");
    ctx.check(!m.nbt.empty() && m.look.contains("data"), "entity NBT and look data saved");
    co_await ctx.ticks(2);
    ctx.check(!mobExists(p, id), "the cow is gone from the world");

    // The footprint is blocked for every insert, sorting leaves it alone.
    auto& inv   = bp->inventory();
    auto  stone = game::ItemConv::toData(game::ItemConv::make("minecraft:stone", 1));
    ctx.check(inv.isSlotBlocked(0) && inv.isSlotBlocked(m.width - 1) && inv.isSlotBlocked((m.height - 1) * 9), "footprint corners blocked");
    ctx.check(!inv.isSlotBlocked(m.width), "the slot right of it is free");
    ctx.check(inv.insertInto(0, stone.withCount(5), core::AccessContext{core::Source::Internal}).inserted == 0, "no item into a mob slot");
    auto ins = inv.insert(stone.withCount(64), core::AccessContext{core::Source::Player});
    ctx.check(ins.inserted == 64 && inv.at(m.width).count == 64, "automatic placement skips the mob");
    inv.set(53, stone.withCount(3));
    inv.sort([](core::ItemStackData const& a, core::ItemStackData const& b) { return a.id < b.id; });
    ctx.check(inv.at(0).empty() && inv.at(1).empty(), "sorting keeps the mob slots empty");
});

ABP_REGISTER_SELFTEST("mobcatcher.capture_by_interaction", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await groundPlayer(ctx);
    auto& p  = static_cast<SimulatedPlayer&>(ctx.player()); // ctx.player() is a SimulatedPlayer
    auto  bp = ctx.createBackpack(Tier::Iron);
    install(ctx, bp, kBasicId);
    int hand = p.getSelectedItemSlot();
    ctx.give(p, ctx.backpackItem(*bp), hand);
    auto* cow = spawnMob(ctx, "minecraft:cow", 2);
    auto  id  = cow->getOrCreateUniqueID();
    p.simulateLookAt(cow->getPosition());
    // Without sneaking the interaction is left to vanilla.
    p.simulateInteract(*cow);
    co_await ctx.ticks(2);
    ctx.check(capturedMobs(*bp).empty(), "no capture without sneaking");
    ctx.check(p.simulateSneaking(), "sneaking");
    if (auto* again = static_cast<Mob*>(p.getLevel().fetchEntity(id, false))) p.simulateInteract(*again);
    co_await ctx.ticks(3);
    p.simulateStopSneaking();
    ctx.check(capturedMobs(*bp).size() == 1, "sneak + use with the backpack in hand captures");
    ctx.check(!mobExists(p, id), "cow removed");
});

ABP_REGISTER_SELFTEST("mobcatcher.hostile_rules", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await groundPlayer(ctx);
    auto&           p = ctx.player();
    DifficultyGuard difficulty(p.getLevel());
    auto*           zombie = spawnMob(ctx, "minecraft:zombie", 3);
    ctx.check(isHostile(*zombie), "zombies are hostile");

    auto basic = ctx.createBackpack(Tier::Netherite);
    install(ctx, basic, kBasicId);
    auto r = capture(p, basic, *zombie, false);
    ctx.check(!r.success && r.message.find("Hostile mobs require Advanced") != std::string::npos, "basic refuses hostiles: " + r.message);

    auto small = ctx.createBackpack(Tier::Leather); // 3 rows: a full-health zombie (cost 40) never fits
    install(ctx, small, kAdvancedId);
    r = capture(p, small, *zombie, true);
    ctx.check(!r.success && r.message.find("No empty ") != std::string::npos && r.message.find("slot area available") != std::string::npos, "no room: " + r.message);

    auto big = ctx.createBackpack(Tier::Netherite); // 12 x 10
    install(ctx, big, kAdvancedId);
    r = capture(p, big, *zombie, true);
    ctx.require(r.success, "advanced catches the zombie: " + r.message);
    auto mobs = capturedMobs(*big);
    ctx.require(mobs.size() == 1, "stored");
    ctx.check(mobs[0].hostile && mobs[0].slotCost == 40, "hostile, cost (10 + 10) * 2 = 40, got " + std::to_string(mobs[0].slotCost));
    auto expected = footprintFor(mobs[0].look.value("bbWidth", 0.0), mobs[0].look.value("bbHeight", 0.0), 40);
    ctx.check(mobs[0].width == expected.width && mobs[0].height == expected.height, "SB footprint of the zombie's box: " + std::to_string(mobs[0].width) + "x" + std::to_string(mobs[0].height));

    // Swapping to basic is refused while a hostile mob is stored; removal is refused too.
    std::string why;
    auto        bctx = UpgradeContext(big, 0, &p);
    auto        basicItem = game::ItemConv::toData(game::ItemConv::make(upgrade::byId(kBasicId)->info().itemId, 1));
    ctx.check(!upgrade::byId(kAdvancedId)->canReplaceWith(bctx, basicItem, why), "advanced -> basic refused");
    ctx.check(why.find("require Advanced Mob Catcher") != std::string::npos, "SB message: " + why);
    ctx.check(!upgrade::remove(big, 0, &p, &why).has_value(), "removal refused");
    ctx.check(why.find("Release captured mobs") != std::string::npos, "SB message: " + why);
    ctx.check(
        static_cast<int>(upgrade::byId(kAdvancedId)->refusalStorageSlots(bctx).size()) == mobs[0].width * mobs[0].height,
        "the mob's slots are highlighted"
    );
});

ABP_REGISTER_SELFTEST("mobcatcher.refusals", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await groundPlayer(ctx);
    auto&       p = ctx.player();
    ConfigGuard guard;
    auto        bp = ctx.createBackpack(Tier::Netherite);
    install(ctx, bp, kAdvancedId);

    auto& other = ctx.spawnPlayer();
    auto  r     = capture(p, bp, other, true);
    ctx.check(!r.success && r.message == "Players cannot be captured", "players: " + r.message);

    auto* cow = spawnMob(ctx, "minecraft:cow", 2);
    guard.cfg().entityBlockList = {"minecraft:cow"};
    r = capture(p, bp, *cow, true);
    ctx.check(!r.success && r.message == "That mob is blocked from capture", "block list: " + r.message);
    guard.cfg().entityBlockList = {"#cow"};
    r = capture(p, bp, *cow, true);
    ctx.check(!r.success && r.message == "That mob is blocked from capture", "block list by family: " + r.message);
    guard.cfg().entityBlockList = {};

    guard.cfg().hostileOverrides = {"minecraft:cow"};
    ctx.check(isHostile(*cow), "hostile override");
    guard.cfg().passiveOverrides = {"minecraft:cow"};
    ctx.check(!isHostile(*cow), "passive override wins");
    guard.cfg().hostileOverrides = {};
    guard.cfg().passiveOverrides = {};

    auto basicBp = ctx.createBackpack(Tier::Netherite);
    install(ctx, basicBp, kBasicId);
    guard.cfg().basicMaxSlotCost = 5;
    r = capture(p, basicBp, *cow, false);
    ctx.check(!r.success && r.message == "Mob needs 10 slots, more than this upgrade allows (5)", "too large: " + r.message);
    ctx.check(capturedMobs(*bp).empty() && capturedMobs(*basicBp).empty(), "nothing stored");
    cow->despawn();
});

ABP_REGISTER_SELFTEST("mobcatcher.release", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await groundPlayer(ctx);
    auto& p  = ctx.player();
    auto  bp = ctx.createBackpack(Tier::Iron);
    install(ctx, bp, kBasicId);
    auto* cow = spawnMob(ctx, "minecraft:cow", 2);
    cow->setNameTag("Bessie");
    ctx.require(capture(p, bp, *cow, false).success, "captured");
    co_await ctx.ticks(2);
    auto mobs = capturedMobs(*bp);
    ctx.require(mobs.size() == 1, "stored");
    ctx.check(mobs[0].displayName == "Bessie", "custom name kept");
    std::string why;
    ctx.check(!upgrade::remove(bp, 0, &p, &why).has_value(), "the catcher can't be removed while a mob is stored");

    auto r = release(p, bp, mobs[0].id);
    ctx.require(r.success, "released: " + r.message);
    ctx.check(r.message == "Released Bessie", "message names the mob: " + r.message);
    ctx.check(capturedMobs(*bp).empty() && !bp->inventory().isSlotBlocked(0), "mob slots free again");
    int  cows = 0;
    auto pos  = p.getPosition();
    for (auto* a : p.getDimensionBlockSource().getEntities(AABB{Vec3{pos.x - 6, pos.y - 3, pos.z - 6}, Vec3{pos.x + 6, pos.y + 4, pos.z + 6}})) {
        if (a && a->getTypeName() == "minecraft:cow" && a->getNameTag() == "Bessie") {
            ++cows;
            a->despawn();
        }
    }
    ctx.check(cows == 1, "the released cow is back next to the player (with its name)");
    ctx.check(upgrade::remove(bp, 0, &p, &why).has_value(), "removable once empty");
});

ABP_REGISTER_SELFTEST("mobcatcher.native_screen", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await groundPlayer(ctx);
    auto& p  = ctx.player();
    auto  bp = ctx.createBackpack(Tier::Iron);
    install(ctx, bp, kBasicId);
    auto* cow = spawnMob(ctx, "minecraft:cow", 2);
    ctx.require(capture(p, bp, *cow, false).success, "captured");
    net::setCompanion(p, net::CompanionInfo{"test", {"native_screen"}});
    struct Reset {
        Player& p;
        ~Reset() {
            net::setCompanion(p, std::nullopt);
            ui::MenuManager::get().close(p);
        }
    } reset{p};
    ctx.require(ui::BackpackMenu::openBackpack(p, bp), "open backpack menu");
    ui::MenuManager::get().forceShown(p);
    auto session = net::nativeSession(p);
    ctx.require(session != 0, "native session");
    co_await ctx.ticks(1);
    auto const* m = net::sentModel(p);
    ctx.require(m && !m->upgrades.empty(), "model");
    auto const& tab = m->upgrades[0].tab;
    ctx.require(tab.contains("mobCatcher"), "catcher data on the hidden tab");
    auto const mobs = tab["mobCatcher"].value("mobs", nlohmann::json::array()); // copy: the model is replaced later
    ctx.require(mobs.is_array() && mobs.size() == 1, "one mob listed");
    ctx.check(!mobs[0].contains("nbt"), "mob listed without its NBT");
    ctx.check(!m->upgrades[0].hasSettings && !m->upgrades[0].toggleable, "no tab, no switch");

    // Taking the upgrade out shows SB's error over the upgrade slot and the mob's slots.
    p.setPlayerUIItem(PlayerUISlot::CursorSelected, ItemStack::EMPTY_ITEM(), false);
    nlohmann::json click{{"session", session}, {"action", "click"}, {"area", "upgrade"}, {"index", 0}, {"button", 0}};
    ctx.check(!net::handleScreenAction(p, click), "removal refused");
    co_await ctx.ticks(1);
    m = net::sentModel(p);
    ctx.check(m && m->errorMessage.find("Release captured mobs") != std::string::npos, "error message");
    ctx.check(m && m->errorStorageSlots.size() == 12, "12 mob slots highlighted");

    nlohmann::json releaseAction{{"session", session}, {"action", "tab"}, {"slot", 0}, {"data", {{"release", mobs[0]["id"]}}}};
    ctx.check(net::handleScreenAction(p, releaseAction), "release action handled");
    ctx.check(capturedMobs(*bp).empty(), "released through the screen");
});

} // namespace
} // namespace abp::upgrade::mobcatcher

namespace abp::upgrade::mobcatcher {
namespace {
/// Bedrock clients send no entity interaction for mobs without one (zombies): their sneak-use hits
/// the block behind the mob. The mob in the line of sight is captured instead (and no backpack is
/// placed).
ABP_REGISTER_SELFTEST("mobcatcher.capture_through_use", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await groundPlayer(ctx);
    co_await isolatePlayer(ctx, 40.f);
    auto&           p = static_cast<SimulatedPlayer&>(ctx.player());
    DifficultyGuard difficulty(p.getLevel());
    auto            bp = ctx.createBackpack(Tier::Netherite);
    install(ctx, bp, kAdvancedId);
    int hand = p.getSelectedItemSlot();
    ctx.give(p, ctx.backpackItem(*bp), hand);
    auto* zombie = spawnMob(ctx, "minecraft:zombie", 2.5f);
    auto  id     = zombie->getOrCreateUniqueID();
    // Aim at the zombie (it may wander a little): retry until the look ray hits it.
    ctx.check(p.simulateSneaking(), "sneaking");
    Vec3      zp;
    HitResult hit;
    for (int i = 0; i < 10; ++i) {
        zombie = static_cast<Mob*>(p.getLevel().fetchEntity(id, false));
        ctx.require(zombie != nullptr, "the zombie is still there");
        zp = zombie->getPosition();
        p.simulateLookAt(Vec3{zp.x, zp.y + 0.8f, zp.z});
        co_await ctx.ticks(1);
        hit = p.traceRay(5.f, true, true);
        if (hit.mType == HitResultType::Entity) break;
    }
    if (hit.mType != HitResultType::Entity) {
        ctx.info(
            "trace hit type " + std::to_string(static_cast<int>(hit.mType)) + " block " + std::to_string(hit.mBlock.x) + ","
            + std::to_string(hit.mBlock.y) + "," + std::to_string(hit.mBlock.z)
        );
    }
    ctx.require(hit.mType == HitResultType::Entity, "the zombie is in the line of sight");
    BlockPos ground{static_cast<int>(std::floor(zp.x)), static_cast<int>(std::floor(zp.y)) - 1, static_cast<int>(std::floor(zp.z))};
    p.simulateUseItemInSlotOnBlock(hand, ground, ScriptModuleMinecraft::ScriptFacing::Up, Vec3{0.5f, 1.f, 0.5f});
    co_await ctx.ticks(3);
    p.simulateStopSneaking();
    ctx.check(capturedMobs(*bp).size() == 1, "captured through the use on the block behind");
    ctx.check(!mobExists(p, id), "zombie removed");
    ctx.check(game::isBackpack(p.getSelectedItem()), "the backpack is still in the hand (not placed)");
});
} // namespace
} // namespace abp::upgrade::mobcatcher
