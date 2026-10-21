#include "game/BackpackItem.h"

#include "mc/deps/nbt/CompoundTag.h"
#include "mc/world/item/ItemStack.h"

#include "game/ItemConv.h"

namespace abp::game {

std::string backpackItemId(core::Tier tier) {
    return std::string(kNamespace) + ":" + std::string(core::tierInfo(tier).id) + "_backpack";
}

std::optional<core::Tier> backpackTierOf(std::string_view id) {
    constexpr std::string_view suffix = "_backpack";
    if (id.size() <= kNamespace.size() + 1 + suffix.size()) return std::nullopt;
    if (!id.starts_with(kNamespace) || id[kNamespace.size()] != ':' || !id.ends_with(suffix)) return std::nullopt;
    auto tierId = id.substr(kNamespace.size() + 1, id.size() - kNamespace.size() - 1 - suffix.size());
    return core::tierFromId(tierId);
}

std::optional<core::Tier> backpackTierOf(ItemStack const& stack) {
    if (stack.isNull()) return std::nullopt;
    return backpackTierOf(stack.getTypeName());
}

bool isBackpack(ItemStack const& stack) { return backpackTierOf(stack).has_value(); }
bool isBackpack(core::ItemStackData const& item) { return backpackTierOf(item.id).has_value(); }

bool isShulkerBox(std::string_view id) { return id.ends_with("shulker_box") && id.starts_with("minecraft:"); }

std::optional<std::string> backpackUuid(ItemStack const& stack) {
    if (stack.isNull() || !stack.mUserData) return std::nullopt;
    auto const& tag = *stack.mUserData;
    if (!tag.contains(kUuidKey, Tag::Type::String)) return std::nullopt;
    std::string const& s = tag[kUuidKey];
    if (s.empty()) return std::nullopt;
    return s;
}

std::optional<std::string> backpackUuid(core::ItemStackData const& item) {
    if (!isBackpack(item) || item.nbt().empty()) return std::nullopt;
    return backpackUuid(ItemConv::toStack(item, 1));
}

void setBackpackUuid(ItemStack& stack, std::string const& uuid) {
    if (stack.isNull()) return;
    auto tag = stack.mUserData ? stack.mUserData->clone() : std::make_unique<CompoundTag>();
    tag->putString(std::string(kUuidKey), uuid);
    stack.setUserData(std::move(tag));
}

ItemStack makeBackpackItem(core::Tier tier, std::string const& uuid) {
    auto s = ItemConv::make(backpackItemId(tier), 1, 0);
    if (!uuid.empty()) setBackpackUuid(s, uuid);
    return s;
}

} // namespace abp::game
