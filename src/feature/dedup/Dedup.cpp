#include "feature/dedup/Dedup.h"

#include <map>
#include <unordered_map>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/nbt/CompoundTag.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/phys/AABB.h"

#include "feature/admin/SbConfig.h"
#include "feature/linked/Linked.h"
#include "game/BackpackItem.h"
#include "game/PlayerScan.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "ui/menu/BackpackMenu.h"
#include "ui/menu/MenuManager.h"

namespace abp::dedup {

namespace {

constexpr int   kCheckCooldownTicks = 40; ///< SB: BACKPACK_CHECK_COOLDOWN
constexpr float kAreaInflate        = 10.0f;

int slotsOf(ItemStack const& stack) {
    auto tier = game::backpackTierOf(stack);
    return tier ? admin::inventorySlotCount(*tier) : 0;
}

/// A non-endpoint backpack item with a uuid.
std::optional<std::string> dedupeKey(ItemStack const& stack) {
    if (stack.isNull() || !game::isBackpack(stack) || linked::isEndpoint(stack)) return std::nullopt;
    return game::backpackUuid(stack);
}

} // namespace

bool firstPreferred(ItemStack const& first, ItemStack const& second) { return slotsOf(first) >= slotsOf(second); }

void removeUuid(ItemStack& stack) {
    if (stack.isNull() || !stack.mUserData) return;
    auto tag = stack.mUserData->clone();
    tag->remove(game::kUuidKey);
    stack.setUserData(std::move(tag));
}

int dedupeItemEntityInArea(ItemActor& entity, BlockSource& region) {
    auto uuid = dedupeKey(entity.item());
    if (!uuid) return 0;
    auto const& b     = entity.getAABB();
    AABB        area{
        Vec3{b.min.x - kAreaInflate, b.min.y - kAreaInflate, b.min.z - kAreaInflate},
        Vec3{b.max.x + kAreaInflate, b.max.y + kAreaInflate, b.max.z + kAreaInflate}
    };
    ItemStack* keep    = &entity.item();
    int        removed = 0;
    for (auto* actor : region.getEntities(area, 0.0f, ActorType::ItemEntity)) {
        if (!actor || actor == &entity || !actor->isType(ActorType::ItemEntity) || !actor->isAlive()) continue;
        auto& other = static_cast<ItemActor&>(*actor).item();
        if (dedupeKey(other) != uuid) continue;
        if (firstPreferred(*keep, other)) {
            removeUuid(other);
        } else {
            removeUuid(*keep);
            keep = &other;
        }
        ++removed;
    }
    if (removed > 0) log().info("dedup: {} duplicate backpack item(s) of {} lost their uuid", removed, *uuid);
    return removed;
}

int dedupePlayers(int dimension) {
    auto level = ll::service::getLevel();
    if (!level) return 0;
    struct Kept {
        Player*            player;
        game::ItemLocation where;
    };
    std::unordered_map<std::string, Kept> kept;
    int                                   removed = 0;
    level->forEachPlayer([&](Player& player) {
        if (player.isClientSide() || static_cast<int>(player.getDimensionId()) != dimension) return true;
        for (auto const& c : game::scanBackpacks(player)) {
            auto const& stack = game::itemAt(player, c.where);
            auto        uuid  = dedupeKey(stack);
            if (!uuid) continue;
            auto it = kept.find(*uuid);
            if (it == kept.end()) {
                kept.emplace(*uuid, Kept{&player, c.where});
                continue;
            }
            ItemStack existing = game::itemAt(*it->second.player, it->second.where);
            ItemStack current  = stack;
            if (firstPreferred(existing, current)) {
                removeUuid(current);
                game::setItemAt(player, c.where, current);
            } else {
                removeUuid(existing);
                game::setItemAt(*it->second.player, it->second.where, existing);
                it->second = Kept{&player, c.where};
            }
            ++removed;
        }
        return true;
    });
    if (removed > 0) log().info("dedup: {} duplicate backpack(s) in player inventories lost their uuid", removed);
    return removed;
}

int dedupeOnOpen(Player& player, game::ItemLocation openedAt) {
    auto uuid = dedupeKey(game::itemAt(player, openedAt));
    if (!uuid) return 0;
    auto keepWhere = openedAt; // SB: the opened backpack is the first candidate to keep
    int  removed   = 0;
    for (auto const& c : game::scanBackpacks(player)) {
        if (c.where == openedAt || dedupeKey(game::itemAt(player, c.where)) != uuid) continue;
        ItemStack keep  = game::itemAt(player, keepWhere);
        ItemStack other = game::itemAt(player, c.where);
        if (firstPreferred(keep, other)) {
            removeUuid(other);
            game::setItemAt(player, c.where, other);
        } else {
            removeUuid(keep);
            game::setItemAt(player, keepWhere, keep);
            keepWhere = c.where;
        }
        ++removed;
    }
    if (removed > 0) log().info("dedup: {} duplicate(s) of the opened backpack {} lost their uuid", removed, *uuid);
    return removed;
}

namespace {

LL_TYPE_INSTANCE_HOOK(
    DedupSpawnItemHook,
    ll::memory::HookPriority::Normal,
    Spawner,
    &Spawner::$spawnItem,
    ItemActor*,
    BlockSource&     region,
    ItemStack const& inst,
    Actor*           spawner,
    Vec3 const&      pos,
    int              throwTime
) {
    auto* actor = origin(region, inst, spawner, pos, throwTime);
    if (actor && !region.getLevel().isClientSide() && game::isBackpack(inst)) {
        try {
            dedupeItemEntityInArea(*actor, region);
        } catch (std::exception const& e) {
            log().error("dedup: {}", e.what());
        }
    }
    return actor;
}

class DedupModule {
public:
    void install() {
        DedupSpawnItemHook::hook();
        mListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>(
            [this](ll::event::ServerLevelTickEvent& ev) { tick(ev.level()); }
        );
        ui::MenuManager::Observer o;
        o.beforeShow = [](Player& player, ui::Menu& menu, int) {
            auto* bm = dynamic_cast<ui::BackpackMenu*>(&menu);
            if (!bm || !bm->source() || player.isClientSide()) return;
            dedupeOnOpen(player, *bm->source());
        };
        mObserver = ui::MenuManager::get().addObserver(std::move(o));
    }
    void uninstall() {
        ll::event::EventBus::getInstance().removeListener(mListener);
        ui::MenuManager::get().removeObserver(mObserver);
        DedupSpawnItemHook::unhook();
        mNextCheck.clear();
    }

private:
    void tick(Level& level) {
        ++mNow;
        if (admin::sbServer().tickDedupeLogicDisabled) return;
        std::map<int, bool> dims;
        level.forEachPlayer([&](Player& p) {
            if (!p.isClientSide()) dims[static_cast<int>(p.getDimensionId())] = true;
            return true;
        });
        for (auto const& [dim, _] : dims) {
            auto& next = mNextCheck[dim];
            if (next > mNow) continue;
            next = mNow + kCheckCooldownTicks;
            dedupePlayers(dim);
        }
    }

    ll::event::ListenerPtr  mListener;
    int                     mObserver = 0;
    std::int64_t            mNow      = 0;
    std::map<int, std::int64_t> mNextCheck;
};

} // namespace
} // namespace abp::dedup

ABP_REGISTER_MODULE(abp::dedup::DedupModule);
