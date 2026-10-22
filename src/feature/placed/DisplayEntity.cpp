#include "feature/placed/DisplayEntity.h"

#include <bitset>
#include <cmath>

#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/world/actor/Actor.h"
#include "mc/entity/components/ActorRotationComponent.h"
#include "mc/world/actor/BuiltInActorComponents.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/phys/AABB.h"

#include "game/ItemConv.h"
#include "mod/Log.h"

namespace abp::placed::display {

namespace {

using SharedTypes::Legacy::ArmorSlot;

ItemStack markedCopy(ItemStack const& src) {
    ItemStack copy(src);
    copy.set(1);
    auto tag = copy.mUserData ? copy.mUserData->clone() : std::make_unique<CompoundTag>();
    tag->putByte(std::string(kCopyKey), 1);
    copy.setUserData(std::move(tag));
    return copy;
}

bool same(ItemStack const& a, ItemStack const& b) {
    if (a.isNull() || b.isNull()) return a.isNull() == b.isNull();
    return game::ItemConv::toData(a) == game::ItemConv::toData(b);
}

/// Bedrock yaw of a backpack whose front looks towards `facing` (0 south, 1 west, 2 north, 3 east).
float yawOf(int facing) { return static_cast<float>((facing & 3) * 90); }

AABB boxOf(BlockLocation const& loc) {
    return AABB(Vec3{static_cast<float>(loc.x), static_cast<float>(loc.y), static_cast<float>(loc.z)},
                Vec3{static_cast<float>(loc.x + 1), static_cast<float>(loc.y + 1), static_cast<float>(loc.z + 1)});
}

std::vector<Actor*> displaysAt(BlockSource& region, BlockLocation const& loc) {
    std::vector<Actor*> out;
    for (auto* a : region.getEntities(boxOf(loc), 0.0f)) {
        if (a && a->isAlive() && a->getTypeName() == kEntityId) out.push_back(a);
    }
    return out;
}

} // namespace

bool isCopy(ItemStack const& stack) {
    return !stack.isNull() && stack.mUserData && stack.mUserData->contains(kCopyKey);
}

Actor* find(BlockSource& region, BlockLocation const& loc) {
    auto all = displaysAt(region, loc);
    return all.empty() ? nullptr : all.front();
}

void remove(BlockSource& region, BlockLocation const& loc) {
    for (auto* a : displaysAt(region, loc)) {
        a->setArmor(ArmorSlot::Torso, ItemStack::EMPTY_ITEM());
        a->setCarriedItem(ItemStack::EMPTY_ITEM());
        a->remove();
    }
}

void sync(BlockSource& region, PlacedEntry const& entry, ItemStack const& backpack, std::optional<ItemStack> const& displayItem) {
    auto all = displaysAt(region, entry.loc);
    // One per backpack: extras (a chunk saved with one and a fresh spawn) go away.
    for (std::size_t i = 1; i < all.size(); ++i) {
        all[i]->setArmor(ArmorSlot::Torso, ItemStack::EMPTY_ITEM());
        all[i]->setCarriedItem(ItemStack::EMPTY_ITEM());
        all[i]->remove();
    }
    Actor* actor = all.empty() ? nullptr : all.front();
    Vec3   at{static_cast<float>(entry.loc.x) + 0.5f, static_cast<float>(entry.loc.y), static_cast<float>(entry.loc.z) + 0.5f};
    if (!actor) {
        actor = region.getLevel().getSpawner().spawnMob(region, ActorDefinitionIdentifier(std::string(kEntityId)), nullptr, at, false, false, false);
        if (!actor) {
            log().warn("could not spawn the display of the placed backpack at {} {} {}", entry.loc.x, entry.loc.y, entry.loc.z);
            return;
        }
    }
    float yaw   = yawOf(entry.facing);
    bool  moved = std::abs(actor->getPosition().x - at.x) > 0.01f || std::abs(actor->getPosition().z - at.z) > 0.01f
              || std::abs(actor->getFeetPos().y - at.y) > 0.01f;
    float diff  = std::fmod(std::abs(actor->getRotation().y - yaw), 360.0f);
    bool  turn  = diff > 0.5f && diff < 359.5f;
    if (turn) {
        auto& rot     = *actor->mBuiltInComponents->mActorRotationComponent;
        rot.mRot->y     = yaw;
        rot.mRotPrev->y = yaw;
        actor->setYHeadRot(yaw);
    }
    if (moved || turn) actor->teleportTo(at, true, 0, 0, false);
    if (actor->hasCategory(ActorCategory::Mob)) static_cast<Mob*>(actor)->setYBodyRotations(yaw, yaw);

    auto wanted = markedCopy(backpack);
    bool armorChanged = !same(actor->getArmor(ArmorSlot::Torso), wanted);
    if (armorChanged) actor->setArmor(ArmorSlot::Torso, wanted);
    ItemStack hand = displayItem ? markedCopy(*displayItem) : ItemStack::EMPTY_ITEM();
    if (!same(actor->getCarriedItem(), hand)) actor->setCarriedItem(hand);
    if (armorChanged && actor->hasCategory(ActorCategory::Mob)) static_cast<Mob*>(actor)->sendArmor(std::bitset<5>{0b11111});
}

int purgeCopies(Player& player) {
    int   removed = 0;
    auto& inv     = player.getInventory();
    for (int i = 0; i < inv.getContainerSize(); ++i) {
        if (isCopy(inv.getItem(i))) {
            inv.setItem(i, ItemStack::EMPTY_ITEM());
            ++removed;
        }
    }
    if (removed > 0) {
        log().warn("removed {} display copies from {}'s inventory", removed, player.getRealName());
        player.sendInventory(false);
    }
    return removed;
}

} // namespace abp::placed::display
