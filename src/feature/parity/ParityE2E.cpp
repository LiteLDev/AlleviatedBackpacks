// GX-5: `/backpack parity ...`, scene setup and status lines for the client e2e scenario `parity`
// (operators only, like the other e2e setup commands):
//
//   parity limits       a diamond backpack (hotbar 0) with Crafting (upgrade slot 0) and Jukebox
//                       (slot 1), a second Crafting upgrade (hotbar 1) and a second Jukebox (hotbar 2)
//   parity status       logs "parity status: health=.. piglins=.. targeting=.." (piglins within 16
//                       blocks and how many of them target the player)
//   parity everlasting  drops an everlasting backpack 3 blocks east of the player
//   parity explode      an explosion 1.5 blocks west of the nearest dropped everlasting backpack;
//                       10 ticks later logs "parity explode: moved <blocks>"

#include <cmath>
#include <string>

#include "ll/api/chrono/GameChrono.h"
#include "ll/api/command/CommandHandle.h"
#include "ll/api/coro/CoroTask.h"
#include "ll/api/service/Bedrock.h"
#include "ll/api/thread/ServerThreadExecutor.h"

#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/phys/AABB.h"

#include "core/Tier.h"
#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/Storage.h"
#include "mod/Commands.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "upgrade/UpgradeManager.h"

namespace abp::feature::parity {
namespace {

Player* operatorOf(CommandOrigin const& origin, CommandOutput& output) {
    auto* e = origin.getEntity();
    if (!e || !e->isPlayer()) {
        output.error(tr("abp.parity.players_only", "Only players can use this."));
        return nullptr;
    }
    auto* player = static_cast<Player*>(e);
    if (origin.getPermissionsLevel() < CommandPermissionLevel::GameDirectors) {
        output.error(trFor(*player, "abp.parity.operators_only", "You need operator permission."));
        return nullptr;
    }
    if (!game::Storage::get().available()) {
        output.error(trFor(*player, "abp.parity.no_storage", "Backpack storage is not available."));
        return nullptr;
    }
    return player;
}

void giveAt(Player& player, int slot, ItemStack stack) {
    player.getInventory().setItem(slot, stack);
}

ItemStack upgradeItem(std::string_view id) {
    auto* u = upgrade::byId(id);
    return u ? game::ItemConv::make(u->info().itemId, 1) : ItemStack{};
}

bool installById(std::shared_ptr<game::Backpack> const& bp, std::string_view id, int slot) {
    auto item = upgradeItem(id);
    return !item.isNull() && upgrade::install(bp, slot, game::ItemConv::toData(item), nullptr);
}

ItemActor* nearestEverlastingDrop(Player& player) {
    auto       at   = player.getPosition();
    ItemActor* best = nullptr;
    float      bd   = 16.0f * 16.0f;
    for (auto* a : player.getDimensionBlockSource().getEntities(AABB(at - Vec3{16, 16, 16}, at + Vec3{16, 16, 16}), 0.0f)) {
        if (!a || a->mRemoved || !a->hasType(ActorType::ItemEntity)) continue;
        auto& item = static_cast<ItemActor&>(*a);
        if (!game::isBackpack(item.item())) continue;
        auto uuid = game::backpackUuid(item.item());
        auto bp   = uuid ? game::Storage::get().find(*uuid) : nullptr;
        if (!bp || !upgrade::hasActive(*bp, "everlasting")) continue;
        float d = a->getPosition().distanceToSqr(at);
        if (d < bd) {
            bd   = d;
            best = &item;
        }
    }
    return best;
}

} // namespace
} // namespace abp::feature::parity

ABP_REGISTER_SUBCOMMAND([](ll::command::CommandHandle& cmd) {
    using namespace abp;
    using namespace abp::feature::parity;
    cmd.overload().text("parity").text("limits").execute([](CommandOrigin const& origin, CommandOutput& output) {
        auto* player = operatorOf(origin, output);
        if (!player) return;
        auto bp = game::Storage::get().create(core::Tier::Diamond);
        if (!bp || !installById(bp, "crafting", 0) || !installById(bp, "jukebox", 1)) {
            output.error(trFor(*player, "abp.parity.setup_failed", "Could not set up the backpack."));
            return;
        }
        game::Storage::get().save(*bp, true);
        giveAt(*player, 0, game::makeBackpackItem(core::Tier::Diamond, bp->uuid()));
        giveAt(*player, 1, upgradeItem("crafting"));
        giveAt(*player, 2, upgradeItem("jukebox"));
        player->sendInventory(false);
        log().info("parity limits: gave diamond backpack {}", bp->uuid());
        output.success(trFor(*player, "abp.parity.limits", "parity limits: gave diamond backpack {}", bp->uuid()));
    });
    cmd.overload().text("parity").text("status").execute([](CommandOrigin const& origin, CommandOutput& output) {
        auto* player = operatorOf(origin, output);
        if (!player) return;
        int  piglins = 0, targeting = 0;
        auto at      = player->getPosition();
        auto me      = player->getOrCreateUniqueID().rawID;
        for (auto* a : player->getDimensionBlockSource().getEntities(AABB(at - Vec3{16, 8, 16}, at + Vec3{16, 8, 16}), 0.0f)) {
            if (!a || a->mRemoved || a->getEntityTypeId() != ActorType::Piglin) continue;
            ++piglins;
            if (a->mTargetId->rawID == me) ++targeting;
        }
        auto line = "health=" + std::to_string(player->getHealth()) + " piglins=" + std::to_string(piglins)
                  + " targeting=" + std::to_string(targeting);
        log().info("parity status: {}", line);
        output.success(trFor(*player, "abp.parity.status", "parity status: {}", line));
    });
    cmd.overload().text("parity").text("everlasting").execute([](CommandOrigin const& origin, CommandOutput& output) {
        auto* player = operatorOf(origin, output);
        if (!player) return;
        auto bp = game::Storage::get().create(core::Tier::Gold);
        if (!bp || !installById(bp, "everlasting", 0)) {
            output.error(trFor(*player, "abp.parity.setup_failed", "Could not set up the backpack."));
            return;
        }
        game::Storage::get().save(*bp, true);
        auto& region = player->getDimensionBlockSource();
        auto  p      = player->getPosition();
        int   x = static_cast<int>(std::floor(p.x)) + 3, z = static_cast<int>(std::floor(p.z));
        Vec3  spot{static_cast<float>(x) + 0.5f, static_cast<float>(region.getAboveTopSolidBlock(x, z, false, false)) + 0.05f, static_cast<float>(z) + 0.5f};
        auto* actor = player->getLevel().getSpawner().spawnItem(region, game::makeBackpackItem(core::Tier::Gold, bp->uuid()), nullptr, spot, 0);
        if (!actor) {
            output.error(trFor(*player, "abp.parity.drop_failed", "Could not drop the backpack."));
            return;
        }
        static_cast<ItemActor*>(actor)->pickupDelay() = 32767;
        log().info("parity everlasting: dropped {} at {:.2f} {:.2f} {:.2f}", bp->uuid(), spot.x, spot.y, spot.z);
        output.success(trFor(*player, "abp.parity.everlasting", "parity everlasting: dropped {}", bp->uuid()));
    });
    cmd.overload().text("parity").text("explode").execute([](CommandOrigin const& origin, CommandOutput& output) {
        auto* player = operatorOf(origin, output);
        if (!player) return;
        auto* item = nearestEverlastingDrop(*player);
        if (!item) {
            output.error(trFor(*player, "abp.parity.no_everlasting", "No dropped Everlasting backpack nearby."));
            return;
        }
        auto id     = item->getOrCreateUniqueID();
        auto before = item->getPosition();
        (void)player->getLevel().explode(player->getDimensionBlockSource(), nullptr, before + Vec3{-1.5f, 0.2f, 0.0f}, 3.0f, false, false, 0.0f, true);
        output.success(trFor(*player, "abp.parity.explode", "parity explode: boom"));
        ll::coro::keepThis([id, before]() -> ll::coro::CoroTask<> {
            co_await ll::chrono::ticks(10);
            auto level = ll::service::getLevel();
            if (!level) co_return;
            auto* a = level->fetchEntity(id, false);
            if (!a || a->mRemoved) {
                log().info("parity explode: gone");
                co_return;
            }
            auto  after = a->getPosition();
            float moved = std::sqrt((after.x - before.x) * (after.x - before.x) + (after.z - before.z) * (after.z - before.z));
            log().info("parity explode: moved {:.3f}", moved);
        }).launch(ll::thread::ServerThreadExecutor::getDefault());
    });
});
