// SB BackpackItem.makesPiglinsNeutral: a Gold Backpack worn in the chest slot counts as gold armour,
// so piglins leave its wearer alone (unless something else angers them).
//
// Bedrock piglins pick players through `minecraft:behavior.nearest_attackable_target`; the player
// entry's filter is "player and not wearing golden_helmet / golden_chestplate / golden_leggings /
// golden_boots" (`has_equipment`, re-evaluated while targeting). Its other entries (an open
// container, block breaking, hurt-by) don't look at armour. `TargetGoal::_matchesTargetTypes`
// returns the first entry that matches a candidate: when that is the gold-armour entry for a piglin
// and a player wearing a Gold Backpack, the entry is skipped and the remaining entries decide.

#include <atomic>
#include <string>
#include <string_view>
#include <variant>

#include "ll/api/memory/Hook.h"

#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/world/actor/ActorFilterGroup.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/ai/goal/MobDescriptor.h"
#include "mc/world/actor/ai/goal/target/TargetGoal.h"
#include "mc/world/filters/FilterGroup.h"
#include "mc/world/filters/FilterTest.h"
#include "mc/world/item/ItemStack.h"

#include "feature/wearable/Wearable.h"
#include "game/BackpackItem.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace abp::wearable {

bool makesPiglinsNeutral(Actor const& wearer) {
    if (!wearer.isPlayer()) return false;
    auto const& chest = wearer.getArmor(SharedTypes::Legacy::ArmorSlot::Torso);
    return !chest.isNull() && chest.getTypeName() == std::string(game::kNamespace) + ":gold_backpack";
}

bool isGoldArmorFilter(FilterGroup const& group) {
    for (auto const& test : *group.mMembers) {
        if (!test || test->getName() != "has_equipment") continue;
        auto value = test->getValue();
        if (!value) continue;
        if (auto const* s = std::get_if<std::string>(&*value); s && s->find("golden_") != std::string::npos) return true;
    }
    for (auto const& child : *group.mChildren) {
        if (child && isGoldArmorFilter(*child)) return true;
    }
    return false;
}

namespace {

std::atomic<int> gSkipped{0};

// LL_TYPE_INSTANCE_HOOK declares an unused counter in manual-registration mode.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-variable"

LL_TYPE_INSTANCE_HOOK(
    AbpPiglinGoldBackpackHook,
    ll::memory::HookPriority::Normal,
    TargetGoal,
    &TargetGoal::_matchesTargetTypes,
    bool,
    ::Mob*                  testMob,
    ::Actor*                target,
    bool                    mustSee,
    ::MobDescriptor const** outDescriptorMatch
) {
    ::MobDescriptor const* matched = nullptr;
    bool                   ok      = origin(testMob, target, mustSee, &matched);
    ::Mob*                 piglin  = testMob ? testMob : &static_cast<::Mob&>(mMob);
    if (ok && matched && target && piglin->getEntityTypeId() == ActorType::Piglin && makesPiglinsNeutral(*target)) {
        auto& filter = const_cast<::MobDescriptor*>(matched)->mTargetFilter.get();
        if (isGoldArmorFilter(filter)) {
            // The entry matched, so every part of it passes: as "none_of" it fails for this player,
            // and the entries after it get their turn. Restored before anything else runs.
            // The filter belongs to the piglin definition (shared by every piglin): restored on
            // every way out.
            struct Restore {
                FilterGroup&                group;
                FilterGroup::CollectionType saved;
                ~Restore() { group.mCollectionType = saved; }
            } restore{filter, filter.mCollectionType};
            filter.mCollectionType = FilterGroup::CollectionType::Not;
            matched                = nullptr;
            ok                     = origin(testMob, target, mustSee, &matched);
            if (!ok) gSkipped.fetch_add(1, std::memory_order_relaxed);
        }
    }
    if (outDescriptorMatch && ok) *outDescriptorMatch = matched;
    return ok;
}

#pragma clang diagnostic pop

class PiglinNeutral {
public:
    void install() { AbpPiglinGoldBackpackHook::hook(); }
    void uninstall() { AbpPiglinGoldBackpackHook::unhook(); }
};

} // namespace

int piglinTargetsSkipped() { return gSkipped.load(std::memory_order_relaxed); }

} // namespace abp::wearable

ABP_REGISTER_MODULE(abp::wearable::PiglinNeutral);
