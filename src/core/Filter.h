#pragma once

#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/ItemStackData.h"

namespace abp::core {

/// Answers tag questions about an item (the game layer implements it with the item registry;
/// tests use a fake). Tags are full names like "minecraft:logs".
class ITagProvider {
public:
    virtual ~ITagProvider() = default;
    [[nodiscard]] virtual bool                     hasTag(ItemStackData const& item, std::string_view tag) const = 0;
    [[nodiscard]] virtual std::vector<std::string> tagsOf(ItemStackData const& item) const                       = 0;
    /// "Match NBT": the data of two items is equal ignoring their durability (SC
    /// `ItemStackHelper.areItemStackComponentsEqualIgnoreDurability`). The default compares the
    /// identity blobs; the game layer strips the damage value first.
    [[nodiscard]] virtual bool componentsEqualIgnoringDamage(ItemStackData const& a, ItemStackData const& b) const {
        return a.nbtHash() == b.nbtHash() && a.nbt() == b.nbt();
    }
};

enum class FilterMode : int {
    Allow = 0, ///< only matching items pass (SC "Allow")
    Deny  = 1, ///< matching items are rejected (SC "Block")
};

/// SC `PrimaryMatch`.
enum class MatchBy : int {
    Item      = 0, ///< same item id (and aux)                           (SC ITEM)
    Namespace = 1, ///< same id namespace as a filter item ("mod" match)  (SC MOD)
    Tag       = 2, ///< the item's tags against the selected tag list     (SC TAGS)
};
using PrimaryMatch = MatchBy;

/// SC `ContentsFilterType`: the type button of contents-filtered upgrades (Pickup, Filter,
/// Restock, Magnet, ...). STORAGE = items already in the storage plus memorized slots.
enum class ContentsFilterType : int { Allow = 0, Block = 1, Storage = 2 };
/// SB `DepositFilterType`: the Deposit upgrade's type button. INVENTORY = only items already in
/// the target inventory.
enum class DepositFilterType : int { Allow = 0, Block = 1, Inventory = 2 };

/// Defaults of a filter that has never been edited (SC `FilterLogic.setAllowByDefault` /
/// `setEmptyAllowListMatchesEverything`). SC: a new filter is a block (deny) list.
struct FilterDefaults {
    bool allowList                       = false;
    bool emptyAllowListMatchesEverything = false;
};

/// SC keeps selected tags in a TreeSet of ResourceLocations: ordered by path, then namespace (a
/// tag without ':' is in the `minecraft` namespace).
[[nodiscard]] bool tagLess(std::string_view a, std::string_view b);
/// A tag as a ResourceLocation string: a tag without ':' is in the `minecraft` namespace, so
/// "logs" and "minecraft:logs" are the same tag ("logs" -> "minecraft:logs").
[[nodiscard]] std::string normalizeTag(std::string_view tag);
/// Normalizes (`normalizeTag`), sorts and de-duplicates tags in that order.
void sortTags(std::vector<std::string>& tags);

/// A reusable item filter: a fixed-size list of filter slots plus SC's `FilterAttributes`.
///
/// Matching follows Sophisticated Core's `FilterLogic` exactly:
/// - `stackMatchesFilter(item, filterItem)`: ITEM = same id (and aux), MOD = same namespace;
///   then "match durability" (equal damage) and "match NBT" (equal data ignoring damage) apply
///   in both modes.
/// - Allow list: TAGS = tag match; otherwise any filter item matches, or the list has only empty
///   slots and `emptyAllowsAll` (SC `emptyAllowListMatchesEverything`).
/// - Block list: TAGS = no tag match; otherwise only empty slots, or no filter item matches.
/// - Tag match: "match any tag" = the item has one of `tags`; "match all tags" = it has all.
/// - STORAGE / INVENTORY types (`filterByStorage` / `filterByInventory`) match against reference
///   items supplied by the caller (`matchesContents`).
struct Filter {
    FilterMode                 mode            = FilterMode::Allow;
    MatchBy                    matchBy         = MatchBy::Item;
    bool                       matchNbt        = false;
    bool                       matchDurability = false;
    bool                       emptyAllowsAll  = true; ///< SC emptyAllowListMatchesEverything
    bool                       matchAnyTag     = true; ///< SC match_any_tag (new filters: true)
    bool                       filterByStorage   = false; ///< SC filter_by_storage (ContentsFilterType::Storage)
    bool                       filterByInventory = false; ///< SB filter_by_inventory (DepositFilterType::Inventory)
    std::vector<ItemStackData> ghosts;         ///< fixed size; empty entries are unused slots (count is 1 when set)
    std::vector<std::string>   tags;           ///< selected tags, sorted and unique (SC TreeSet)
    /// Phase-1 data (no "v"): a tag filter without selected tags matched the tags of its filter
    /// items. Kept for such stored filters until a tag is selected; new filters never set it.
    bool                       legacyGhostTags = false;

    Filter() = default;
    explicit Filter(int slots) : ghosts(static_cast<std::size_t>(slots)) {}
    /// A never-edited filter with SC defaults (`defaults.allowList`, match any tag).
    Filter(int slots, FilterDefaults defaults);

    /// No filter item set (tags are not considered; SC `hasOnlyEmptyFilters`).
    [[nodiscard]] bool hasOnlyEmptyFilters() const;
    /// No filter item and, in TAGS mode, no tag.
    [[nodiscard]] bool isEmpty() const;
    /// SC `stackMatchesFilter`: does `item` match the single filter item `filter`?
    [[nodiscard]] bool stackMatchesFilter(ItemStackData const& item, ItemStackData const& filter, ITagProvider const* tags = nullptr) const;
    /// TAGS mode: any / all selected tags present on `item`.
    [[nodiscard]] bool isTagMatch(ItemStackData const& item, ITagProvider const* tags) const;
    /// Positive match: TAGS = tag match, otherwise any filter item matches.
    [[nodiscard]] bool matches(ItemStackData const& item, ITagProvider const* tags = nullptr) const;
    /// SC `FilterLogic.matchesFilter` (allow / block list semantics; ignores the storage type).
    [[nodiscard]] bool passes(ItemStackData const& item, ITagProvider const* tags = nullptr) const;
    [[nodiscard]] bool matchesFilter(ItemStackData const& item, ITagProvider const* tags = nullptr) const {
        return passes(item, tags);
    }
    /// SC `ContentsFilterLogic.matchesFilter` / SB `DepositFilterLogic.matchesFilter`: with
    /// `filterByStorage` or `filterByInventory` set, true if `item` matches one of `reference`
    /// (`stackMatchesFilter`) or `extra(item)` (memorized slots); otherwise `passes`. Empty items
    /// never match.
    [[nodiscard]] bool matchesContents(
        ItemStackData const&                         item,
        std::span<ItemStackData const>               reference,
        ITagProvider const*                          tags  = nullptr,
        std::function<bool(ItemStackData const&)> const& extra = {}
    ) const;

    // ---- type buttons -------------------------------------------------------------------------
    [[nodiscard]] ContentsFilterType contentsType() const;
    /// SC `setDepositFilterType` (ALLOW / BLOCK also clear the storage flag).
    void setContentsType(ContentsFilterType type);
    /// SC ContentsFilterControl: next type, skipping STORAGE in TAGS mode.
    [[nodiscard]] ContentsFilterType nextContentsType() const;
    [[nodiscard]] DepositFilterType depositType() const;
    void                            setDepositType(DepositFilterType type);
    /// SC FilterLogicControlBase primary match button: next match; switching to TAGS turns a
    /// STORAGE contents type into the next type (`onTagsMatchSelected`) when `contents`.
    void cyclePrimaryMatch(bool contents);

    // ---- filter slots -------------------------------------------------------------------------
    /// Sets ghost slot `i` to a 1-count copy of `item` (or clears it when `item` is empty).
    void setGhost(int i, ItemStackData const& item);
    /// True if a ghost of the same item (same id, aux and data) already exists.
    [[nodiscard]] bool hasGhost(ItemStackData const& item) const;
    /// SC `ObservableFilterItemStackHandler.isItemValid`: empty, or not already in the filter.
    [[nodiscard]] bool canSetGhost(ItemStackData const& item) const { return item.empty() || !hasGhost(item); }

    // ---- tags ---------------------------------------------------------------------------------
    /// Adds a tag (normalized with `normalizeTag`, kept sorted and unique). Returns false if it was
    /// already selected.
    bool addTag(std::string const& tag);
    /// Removes a tag. Returns false if it was not selected.
    bool removeTag(std::string const& tag);
    [[nodiscard]] bool hasTag(std::string_view tag) const;
    /// SC TagSelectionSlot: the tags of an item that are not selected yet, sorted.
    [[nodiscard]] std::vector<std::string> tagsToAdd(std::vector<std::string> itemTags) const;

    [[nodiscard]] nlohmann::json toJson() const;
    /// Reads JSON written by `toJson`; keeps `slots` ghost entries (pads/truncates), -1 = as stored.
    /// Missing fields take the phase-1 defaults (allow list, empty allows all).
    [[nodiscard]] static Filter fromJson(nlohmann::json const& j, int slots = -1);
    /// Like `fromJson`, but a missing / non-object `j` is a new SC filter with `defaults`, and
    /// `emptyAllowsAll` always comes from `defaults` (it is a property of the upgrade, not data).
    [[nodiscard]] static Filter fromJson(nlohmann::json const& j, int slots, FilterDefaults defaults);
};

} // namespace abp::core
