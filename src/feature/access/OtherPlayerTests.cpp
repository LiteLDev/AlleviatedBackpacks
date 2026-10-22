// Selftests of opening another player's backpack: access.*

#include <cmath>

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"

#include "feature/access/OtherPlayer.h"
#include "feature/admin/SbConfig.h"
#include "feature/gui/Gui.h"
#include "game/BackpackItem.h"
#include "game/PlayerScan.h"
#include "game/Storage.h"
#include "mod/Config.h"
#include "selftest/SelfTest.h"
#include "ui/menu/BackpackMenu.h"
#include "ui/menu/MenuManager.h"

namespace abp::access {
namespace {

using selftest::Context;
using core::Tier;
using SharedTypes::Legacy::ArmorSlot;

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

/// Puts `target` at `at` facing +z (yaw 0) and `source` 1.5 blocks away along z (`behind`: -z).
ll::coro::CoroTask<> arrange(Context& ctx, Player& source, Player& target, Vec3 at, bool behind) {
    target.teleport(at, target.getDimensionId(), Vec2{0.0f, 0.0f});
    source.teleport(Vec3{at.x, at.y, at.z + (behind ? -1.5f : 1.5f)}, source.getDimensionId());
    co_await ctx.ticks(3);
    target.setYBodyRotation(0.0f);
}

ABP_REGISTER_SELFTEST("access.open_another_players_backpack", [](Context& ctx) -> ll::coro::CoroTask<> {
    co_await groundPlayer(ctx);
    struct Restore {
        bool saved = admin::sbServer().allowOpeningOtherPlayerBackpacks;
        ~Restore() { config::mut<admin::SbServerConfig>().allowOpeningOtherPlayerBackpacks = saved; }
    } restore;
    auto& source = ctx.player();
    auto& target = ctx.spawnPlayer("abp_target");
    auto& mm     = ui::MenuManager::get();
    auto  bp     = ctx.createBackpack(Tier::Iron);
    target.setArmor(ArmorSlot::Torso, ctx.backpackItem(*bp));
    auto at = source.getPosition();
    at      = Vec3{at.x + 2.0f, source.getFeetPos().y, at.z};
    co_await arrange(ctx, source, target, at, true);
    auto feet    = target.getFeetPos();
    Vec3 backHit = Vec3{feet.x, feet.y + 1.2f, feet.z - 0.3f};

    ctx.check(hitsBack(source, target, backHit), "a body hit from behind");
    ctx.check(!hitsBack(source, target, Vec3{feet.x, feet.y + 0.5f, feet.z - 0.3f}), "legs are not the back");
    ctx.check(!hitsBack(source, target, Vec3{feet.x, feet.y + 1.7f, feet.z - 0.3f}), "the head is not the back");

    // Opens the worn backpack for the clicking player.
    auto r = tryOpen(source, target, backHit);
    ctx.check(r == OpenResult::Opened, std::string("opened: ") + std::string(resultName(r)));
    co_await ctx.ticks(2);
    auto menu = std::dynamic_pointer_cast<ui::BackpackMenu>(mm.current(source));
    ctx.require(menu && menu->backpack() == bp, "the target's backpack menu is open");
    // Taking the backpack off closes it.
    target.setArmor(ArmorSlot::Torso, ItemStack::EMPTY_ITEM());
    co_await ctx.ticks(3);
    ctx.check(mm.current(source) == nullptr, "closed when the target takes the backpack off");
    target.setArmor(ArmorSlot::Torso, ctx.backpackItem(*bp));

    // From the front: no.
    co_await arrange(ctx, source, target, at, false);
    feet = target.getFeetPos();
    ctx.check(tryOpen(source, target, Vec3{feet.x, feet.y + 1.2f, feet.z + 0.3f}) == OpenResult::NotBack, "not from the front");
    co_await arrange(ctx, source, target, at, true);
    feet    = target.getFeetPos();
    backHit = Vec3{feet.x, feet.y + 1.2f, feet.z - 0.3f};

    // The target's setting (backpack override) forbids it.
    feature::gui::setBackpackOption(*bp, std::string(kAnotherPlayerCanOpen), false);
    ctx.check(!anotherPlayerCanOpen(target, *bp), "anotherPlayerCanOpen off");
    ctx.check(tryOpen(source, target, backHit) == OpenResult::NotAllowed, "the setting refuses");
    feature::gui::setBackpackOption(*bp, std::string(kAnotherPlayerCanOpen), std::nullopt);
    ctx.check(anotherPlayerCanOpen(target, *bp), "default on (SB)");

    // The server config turns it off for everyone.
    config::mut<admin::SbServerConfig>().allowOpeningOtherPlayerBackpacks = false;
    ctx.check(tryOpen(source, target, backHit) == OpenResult::Disabled, "allowOpeningOtherPlayerBackpacks off");
    config::mut<admin::SbServerConfig>().allowOpeningOtherPlayerBackpacks = true;

    // Only the chest slot counts.
    target.setArmor(ArmorSlot::Torso, ItemStack::EMPTY_ITEM());
    ctx.require(ctx.give(target, ctx.backpackItem(*bp), 0) == 0, "backpack in the hotbar");
    ctx.check(tryOpen(source, target, backHit) == OpenResult::NoBackpack, "carried but not worn: nothing to open");
    mm.close(source);
});

} // namespace
} // namespace abp::access
