#pragma once
// Selftest helpers of unit U2 (placed backpacks + hopper upgrades): a small building site next
// to the test player that is cleaned up afterwards.

#include <cmath>
#include <string>
#include <string_view>
#include <vector>

#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/server/commands/ServerCommandOrigin.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/Container.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/gamemode/InteractionResult.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/phys/AABB.h"

#include "feature/placed/ContainerIO.h"
#include "feature/placed/PlacedBackpacks.h"
#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "selftest/SelfTest.h"

namespace abp::placed::test {

/// Blocks set through a site are restored to air when the site goes away; placed backpacks in
/// it are taken back silently (their storage is deleted by the selftest context).
class Site {
public:
    /// Origin `dx` blocks east of the player, on the ground (the first air block of that column).
    Site(Player& player, int dx = 3, int dz = 0) : mRegion(player.getDimension().getBlockSourceFromMainChunkSource()) {
        auto feet = player.getFeetPos();
        int  x    = static_cast<int>(std::floor(feet.x)) + dx;
        int  z    = static_cast<int>(std::floor(feet.z)) + dz;
        mOrigin   = BlockPos{x, static_cast<int>(std::floor(feet.y)), z};
    }
    ~Site() {
        for (auto it = mTouched.rbegin(); it != mTouched.rend(); ++it) clear(*it);
    }
    Site(Site const&)            = delete;
    Site& operator=(Site const&) = delete;

    [[nodiscard]] BlockSource&    region() const { return mRegion; }
    [[nodiscard]] BlockPos        at(int dx, int dy, int dz = 0) const { return mOrigin + BlockPos{dx, dy, dz}; }
    [[nodiscard]] BlockPos const& origin() const { return mOrigin; }

    /// Sets a block (default state, or `block` if given). Returns false if unknown.
    bool set(BlockPos const& pos, std::string_view id) {
        auto block = Block::tryGetFromRegistry(HashedString(std::string(id)));
        if (!block) return false;
        return set(pos, *block);
    }
    bool set(BlockPos const& pos, Block const& block) {
        touch(pos);
        BlockChangeContext ctx{};
        return mRegion.setBlock(pos, block, 3, nullptr, ctx);
    }
    /// Marks a position for cleanup (e.g. where a backpack gets placed by a player action).
    void touch(BlockPos const& pos) { mTouched.push_back(pos); }
    /// Air at `pos`; a placed backpack there is taken back without a drop.
    void clear(BlockPos const& pos) {
        if (PlacedBackpacks::isBackpackBlock(mRegion.getBlock(pos))) (void)PlacedBackpacks::get().take(mRegion, pos);
        if (!mRegion.getBlock(pos).isAir()) {
            if (auto* c = containerAt(mRegion, pos)) c->removeAllItems();
            BlockChangeContext ctx{};
            mRegion.removeBlock(pos, ctx);
        }
    }

    /// Item entities within 2 blocks of `pos`.
    [[nodiscard]] std::vector<ItemActor*> dropsNear(BlockPos const& pos) const {
        std::vector<ItemActor*> out;
        auto                    c = pos.center();
        for (auto* a : mRegion.getEntities(AABB(c - Vec3{2, 2, 2}, c + Vec3{2, 2, 2}), 0.0f)) {
            if (a && a->isType(ActorType::ItemEntity)) out.push_back(static_cast<ItemActor*>(a));
        }
        return out;
    }
    /// Removes the dropped backpack items bound to `uuid` near `pos`; returns how many there were.
    int collectBackpackDrops(BlockPos const& pos, std::string const& uuid) const {
        int n = 0;
        for (auto* a : dropsNear(pos)) {
            auto const& item = a->item();
            if (game::isBackpack(item) && game::backpackUuid(item) == uuid) {
                n += item.mCount;
                a->remove();
            }
        }
        return n;
    }

private:
    BlockSource&          mRegion;
    BlockPos              mOrigin;
    std::vector<BlockPos> mTouched;
};

/// The first air block above the topmost non-air block of a column.
inline int groundY(BlockSource& region, int x, int z) {
    for (int y = region.getMaxHeight() - 1; y > region.getMinHeight(); --y) {
        if (!region.getBlock(BlockPos{x, y - 1, z}).isAir()) return y;
    }
    return region.getMinHeight();
}

/// Moves a freshly spawned test player (it spawns high above the world spawn) to the U2 test area
/// (the middle of a chunk away from spawn) and onto the ground there, once it is loaded.
inline ll::coro::CoroTask<> toGround(selftest::Context& ctx, Player& player) {
    auto& region = player.getDimension().getBlockSourceFromMainChunkSource();
    auto  feet   = player.getFeetPos();
    // Chunks around the world spawn sometimes never finish loading after many quick test players
    // came and went there (BDS), so use an area of our own, kept loaded by a ticking area (adding
    // it again fails harmlessly).
    int x = 1032;
    int  z  = 1032;
    auto& sp = static_cast<SimulatedPlayer&>(player); // test players are simulated players
    sp.simulateFly(); // no fall damage while the area loads
    player.teleport(
        Vec3{static_cast<float>(x) + 0.5f, static_cast<float>(region.getMaxHeight() - 2), static_cast<float>(z) + 0.5f},
        player.getDimensionId()
    );
    if (auto level = ll::service::getLevel()) {
        ServerCommandOrigin origin("abp-selftest", level->asServer(), CommandPermissionLevel::Owner, player.getDimensionId());
        auto out = ll::command::CommandRegistrar::getServerInstance().executeCommand(
            "tickingarea add circle " + std::to_string(x) + " 0 " + std::to_string(z) + " 2 abp_selftest_u2",
            origin
        );
        (void)out;
    }
    for (int i = 0; i < 200 && !region.hasChunksAt(BlockPos{x, 0, z}, 16, false); ++i) co_await ctx.ticks(1);
    ctx.require(
        region.hasChunksAt(BlockPos{x, 0, z}, 16, false),
        "chunks around the test player never loaded (spawned at " + std::to_string(feet.y) + ", now at "
            + std::to_string(player.getFeetPos().x) + " " + std::to_string(player.getFeetPos().y) + " "
            + std::to_string(player.getFeetPos().z) + ", hasChunks " + std::to_string(region.hasChunksAt(BlockPos{x, 0, z}, 0, false))
            + ", alive " + std::to_string(player.isAlive()) + ")"
    );
    auto y = groundY(region, x, z);
    player.teleport(Vec3{static_cast<float>(x) + 0.5f, static_cast<float>(y), static_cast<float>(z) + 0.5f}, player.getDimensionId());
    sp.simulateStopFlying();
    co_await ctx.ticks(10);
}

/// Runs the player's real "use item on block" path (what a right click sends).
inline void useOn(Player& player, BlockPos const& pos, int face = 1) {
    ItemStack item = player.getSelectedItem();
    auto&     region = player.getDimensionBlockSource();
    (void)player.mGameMode->useItemOn(item, pos, static_cast<uchar>(face), pos.center(), HandSlot::Mainhand, &region.getBlock(pos), true);
}

/// Total count of `id` in a vanilla container.
inline int countIn(Container& c, std::string_view id) {
    int n = 0;
    for (int i = 0; i < c.getContainerSize(); ++i) {
        auto const& s = c.getItem(i);
        if (!s.isNull() && s.getTypeName() == id) n += s.mCount;
    }
    return n;
}

inline int countIn(game::Backpack& bp, std::string_view id) {
    return static_cast<int>(bp.inventory().count([&](core::ItemStackData const& s) { return s.id == id; }));
}

} // namespace abp::placed::test
