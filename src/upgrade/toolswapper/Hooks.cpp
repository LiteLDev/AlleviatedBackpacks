// Game hooks that drive the Tool Swapper:
// - start of block breaking (GameMode / SurvivalMode::startDestroyBlock and the server-authoritative
//   ServerPlayerBlockUseHandler::onStartDestroyBlock) -> best tool, before the break speed is used;
// - attacks (GameMode / SurvivalMode::attack, plus LeviLamina's PlayerAttackEvent) -> best weapon,
//   before the damage is computed;
// - empty-hand interaction with a shearable mob, or sneak-use of a log/dirt/... block with an empty
//   hand -> shears/axe/hoe/shovel. These swaps run on the next tick, so the vanilla interaction that
//   is in progress never sees (or overwrites) the swapped item.
// Each hook is idempotent: once the right item is held, the next hook in the chain does nothing.

#include <cstdint>
#include <set>
#include <tuple>
#include <unordered_map>
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

struct PendingUse {
    bool         block = false;
    BlockPos     pos{};
    std::int64_t actor = 0;
};
std::unordered_map<std::int64_t, PendingUse> gPending; // player unique id -> interaction to serve
/// (player, action, x, y, z) already evaluated this tick: several hooks see the same action (e.g.
/// SurvivalMode -> GameMode -> Player::attack); only the first one searches the backpacks.
std::set<std::tuple<std::int64_t, int, int, int, int>> gSeenThisTick;
bool                                                   gInstalled = false;

/// Cheap pre-filter; `swapBest` does the full checks (alive, creative, open menu, config).
bool eligible(Player& p) { return gInstalled && !p.isClientSide(); }

bool firstThisTick(Player& p, int action, BlockPos const& pos = {}) {
    return gSeenThisTick.emplace(p.getOrCreateUniqueID().rawID, action, pos.x, pos.y, pos.z).second;
}

void onStartBreak(Player& p, BlockPos const& pos) {
    if (!eligible(p) || !firstThisTick(p, 0, pos)) return;
    try {
        swapForBlock(p, p.getDimensionBlockSourceConst().getBlock(pos));
    } catch (std::exception const& e) {
        log().error("tool swapper (block) failed: {}", e.what());
    }
}

void onAttack(Player& p) {
    if (!eligible(p) || !firstThisTick(p, 1)) return;
    try {
        swapForAttack(p);
    } catch (std::exception const& e) {
        log().error("tool swapper (attack) failed: {}", e.what());
    }
}

void onUseBlock(Player& p, ItemStack const& item, BlockPos const& pos, Block const* block) {
    if (!eligible(p) || !item.isNull() || !p.isSneaking()) return;
    Block const& b = block ? *block : p.getDimensionBlockSourceConst().getBlock(pos);
    if (useToolFor(b).empty()) return;
    gPending[p.getOrCreateUniqueID().rawID] = PendingUse{true, pos, 0};
}

void onInteractEntity(Player& p, Actor& target) {
    if (!eligible(p) || !p.getSelectedItem().isNull() || !isShearable(target)) return;
    gPending[p.getOrCreateUniqueID().rawID] = PendingUse{false, {}, target.getOrCreateUniqueID().rawID};
}

void onTick(Level& level) {
    gSeenThisTick.clear();
    if (gPending.empty()) return;
    auto pending = std::move(gPending);
    gPending.clear();
    for (auto const& [playerId, use] : pending) {
        auto* p = level.getPlayer(ActorUniqueID{playerId});
        if (!p || !eligible(*p)) continue;
        try {
            if (use.block) {
                swapForBlockUse(*p, p->getDimensionBlockSourceConst().getBlock(use.pos));
            } else if (auto* target = level.fetchEntity(ActorUniqueID{use.actor}, false)) {
                swapForEntityUse(*p, *target);
            }
        } catch (std::exception const& e) {
            log().error("tool swapper (use) failed: {}", e.what());
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
