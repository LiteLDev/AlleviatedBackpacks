// Game hooks that drive the Tool Swapper:
// - start of block breaking (GameMode / SurvivalMode::startDestroyBlock and the server-authoritative
//   ServerPlayerBlockUseHandler::onStartDestroyBlock) -> best tool;
// - attacks (GameMode / SurvivalMode::attack, plus LeviLamina's PlayerAttackEvent) -> best weapon;
// - empty-hand interaction with a shearable mob, or sneak-use of a log/dirt/... block with an empty
//   hand -> shears/axe/hoe/shovel.
//
// The hooks only record what happened; the swap itself runs at the start of the next server tick.
// Swapping inside the hook is not safe on a real client: the input packet being processed still
// holds a copy of the old main-hand item and writes it back afterwards, which would duplicate the
// held item (now also in the backpack) and delete the tool. Breaking continues server-side with
// the new tool, and the inventory is re-sent so the client predicts with it too.

#include <cstdint>
#include <set>
#include <tuple>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerAttackEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"

#include "mc/network/ServerPlayerBlockUseHandler.h"
#include "mc/server/ServerPlayer.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/gamemode/InteractionResult.h"
#include "mc/world/gamemode/SurvivalMode.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"

#include "mod/Log.h"
#include "mod/Module.h"
#include "upgrade/toolswapper/ToolSwapper.h"

namespace abp::upgrade::toolswapper {

namespace {

enum class Action : int { Break = 0, Attack = 1, UseBlock = 2, UseEntity = 3 };

struct Pending {
    std::int64_t player = 0;
    Action       action = Action::Break;
    BlockPos     pos{};
    std::int64_t actor = 0;
};
std::vector<Pending> gPending; // served on the next tick, in order
/// (player, action, x, y, z) already queued this tick: several hooks see the same action (e.g.
/// SurvivalMode -> GameMode -> Player::attack); it is served once.
std::set<std::tuple<std::int64_t, int, int, int, int>> gSeenThisTick;
bool                                                   gInstalled = false;

/// Cheap pre-filter; `swapBest` does the full checks (alive, creative, open menu, config).
bool eligible(Player& p) { return gInstalled && !p.isClientSide(); }

void queue(Player& p, Action action, BlockPos const& pos = {}, std::int64_t actor = 0) {
    auto id = p.getOrCreateUniqueID().rawID;
    if (!gSeenThisTick.emplace(id, static_cast<int>(action), pos.x, pos.y, pos.z).second) return;
    gPending.push_back(Pending{id, action, pos, actor});
}

void onStartBreak(Player& p, BlockPos const& pos) {
    if (eligible(p)) queue(p, Action::Break, pos);
}

void onAttack(Player& p) {
    if (eligible(p)) queue(p, Action::Attack);
}

void onUseBlock(Player& p, ItemStack const& item, BlockPos const& pos, Block const* block) {
    if (!eligible(p) || !item.isNull() || !p.isSneaking()) return;
    Block const& b = block ? *block : p.getDimensionBlockSourceConst().getBlock(pos);
    if (!useToolFor(b).empty()) queue(p, Action::UseBlock, pos);
}

void onInteractEntity(Player& p, Actor& target) {
    if (!eligible(p) || !p.getSelectedItem().isNull() || !isShearable(target)) return;
    queue(p, Action::UseEntity, {}, target.getOrCreateUniqueID().rawID);
}

bool serve(Level& level, Player& p, Pending const& job) {
    auto const& region = p.getDimensionBlockSourceConst();
    switch (job.action) {
    case Action::Break:
        return swapForBlock(p, region.getBlock(job.pos));
    case Action::Attack:
        return swapForAttack(p);
    case Action::UseBlock:
        return swapForBlockUse(p, region.getBlock(job.pos));
    case Action::UseEntity:
        if (auto* target = level.fetchEntity(ActorUniqueID{job.actor}, false)) return swapForEntityUse(p, *target);
        return false;
    }
    return false;
}

void onTick(Level& level) {
    gSeenThisTick.clear();
    if (gPending.empty()) return;
    auto pending = std::move(gPending);
    gPending.clear();
    for (auto const& job : pending) {
        auto* p = level.getPlayer(ActorUniqueID{job.player});
        if (!p || !eligible(*p)) continue;
        try {
            if (serve(level, *p, job)) p->refreshInventory();
        } catch (std::exception const& e) {
            log().error("tool swapper failed: {}", e.what());
        }
    }
}

// LL_TYPE_INSTANCE_HOOK declares an unused `_AutoHookCount` in manual-registration mode.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-variable"

// ---- block breaking ----------------------------------------------------------------------------

LL_TYPE_INSTANCE_HOOK(
    TsGameModeStartDestroyHook,
    ll::memory::HookPriority::Normal,
    GameMode,
    &GameMode::$startDestroyBlock,
    bool,
    BlockPos const& pos,
    uchar           face,
    bool&           hasDestroyedBlock
) {
    onStartBreak(this->mPlayer, pos);
    return origin(pos, face, hasDestroyedBlock);
}

LL_TYPE_INSTANCE_HOOK(
    TsSurvivalStartDestroyHook,
    ll::memory::HookPriority::Normal,
    SurvivalMode,
    &SurvivalMode::$startDestroyBlock,
    bool,
    BlockPos const& pos,
    uchar           face,
    bool&           hasDestroyedBlock
) {
    onStartBreak(this->mPlayer, pos);
    return origin(pos, face, hasDestroyedBlock);
}

LL_STATIC_HOOK(
    TsServerStartDestroyHook,
    ll::memory::HookPriority::Normal,
    &ServerPlayerBlockUseHandler::onStartDestroyBlock,
    void,
    ServerPlayer&   player,
    BlockPos const& pos,
    int             face
) {
    onStartBreak(player, pos);
    origin(player, pos, face);
}

// ---- attacks -----------------------------------------------------------------------------------

LL_TYPE_INSTANCE_HOOK(
    TsGameModeAttackHook,
    ll::memory::HookPriority::Normal,
    GameMode,
    &GameMode::$attack,
    bool,
    Actor&      entity,
    Vec3 const& hitPosition
) {
    onAttack(this->mPlayer);
    return origin(entity, hitPosition);
}

LL_TYPE_INSTANCE_HOOK(
    TsSurvivalAttackHook,
    ll::memory::HookPriority::Normal,
    SurvivalMode,
    &SurvivalMode::$attack,
    bool,
    Actor&      entity,
    Vec3 const& hitPosition
) {
    onAttack(this->mPlayer);
    return origin(entity, hitPosition);
}

// ---- interactions ------------------------------------------------------------------------------

LL_TYPE_INSTANCE_HOOK(
    TsGameModeUseItemOnHook,
    ll::memory::HookPriority::Normal,
    GameMode,
    &GameMode::$useItemOn,
    InteractionResult,
    ItemStack&      item,
    BlockPos const& at,
    uchar           face,
    Vec3 const&     hit,
    HandSlot        handSlot,
    Block const*    targetBlock,
    bool            isFirstEvent
) {
    if (isFirstEvent) onUseBlock(this->mPlayer, item, at, targetBlock);
    return origin(item, at, face, hit, handSlot, targetBlock, isFirstEvent);
}

LL_TYPE_INSTANCE_HOOK(
    TsSurvivalUseItemOnHook,
    ll::memory::HookPriority::Normal,
    SurvivalMode,
    &SurvivalMode::$useItemOn,
    InteractionResult,
    ItemStack&      item,
    BlockPos const& at,
    uchar           face,
    Vec3 const&     hit,
    HandSlot const  handSlot,
    Block const*    targetBlock,
    bool            isFirstEvent
) {
    if (isFirstEvent) onUseBlock(this->mPlayer, item, at, targetBlock);
    return origin(item, at, face, hit, handSlot, targetBlock, isFirstEvent);
}

LL_TYPE_INSTANCE_HOOK(
    TsGameModeInteractHook,
    ll::memory::HookPriority::Normal,
    GameMode,
    &GameMode::$interact,
    bool,
    Actor&      entity,
    Vec3 const& location,
    HandSlot    handSlot
) {
    onInteractEntity(this->mPlayer, entity);
    return origin(entity, location, handSlot);
}

LL_TYPE_INSTANCE_HOOK(
    TsSurvivalInteractHook,
    ll::memory::HookPriority::Normal,
    SurvivalMode,
    &SurvivalMode::$interact,
    bool,
    Actor&         entity,
    Vec3 const&    location,
    HandSlot const handSlot
) {
    onInteractEntity(this->mPlayer, entity);
    return origin(entity, location, handSlot);
}

#pragma clang diagnostic pop

} // namespace

class ToolSwapperHooks {
public:
    void install() {
        TsGameModeStartDestroyHook::hook();
        TsSurvivalStartDestroyHook::hook();
        TsServerStartDestroyHook::hook();
        TsGameModeAttackHook::hook();
        TsSurvivalAttackHook::hook();
        TsGameModeUseItemOnHook::hook();
        TsSurvivalUseItemOnHook::hook();
        TsGameModeInteractHook::hook();
        TsSurvivalInteractHook::hook();
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerAttackEvent>(
            [](ll::event::PlayerAttackEvent& ev) { onAttack(ev.self()); },
            ll::event::EventPriority::High
        ));
        mListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>(
            [](ll::event::ServerLevelTickEvent& ev) { onTick(ev.level()); }
        ));
        gInstalled = true;
    }

    void uninstall() {
        gInstalled = false;
        gPending.clear();
        gSeenThisTick.clear();
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
        TsGameModeStartDestroyHook::unhook();
        TsSurvivalStartDestroyHook::unhook();
        TsServerStartDestroyHook::unhook();
        TsGameModeAttackHook::unhook();
        TsSurvivalAttackHook::unhook();
        TsGameModeUseItemOnHook::unhook();
        TsSurvivalUseItemOnHook::unhook();
        TsGameModeInteractHook::unhook();
        TsSurvivalInteractHook::unhook();
    }

private:
    std::vector<ll::event::ListenerPtr> mListeners;
};

} // namespace abp::upgrade::toolswapper

ABP_REGISTER_MODULE(abp::upgrade::toolswapper::ToolSwapperHooks);
