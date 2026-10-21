#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/Filter.h"
#include "core/ItemStackData.h"

class ItemStack;
class CompoundTag;

namespace abp::game {

/// Conversion between game `ItemStack`s and core `ItemStackData`.
///
/// The identity blob (`ItemStackData::nbt()`) is the item's full saved tag (name, aux, block
/// states, user data, ...) without `Count`, as little-endian binary NBT. Two stacks that the
/// game would consider the same item produce the same blob.
namespace ItemConv {

/// ItemStack -> core (empty ItemStack -> empty data).
[[nodiscard]] core::ItemStackData toData(ItemStack const& stack);

/// core -> ItemStack with `count` items (clamped to 1..255; Bedrock stores counts in a uchar).
/// `count < 0` uses `min(data.count, 255)`. Returns an empty ItemStack for empty data.
[[nodiscard]] ItemStack toStack(core::ItemStackData const& data, int count = -1);

/// Item id of a stack ("" when empty).
[[nodiscard]] std::string idOf(ItemStack const& stack);

/// A plain stack of `id` (no NBT). Empty if the id is unknown.
[[nodiscard]] ItemStack make(std::string_view id, int count = 1, int aux = 0);

/// Sets the custom name and lore shown by the client (pass an empty name to keep the default).
void setDisplay(ItemStack& stack, std::string const& name, std::vector<std::string> const& lore = {});

/// Tag lookups via the item registry (`minecraft:logs`, ...). Thread: server thread.
class TagProvider final : public core::ITagProvider {
public:
    [[nodiscard]] bool                     hasTag(core::ItemStackData const& item, std::string_view tag) const override;
    [[nodiscard]] std::vector<std::string> tagsOf(core::ItemStackData const& item) const override;
};
[[nodiscard]] TagProvider const& tags();

} // namespace ItemConv
} // namespace abp::game
