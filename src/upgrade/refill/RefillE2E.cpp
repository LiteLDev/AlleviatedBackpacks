// Setup step for the client e2e scenario (tools/e2e/client/scenarios/refill.ps1): prepares the
// inventory of the real player tagged `abp_e2e_refill` (the scenario adds the tag) so that real
// input (pick block, GUI clicks) can be exercised. Players without the tag are never touched.
// Without a tagged player the test does nothing.

#include <utility>

#include "ll/api/coro/CoroTask.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"

#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/Storage.h"
#include "selftest/SelfTest.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/refill/Refill.h"

namespace abp::upgrade::refill {
namespace {

// The player and backpack prepared by `refill.e2e_real_player` (for `refill.e2e_open_settings`).
ActorUniqueID gE2ePlayer;
std::string   gE2eBackpack;

ABP_REGISTER_SELFTEST("refill.e2e_real_player", [](selftest::Context& ctx) {
    auto level = ll::service::getLevel();
    ctx.require(level.has_value(), "level");
    Player* real = nullptr;
    level->forEachPlayer([&](Player& p) {
        if (p.isSimulatedPlayer() || !p.hasTag("abp_e2e_refill")) return true;
        real = &p;
        return false;
    });
    if (!real) {
        ctx.info("no real player tagged abp_e2e_refill: nothing to set up");
        return;
    }
    real->removeTag("abp_e2e_refill"); // one-shot
    auto bp = game::Storage::get().create(core::Tier::Gold);
    ctx.require(bp != nullptr, "create backpack");
    auto upgradeItem = game::ItemConv::toData(game::ItemConv::make("alleviatedbackpacks:advanced_refill_upgrade", 1));
    std::string reason;
    ctx.require(upgrade::install(bp, 0, upgradeItem, real, &reason), "install advanced refill: " + reason);
    UpgradeContext uc(bp, 0, real);
    auto           s = load(uc, kAdvancedSlots);
    s.filter.setGhost(0, game::ItemConv::toData(game::ItemConv::make("minecraft:torch", 1)));
    s.targets[0] = static_cast<Target>(static_cast<int>(Target::Hotbar1) + 2); // hotbar slot 3
    save(uc, s);
    core::AccessContext internal{core::Source::Internal};
    bp->inventory().insert(game::ItemConv::toData(game::ItemConv::make("minecraft:torch", 64)), internal);
    bp->inventory().insert(game::ItemConv::toData(game::ItemConv::make("minecraft:gold_block", 64)), internal);
    bp->markDirty();

    auto& inv = real->getInventory();
    for (int i = 0; i < inv.getContainerSize(); ++i) inv.setItem(i, ItemStack::EMPTY_ITEM());
    inv.setItem(0, game::makeBackpackItem(core::Tier::Gold, bp->uuid()));
    real->setSelectedSlot(0);
    real->sendInventory(true);
    // A gold block under the player, looking straight down at it (for the pick block step).
    auto&    region  = real->getDimensionBlockSource();
    BlockPos below   = real->getFeetBlockPos();
    below.y         -= 1;
    auto gold        = Block::tryGetFromRegistry(HashedString("minecraft:gold_block"));
    ctx.require(gold.has_value() && region.setBlockAndRetainCompatibleBlockActor(below, *gold, 3), "place gold block");
    real->teleport(real->getFeetPos(), real->getDimensionId(), Vec2{90.0f, 0.0f});
    gE2ePlayer   = real->getOrCreateUniqueID();
    gE2eBackpack = bp->uuid();
    ctx.info("real player set up with backpack " + bp->uuid());
});

// Opens the Advanced Refill settings of the backpack prepared above for that player (a fresh
// chest menu, after the chat screen that ran the command has closed).
ABP_REGISTER_SELFTEST("refill.e2e_open_settings", [](selftest::Context& ctx) -> ll::coro::CoroTask<> {
    if (gE2eBackpack.empty()) {
        ctx.info("no e2e backpack prepared: nothing to open");
        co_return;
    }
    auto uuid = std::exchange(gE2eBackpack, {});
    co_await ctx.ticks(20);
    auto level = ll::service::getLevel();
    ctx.require(level.has_value(), "level");
    auto* player = level->getPlayer(gE2ePlayer);
    ctx.require(player != nullptr, "e2e player online");
    auto bp = game::Storage::get().find(uuid);
    ctx.require(bp != nullptr, "e2e backpack");
    auto* type = upgrade::byId(kAdvancedId);
    type->openSettings(*player, UpgradeContext(bp, 0, player));
    ctx.check(ui::MenuManager::get().current(*player) != nullptr, "settings menu opened");
});

} // namespace
} // namespace abp::upgrade::refill
