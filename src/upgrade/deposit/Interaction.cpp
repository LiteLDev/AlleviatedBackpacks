// "Use the backpack on a container" for the Deposit / Restock upgrades.
//
// Like the Java mod, sneaking and using a backpack that has an active Deposit or Restock upgrade
// on a block with an inventory (chest, barrel, shulker box, hopper, dropper, furnace, ...) runs
// those upgrades against that inventory instead of the normal use: neither the container nor the
// backpack GUI opens. The player sees "Deposited N stacks" / "Restocked N stacks" above the hotbar.

#include <chrono>
#include <cstdint>
#include <unordered_map>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/event/player/PlayerUseItemEvent.h"

#include "mc/scripting/modules/minecraft/ScriptFacing.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/Container.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"

#include "feature/admin/SbConfig.h"
#include "game/BackpackItem.h"
#include "game/Storage.h"
#include "mod/Config.h"
#include "mod/Module.h"
#include "selftest/SelfTest.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/deposit/ContainerTransfer.h"
#include "upgrade/deposit/TestSupport.h"

namespace abp::upgrade::transfer {

/// `config/container_interaction.json`
struct InteractionConfig {
    int  version = 1;
    /// Like the Java mod, only sneak + use runs Deposit / Restock (a plain use opens the
    /// container as usual). false: any use of the backpack on a container runs them.
    bool requireSneak = true;
    /// Minimum time between two runs for one player (holding the use button repeats the use).
    int cooldownMs = 300;
};

namespace {

using Clock = std::chrono::steady_clock;

bool hasInteractionUpgrade(std::shared_ptr<game::Backpack> const& bp) {
    return hasActive(*bp, "deposit") || hasActive(*bp, "restock");
}

} // namespace

class ContainerInteraction {
public:
    static ContainerInteraction& get() {
        static ContainerInteraction instance;
        return instance;
    }

    void install() {
        auto& bus = ll::event::EventBus::getInstance();
        mInteract = bus.emplaceListener<ll::event::PlayerInteractBlockEvent>(
            [this](ll::event::PlayerInteractBlockEvent& ev) { onInteract(ev); },
            // Late, so that protection plugins (claims, chest locks) can cancel the use first.
            ll::event::EventPriority::Low
        );
        // The client may follow the block use with a plain item use in the same click; don't let
        // that open the backpack GUI.
        mUse = bus.emplaceListener<ll::event::PlayerUseItemEvent>(
            [this](ll::event::PlayerUseItemEvent& ev) {
                auto it = mLast.find(ev.self().getOrCreateUniqueID().rawID);
                if (it != mLast.end() && Clock::now() - it->second < std::chrono::milliseconds(150)
                    && game::isBackpack(ev.item())) {
                    ev.cancel();
                }
            },
            ll::event::EventPriority::Highest
        );
    }
    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        bus.removeListener(mInteract);
        bus.removeListener(mUse);
        mLast.clear();
    }

    /// Handles one block use. Returns true if the use was taken over (and cancelled).
    bool onInteract(ll::event::PlayerInteractBlockEvent& ev) {
        auto& player = ev.self();
        if (player.isClientSide() || ev.isCancelled()) return false;
        auto const& item = ev.item();
        if (!game::isBackpack(item)) return false;
        auto const& cfg = config::get<InteractionConfig>();
        if (cfg.requireSneak && !player.isSneaking()) return false;
        auto uuid = game::backpackUuid(item);
        if (!uuid || !game::Storage::get().available()) return false;
        auto bp = game::Storage::get().find(*uuid);
        if (!bp || !hasInteractionUpgrade(bp)) return false;
        auto& region = player.getDimensionBlockSource();
        if (admin::isBlockInteractionDisallowed(region.getBlock(ev.blockPos()).getTypeName())) return false; // SB noInteractionBlocks
        auto container = region.tryGetContainer(ev.blockPos());
        if (!container) return false;

        ev.cancel(); // neither open the container nor use the item
        auto key = player.getOrCreateUniqueID().rawID;
        auto now = Clock::now();
        if (auto it = mLast.find(key); it != mLast.end() && now - it->second < std::chrono::milliseconds(cfg.cooldownMs)) {
            it->second = now;
            return true;
        }
        mLast[key] = now;
        if (mLast.size() > 256) std::erase_if(mLast, [&](auto const& e) { return now - e.second > std::chrono::seconds(5); });

        auto result = interact(bp, player, *container, static_cast<int>(ev.face()));
        if (result.handled && !result.message.empty()) sendTip(player, result.message);
        mLastResult = result;
        return true;
    }

    /// The last interaction's result (selftests).
    [[nodiscard]] Interaction const& lastResult() const { return mLastResult; }
    void                             forgetCooldowns() { mLast.clear(); }

private:
    ll::event::ListenerPtr                       mInteract;
    ll::event::ListenerPtr                       mUse;
    std::unordered_map<std::int64_t, Clock::time_point> mLast;
    Interaction                                  mLastResult;
};

} // namespace abp::upgrade::transfer

ABP_REGISTER_CONFIG("container_interaction", abp::upgrade::transfer::InteractionConfig);
ABP_REGISTER_MODULE(abp::upgrade::transfer::ContainerInteraction);

// ---- selftests -----------------------------------------------------------------------------------

namespace abp::upgrade::transfer {
namespace {

using selftest::Context;
using namespace test;

/// Uses the item in hotbar slot 0 on the top of `pos`.
bool useOn(SimulatedPlayer& sp, BlockPos const& pos) {
    return sp.simulateUseItemInSlotOnBlock(0, pos, ScriptModuleMinecraft::ScriptFacing::Up, Vec3{0.5f, 1.0f, 0.5f});
}

ABP_REGISTER_SELFTEST("deposit.use_on_container", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& sp     = ctx.spawnPlayer();
    auto& module = ContainerInteraction::get();
    module.forgetCooldowns();
    co_await settle(ctx, sp);
    auto bp = ctx.createBackpack(core::Tier::Iron);
    int depSlot = ctx.installUpgrade(bp, "deposit");
    int resSlot = ctx.installUpgrade(bp, "restock");
    ctx.require(depSlot < resSlot, "deposit runs first (upgrade slot order)");
    {
        UpgradeContext dep(bp, depSlot, &sp);
        UpgradeContext res(bp, resSlot, &sp);
        setFilter(dep, false, {"minecraft:cobblestone"}, core::FilterMode::Allow);
        setFilter(res, false, {"minecraft:torch"}, core::FilterMode::Allow);
    }
    fill(*bp, "minecraft:cobblestone", 128);
    ctx.require(ctx.give(sp, ctx.backpackItem(*bp), 0) == 0, "give backpack");
    PlacedBlock chest(ctx, sp, "minecraft:chest");
    auto&       c = chest.container();
    c.setItem(0, game::ItemConv::make("minecraft:torch", 32));
    ctx.info(
        "chest at " + std::to_string(chest.pos().x) + "," + std::to_string(chest.pos().y) + "," + std::to_string(chest.pos().z)
    );

    ctx.check(sp.simulateSneaking(), "sneaking");
    (void)useOn(sp, chest.pos());
    co_await ctx.ticks(2);
    ctx.check(countIn(c, "minecraft:cobblestone") == 128, "sneak-use deposited the cobblestone");
    ctx.check(countIn(*bp, "minecraft:torch") == 32 && countIn(c, "minecraft:torch") == 0, "and restocked the torches");
    auto const& last = module.lastResult();
    ctx.check(last.handled && last.deposited.stacks == 2 && last.restocked.stacks == 1, "result counts");
    ctx.check(
        last.message.find("2") != std::string::npos && last.message.find("1") != std::string::npos,
        "message mentions the stack counts: " + last.message
    );
    ctx.check(ui::MenuManager::get().current(sp) == nullptr, "the backpack GUI did not open");
    ctx.check(game::isBackpack(sp.getInventory().getItem(0)), "the backpack stays in hand");

    // A repeated use right away is swallowed by the cooldown (holding the use button).
    fill(*bp, "minecraft:cobblestone", 10);
    (void)useOn(sp, chest.pos());
    co_await ctx.ticks(1);
    ctx.check(countIn(*bp, "minecraft:cobblestone") == 10, "cooldown");
    co_await ctx.ticks(10);

    // Disabled upgrades do nothing.
    module.forgetCooldowns();
    setEnabled(bp, depSlot, false);
    setEnabled(bp, resSlot, false);
    PlacedBlock barrel(ctx, sp, "minecraft:barrel", BlockPos{0, 0, 2});
    (void)useOn(sp, barrel.pos());
    co_await ctx.ticks(10);
    ctx.check(countIn(barrel.container(), "minecraft:cobblestone") == 0, "disabled upgrades are inactive");

    // Without sneaking the container opens as usual (nothing is deposited).
    setEnabled(bp, depSlot, true);
    module.forgetCooldowns();
    sp.simulateStopSneaking();
    co_await ctx.ticks(2);
    (void)useOn(sp, barrel.pos());
    co_await ctx.ticks(2);
    ctx.check(countIn(barrel.container(), "minecraft:cobblestone") == 0, "plain use doesn't deposit");
    ui::MenuManager::get().close(sp);
});

ABP_REGISTER_SELFTEST("deposit.furnace_faces", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& player = ctx.player();
    co_await settle(ctx, player);
    auto  bp     = ctx.createBackpack(core::Tier::Iron);
    int   slot   = ctx.installUpgrade(bp, "deposit");
    UpgradeContext uc(bp, slot, &player);
    PlacedBlock    furnace(ctx, player, "minecraft:furnace");
    auto&          c = furnace.container();
    fill(*bp, "minecraft:raw_iron", 8);
    fill(*bp, "minecraft:coal", 8);
    // Hopper rules: the top feeds the input slot, the sides the fuel slot.
    setFilter(uc, false, {"minecraft:raw_iron"}, core::FilterMode::Allow);
    (void)deposit::run(uc, c, kUp, false);
    ctx.check(c.getItem(0).getTypeName() == "minecraft:raw_iron" && c.getItem(0).mCount == 8, "top: raw iron into the input slot");
    setFilter(uc, false, {"minecraft:coal"}, core::FilterMode::Allow);
    (void)deposit::run(uc, c, kNorth, false);
    ctx.check(c.getItem(1).getTypeName() == "minecraft:coal" && c.getItem(1).mCount == 8, "side: coal into the fuel slot");
    ctx.check(c.getItem(2).isNull(), "nothing into the output slot");
    // Restock pulls from the output slot only from below, like a hopper.
    c.setItem(2, game::ItemConv::make("minecraft:iron_ingot", 4));
    UpgradeContext rc(bp, ctx.installUpgrade(bp, "restock"), &player);
    (void)restock::run(rc, c, kDown, false);
    ctx.check(countIn(*bp, "minecraft:iron_ingot") == 4, "bottom: output taken");
});

} // namespace
} // namespace abp::upgrade::transfer
