#pragma once

// Selftest helpers shared by the Deposit / Restock selftests (header-only, server thread).

#include <initializer_list>
#include <string>
#include <string_view>

#include "ll/api/coro/CoroTask.h"

#include "mc/world/Container.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"

#include "game/Backpack.h"
#include "game/ItemConv.h"
#include "selftest/SelfTest.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/IUpgrade.h"
#include "upgrade/deposit/ContainerTransfer.h"

namespace abp::upgrade::transfer::test {

inline constexpr int kDown = 0;
inline constexpr int kUp   = 1;
inline constexpr int kNorth = 2;

inline core::ItemStackData itemData(std::string_view id, int count) {
    return game::ItemConv::toData(game::ItemConv::make(id, count));
}

/// Inserts `count` items of `id` into the backpack (as many stacks as needed).
inline void fill(game::Backpack& bp, std::string_view id, int count) {
    auto one = itemData(id, 1);
    (void)bp.inventory().insert(one.withCount(count), core::AccessContext{core::Source::Internal});
}

inline long long countIn(game::Backpack& bp, std::string_view id) {
    return bp.inventory().count([&](core::ItemStackData const& s) { return s.id == id; });
}

inline long long countIn(Container& c, std::string_view id) {
    long long n = 0;
    for (int i = 0; i < c.getContainerSize(); ++i) {
        auto const& it = c.getItem(i);
        if (!it.isNull() && it.getTypeName() == id) n += it.mCount;
    }
    return n;
}

inline void setFilter(UpgradeContext const& ctx, bool advanced, std::initializer_list<std::string_view> ids, core::FilterMode mode) {
    core::Filter f(filterSlots(advanced), core::FilterDefaults{});
    f.mode = mode;
    int i  = 0;
    for (auto id : ids) f.setGhost(i++, itemData(id, 1));
    ctx.settings()[std::string(kFilterKey)] = f.toJson();
    ctx.saveSettings(false);
}

/// Sets the type button's third state: Deposit "Match Inventory Contents" / Restock "Match
/// Backpack Contents".
inline void setContents(UpgradeContext const& ctx, bool advanced, bool on) {
    auto f              = loadFilter(ctx, advanced);
    f.filterByInventory = on;
    f.filterByStorage   = on;
    ctx.settings()[std::string(kFilterKey)] = f.toJson();
    ctx.saveSettings(false);
}

inline std::string describeResult(Result const& r) {
    return "stacks=" + std::to_string(r.stacks) + " items=" + std::to_string(r.items);
}

/// Moves the player onto the ground at its x/z (simulated players spawn at the world spawn, which
/// can be far up in the air on a dedicated server).
inline void standOnGround(Player& player) {
    auto feet = player.getFeetBlockPos();
    auto y    = player.getDimensionBlockSource().getAboveTopSolidBlock(feet.x, feet.z, false, false);
    player.teleport(
        Vec3{static_cast<float>(feet.x) + 0.5f, static_cast<float>(y), static_cast<float>(feet.z) + 0.5f},
        player.getDimensionId()
    );
}

/// Waits until the chunks around the player are loaded (a just-spawned simulated player's area may
/// still be loading), then puts it on the ground.
inline ll::coro::CoroTask<> settle(selftest::Context& ctx, Player& player) {
    for (int i = 0; i < 200; ++i) {
        auto& region = player.getDimensionBlockSource();
        auto  feet   = player.getFeetBlockPos();
        bool  ready  = true;
        for (int dx = -1; dx <= 1 && ready; ++dx) {
            for (int dz = -1; dz <= 1 && ready; ++dz) {
                int x = feet.x + dx * 16, z = feet.z + dz * 16;
                ready = region.getChunkAt(BlockPos{x, 0, z}) != nullptr
                     && region.getAboveTopSolidBlock(x, z, false, false) < region.getMaxHeight();
            }
        }
        if (ready) break;
        co_await ctx.ticks(1);
    }
    standOnGround(player);
    co_await ctx.ticks(5);
    ctx.info(
        "player at " + std::to_string(player.getFeetBlockPos().x) + "," + std::to_string(player.getFeetBlockPos().y) + ","
        + std::to_string(player.getFeetBlockPos().z)
    );
}

/// Places a container block next to the player for the duration of a test; empties and removes
/// it afterwards.
class PlacedBlock {
public:
    PlacedBlock(selftest::Context& ctx, Player& player, std::string const& blockId, BlockPos offset = {2, 0, 0})
    : mRegion(player.getDimensionBlockSource()) {
        // On the ground next to the player (a fresh simulated player may still be high in the air).
        auto feet = player.getFeetBlockPos();
        int  x    = feet.x + offset.x;
        int  z    = feet.z + offset.z;
        mPos      = BlockPos{x, mRegion.getAboveTopSolidBlock(x, z, false, false) + offset.y, z};
        auto block = Block::tryGetFromRegistry(HashedString(blockId));
        if (!block) ctx.fail("unknown block " + blockId);
        bool set       = mRegion.setBlock(mPos, *block, 3, nullptr, BlockChangeContext{});
        auto container = mRegion.tryGetContainer(mPos);
        if (!container) {
            std::string why = "no container at the placed " + blockId + " (setBlock=" + (set ? "1" : "0")
                            + ", block there=" + mRegion.getBlock(mPos).getTypeName()
                            + ", blockActor=" + (mRegion.getBlockEntity(mPos) ? "1" : "0") + ", pos=" + std::to_string(mPos.x)
                            + "," + std::to_string(mPos.y) + "," + std::to_string(mPos.z) + ")";
            clear();
            ctx.fail(why);
        }
        mContainer = container.as_ptr();
    }
    ~PlacedBlock() { clear(); }
    PlacedBlock(PlacedBlock const&)            = delete;
    PlacedBlock& operator=(PlacedBlock const&) = delete;

    [[nodiscard]] Container&      container() const { return *mContainer; }
    [[nodiscard]] BlockPos const& pos() const { return mPos; }

private:
    void clear() {
        if (mContainer) mContainer->removeAllItems();
        mContainer = nullptr;
        if (auto air = Block::tryGetFromRegistry(HashedString("minecraft:air"))) {
            mRegion.setBlock(mPos, *air, 3, nullptr, BlockChangeContext{});
        }
    }

    BlockSource& mRegion;
    BlockPos     mPos;
    Container*   mContainer = nullptr;
};

/// Opens the advanced upgrade's settings menu and toggles the "contents" mode button.
inline ll::coro::CoroTask<> settingsMenuTest(selftest::Context& ctx, std::string upgradeId) {
    using Where  = ui::MenuManager::SlotAddress::Where;
    using Action = ui::MenuManager::ActionType;
    auto& mm     = ui::MenuManager::get();
    auto& player = ctx.player();
    auto  bp     = ctx.createBackpack(core::Tier::Iron);
    int   slot   = ctx.installUpgrade(bp, upgradeId);
    UpgradeContext uc(bp, slot, &player);
    auto*          type = byId(upgradeId);
    ctx.require(type != nullptr && type->info().hasSettings, "upgrade has settings");
    auto menu = type->createSettingsMenu(player, uc);
    ctx.require(menu != nullptr, "settings menu");
    ctx.require(mm.open(player, menu), "open settings menu");
    mm.forceShown(player);
    ctx.check(menu->size() == 27, "16 filter slots + control row fit a single chest");
    co_await ctx.ticks(2);

    // Ghost slot 15 (advanced has 16).
    ctx.require(ctx.give(player, game::ItemConv::make("minecraft:redstone", 3), 4) == 4, "give");
    ctx.check(mm.simulate(player, {Action::Take, {Where::Inventory, 4}, {Where::Cursor, 0}, 3}), "to cursor");
    (void)mm.simulate(player, {Action::Place, {Where::Cursor, 0}, {Where::Menu, 15}, 3});
    auto f = core::Filter::fromJson(uc.settings()[std::string(kFilterKey)], 16);
    ctx.check(f.ghosts[15].id == "minecraft:redstone", "ghost saved in settings");
    (void)mm.simulate(player, {Action::Place, {Where::Cursor, 0}, {Where::Inventory, 4}, 3});

    ctx.check(!contentsMode(uc, true), "contents mode off by default");
    ctx.check(loadFilter(uc, true).mode == core::FilterMode::Deny, "a new filter is a block list (SB)");
    (void)mm.simulate(player, {Action::Take, {Where::Menu, 18 + 5}, {Where::Cursor, 0}, 1});
    ctx.check(contentsMode(uc, true), "contents button toggles the mode");
    auto lore = type->describe(player, uc);
    ctx.check(!lore.empty(), "describe() has lore");
    mm.close(player);
}

} // namespace abp::upgrade::transfer::test
