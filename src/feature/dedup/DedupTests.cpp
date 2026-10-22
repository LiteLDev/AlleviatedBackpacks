// Selftests of the uuid dedup logic and the nerfs: dedup.*, nerf.*

#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/effect/MobEffect.h"
#include "mc/world/effect/MobEffectInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"

#include <cmath>

#include "feature/admin/SbConfig.h"
#include "feature/dedup/Dedup.h"
#include "feature/nerf/Nerf.h"
#include "game/BackpackItem.h"
#include "game/PlayerScan.h"
#include "game/Storage.h"
#include "mod/Config.h"
#include "selftest/SelfTest.h"
#include "ui/menu/BackpackMenu.h"
#include "ui/menu/MenuManager.h"

namespace abp::dedup {
namespace {

using selftest::Context;
using core::Tier;
using SharedTypes::Legacy::ArmorSlot;

struct SbGuard {
    admin::SbServerConfig saved = admin::sbServer();
    ~SbGuard() { config::mut<admin::SbServerConfig>() = saved; }
    admin::SbServerConfig& cfg() { return config::mut<admin::SbServerConfig>(); }
};

/// Test players may stand above unloaded chunks at the world spawn: move them onto the ground.
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

std::optional<std::string> uuidAt(Player& p, int slot) { return game::backpackUuid(game::itemAt(p, {game::ItemLocation::Kind::Inventory, slot})); }

ABP_REGISTER_SELFTEST("dedup.players", [](Context& ctx) {
    SbGuard guard;
    guard.cfg().tickDedupeLogicDisabled = true; // run it by hand
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(Tier::Leather);
    ctx.require(ctx.give(player, game::makeBackpackItem(Tier::Leather, bp->uuid()), 1) == 1, "leather copy");
    ctx.require(ctx.give(player, game::makeBackpackItem(Tier::Iron, bp->uuid()), 2) == 2, "iron copy");
    ctx.check(dedupePlayers(static_cast<int>(player.getDimensionId())) == 1, "one duplicate");
    ctx.check(!uuidAt(player, 1).has_value(), "the smaller copy lost the uuid");
    ctx.check(uuidAt(player, 2) == bp->uuid(), "the copy with more slots keeps it");
    ctx.check(dedupePlayers(static_cast<int>(player.getDimensionId())) == 0, "nothing left to dedupe");
});

ABP_REGISTER_SELFTEST("dedup.tick", [](Context& ctx) -> ll::coro::CoroTask<> {
    SbGuard guard;
    auto&   player = ctx.player();
    auto    bp     = ctx.createBackpack(Tier::Copper);
    guard.cfg().tickDedupeLogicDisabled = true;
    ctx.require(ctx.give(player, game::makeBackpackItem(Tier::Copper, bp->uuid()), 3) == 3, "copy 1");
    ctx.require(ctx.give(player, game::makeBackpackItem(Tier::Copper, bp->uuid()), 4) == 4, "copy 2");
    co_await ctx.ticks(45);
    ctx.check(uuidAt(player, 3).has_value() && uuidAt(player, 4).has_value(), "tickDedupeLogicDisabled: both keep it");
    guard.cfg().tickDedupeLogicDisabled = false;
    co_await ctx.ticks(45); // every 40 ticks per dimension
    ctx.check(uuidAt(player, 3).has_value() != uuidAt(player, 4).has_value(), "the periodic check leaves one copy");
    ctx.check(uuidAt(player, 3) == bp->uuid(), "a tie keeps the first one");
});

ABP_REGISTER_SELFTEST("dedup.item_entities", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await groundPlayer(ctx);
    auto& player  = ctx.player();
    auto& region  = player.getDimensionBlockSource();
    auto& spawner = player.getLevel().getSpawner();
    auto  bp      = ctx.createBackpack(Tier::Gold);
    auto  pos     = player.getPosition();
    auto* first   = spawner.spawnItem(region, game::makeBackpackItem(Tier::Leather, bp->uuid()), nullptr, pos, 0);
    ctx.require(first != nullptr, "first drop");
    auto* second = spawner.spawnItem(region, game::makeBackpackItem(Tier::Gold, bp->uuid()), nullptr, Vec3{pos.x + 3, pos.y, pos.z}, 0);
    ctx.require(second != nullptr, "second drop");
    ctx.check(!game::backpackUuid(first->item()), "the smaller item entity lost the uuid");
    ctx.check(game::backpackUuid(second->item()) == bp->uuid(), "the new, bigger one keeps it");
    auto* far = spawner.spawnItem(region, game::makeBackpackItem(Tier::Leather, bp->uuid()), nullptr, Vec3{pos.x + 40, pos.y, pos.z}, 0);
    ctx.check(far && game::backpackUuid(far->item()) == bp->uuid(), "beyond 10 blocks: untouched");
    first->remove();
    second->remove();
    if (far) far->remove();
});

ABP_REGISTER_SELFTEST("dedup.on_open", [](Context& ctx) -> ll::coro::CoroTask<> {
    SbGuard guard;
    guard.cfg().tickDedupeLogicDisabled = true;
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(Tier::Iron);
    ctx.require(ctx.give(player, game::makeBackpackItem(Tier::Iron, bp->uuid()), 6) == 6, "copy 1");
    ctx.require(ctx.give(player, game::makeBackpackItem(Tier::Iron, bp->uuid()), 7) == 7, "copy 2");
    ctx.require(ui::BackpackMenu::openFor(player, {game::ItemLocation::Kind::Inventory, 7}), "open copy 2");
    co_await ctx.ticks(3);
    ctx.check(uuidAt(player, 7) == bp->uuid(), "the opened backpack keeps the uuid (tie)");
    ctx.check(!uuidAt(player, 6).has_value(), "its duplicate lost it");
    ui::MenuManager::get().close(player);
});

// ---- nerfs ------------------------------------------------------------------------------------------

ABP_REGISTER_SELFTEST("nerf.too_many_backpacks", [](Context& ctx) {
    SbGuard guard;
    auto&   player = ctx.player();
    for (int i = 0; i < 5; ++i) ctx.require(ctx.give(player, game::makeBackpackItem(Tier::Leather), 10 + i) == 10 + i, "give");
    guard.cfg().nerfs.tooManyBackpacksSlowness = false;
    ctx.check(nerf::applySlowness(player) == 0, "off by default (SB)");
    guard.cfg().nerfs.tooManyBackpacksSlowness             = true;
    guard.cfg().nerfs.maxNumberOfBackpacks                 = 3;
    guard.cfg().nerfs.slownessLevelsPerAdditionalBackpack = 1;
    ctx.check(nerf::applySlowness(player) == 2, "5 backpacks, max 3: 2 levels");
    auto const* slowness = nerf::nerfEffect();
    ctx.require(slowness != nullptr, "slowness effect");
    auto const* effect = player.getEffect(*slowness);
    ctx.require(effect != nullptr, "slowness applied");
    ctx.check(effect->mAmplifier == 1, "amplifier = levels - 1");
    ctx.check(effect->mDuration->mValue <= 80, "80 ticks (40 * 2)");
    guard.cfg().nerfs.maxNumberOfBackpacks = 10;
    ctx.check(nerf::applySlowness(player) == 0, "under the limit");
    guard.cfg().nerfs.nerfEffect = "minecraft:does_not_exist";
    ctx.check(nerf::nerfEffect() == slowness, "unknown effects fall back to slowness");
    player.removeAllEffects();
});

ABP_REGISTER_SELFTEST("nerf.only_worn_triggers_upgrades", [](Context& ctx) {
    SbGuard guard;
    auto&   player = ctx.player();
    auto    worn   = ctx.createBackpack(Tier::Leather);
    auto    held   = ctx.createBackpack(Tier::Leather);
    player.setArmor(ArmorSlot::Torso, ctx.backpackItem(*worn));
    ctx.require(ctx.give(player, ctx.backpackItem(*held), 8) == 8, "give");
    auto active = [&](std::string const& uuid) {
        for (auto& [c, bp] : game::carriedBackpacks(player, false, true)) {
            if (bp->uuid() == uuid) return c.upgradeActive("any", "any");
        }
        return false;
    };
    guard.cfg().nerfs.onlyWornBackpackTriggersUpgrades = false;
    ctx.check(active(worn->uuid()) && active(held->uuid()), "default: every carried backpack runs its upgrades");
    guard.cfg().nerfs.onlyWornBackpackTriggersUpgrades = true;
    ctx.check(active(worn->uuid()), "the worn backpack runs its upgrades");
    ctx.check(!active(held->uuid()), "the others don't");
    player.setArmor(ArmorSlot::Torso, ItemStack::EMPTY_ITEM());
});

} // namespace
} // namespace abp::dedup
