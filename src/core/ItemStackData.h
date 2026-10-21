#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include "nlohmann/json.hpp"

namespace abp::core {

/// Identity of an item kind: two stacks with equal keys can merge.
struct ItemKey {
    std::string   id;  ///< full name, e.g. "minecraft:oak_log"
    int           aux = 0;
    std::uint64_t nbtHash = 0; ///< hash of the identity NBT blob (0 = no NBT)
    std::string   nbt;         ///< identity NBT blob (opaque, produced by the game layer)

    [[nodiscard]] bool operator==(ItemKey const& o) const {
        return nbtHash == o.nbtHash && aux == o.aux && id == o.id && nbt == o.nbt;
    }
};

struct ItemKeyHash {
    std::size_t operator()(ItemKey const& k) const noexcept {
        return std::hash<std::string>{}(k.id) ^ (std::size_t(k.nbtHash) * 31u) ^ (std::size_t(k.aux) << 7);
    }
};

/// A game-independent item stack.
///
/// - `count` is an int: backpack slots can hold far more than Bedrock's uchar stack count.
/// - `nbt` is an opaque blob the game layer produces (binary NBT of the item's identity data,
///   without the count). Core only compares it. Use `setNbt` so the hash stays in sync.
/// - `maxStack`, `damage`, `maxDamage` are derived metadata (filled by `game::ItemConv`); they
///   are not part of the identity.
class ItemStackData {
public:
    std::string id;
    int         aux       = 0;
    int         count     = 0;
    int         maxStack  = 64;
    int         damage    = 0;
    int         maxDamage = 0;

    ItemStackData() = default;

    /// Convenience factory (mainly for tests and simple game code).
    [[nodiscard]] static ItemStackData of(std::string id, int count, int maxStack = 64, int aux = 0);

    [[nodiscard]] std::string const& nbt() const { return mNbt; }
    [[nodiscard]] std::uint64_t      nbtHash() const { return mNbtHash; }
    void                             setNbt(std::string blob);

    [[nodiscard]] bool empty() const { return count <= 0 || id.empty(); }

    /// Same item kind (id, aux and NBT); ignores count and metadata.
    [[nodiscard]] bool sameItem(ItemStackData const& o) const {
        return mNbtHash == o.mNbtHash && aux == o.aux && id == o.id && mNbt == o.mNbt;
    }

    [[nodiscard]] ItemKey key() const { return ItemKey{id, aux, mNbtHash, mNbt}; }

    /// Copy with a different count (0 or less yields an empty stack).
    [[nodiscard]] ItemStackData withCount(int n) const;

    /// "minecraft" for "minecraft:stone"; "minecraft" when the id has no namespace.
    [[nodiscard]] std::string_view itemNamespace() const;

    void clear() { *this = ItemStackData{}; }

    [[nodiscard]] bool operator==(ItemStackData const& o) const { return count == o.count && sameItem(o); }

    /// JSON form: {"id","aux","count","max","dmg","maxDmg","nbt"(base64)}; empty stacks are `null`.
    [[nodiscard]] nlohmann::json toJson() const;
    [[nodiscard]] static ItemStackData fromJson(nlohmann::json const& j);

private:
    std::string   mNbt;
    std::uint64_t mNbtHash = 0;
};

} // namespace abp::core
