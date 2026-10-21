#include "game/ItemConv.h"

#include <algorithm>
#include <mutex>
#include <unordered_map>

#include "mc/deps/nbt/CompoundTag.h"
#include "mc/safety/RedactableString.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/ItemTag.h"
#include "mc/world/item/SaveContext.h"

namespace abp::game::ItemConv {

namespace {
// Keys of the saved item tag that are not part of the identity.
constexpr std::string_view kVolatileKeys[] = {"Count", "WasPickedUp"};

std::unordered_map<std::string, std::vector<std::string>> gTagCache;
std::mutex                                                gTagMutex;
} // namespace

std::string idOf(ItemStack const& stack) {
    if (stack.isNull()) return {};
    return stack.getTypeName();
}

core::ItemStackData toData(ItemStack const& stack) {
    core::ItemStackData d;
    if (stack.isNull() || stack.mCount == 0) return d;
    d.id       = stack.getTypeName();
    d.aux      = stack.getAuxValue();
    d.count    = stack.mCount;
    d.maxStack = std::max<int>(stack.getMaxStackSize(), 1);
    if (stack.isDamageableItem()) {
        d.damage = stack.getDamageValue();
        if (auto* item = stack.mItem.get()) d.maxDamage = item->getMaxDamage();
    }
    if (auto tag = stack.save(SaveContext{SaveContext::SaveUseCase::SaveToDisk})) {
        for (auto key : kVolatileKeys) tag->erase(key);
        d.setNbt(tag->toBinaryNbt());
    }
    return d;
}

ItemStack make(std::string_view id, int count, int aux) {
    ItemStack s;
    s.reinit(id, std::clamp(count, 1, 255), aux);
    return s;
}

ItemStack toStack(core::ItemStackData const& data, int count) {
    if (data.empty()) return ItemStack{};
    if (count < 0) count = data.count;
    count = std::clamp(count, 1, 255);
    if (!data.nbt().empty()) {
        if (auto tag = CompoundTag::fromBinaryNbt(data.nbt())) {
            tag->putByte("Count", static_cast<uchar>(count));
            auto s = ItemStack::fromTag(*tag);
            if (!s.isNull()) return s;
        }
    }
    return make(data.id, count, data.aux);
}

void setDisplay(ItemStack& stack, std::string const& name, std::vector<std::string> const& lore) {
    if (stack.isNull()) return;
    if (!name.empty()) stack.setCustomName(Bedrock::Safety::RedactableString(name));
    if (!lore.empty()) stack.setCustomLore(lore);
}

namespace {
std::vector<std::string> const& cachedTags(core::ItemStackData const& item) {
    std::lock_guard lock(gTagMutex);
    auto            it = gTagCache.find(item.id);
    if (it != gTagCache.end()) return it->second;
    std::vector<std::string> tags;
    auto                     stack = make(item.id, 1, item.aux);
    if (auto* it2 = stack.mItem.get()) {
        for (auto const& t : *it2->mTags) tags.push_back(t.getString());
    }
    return gTagCache.emplace(item.id, std::move(tags)).first->second;
}
} // namespace

bool TagProvider::hasTag(core::ItemStackData const& item, std::string_view tag) const {
    auto const& tags = cachedTags(item);
    return std::find(tags.begin(), tags.end(), tag) != tags.end();
}

std::vector<std::string> TagProvider::tagsOf(core::ItemStackData const& item) const { return cachedTags(item); }

TagProvider const& tags() {
    static TagProvider p;
    return p;
}

} // namespace abp::game::ItemConv
