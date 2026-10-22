#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/BackpackInventory.h"
#include "core/Tier.h"

namespace abp::core {

/// One upgrade slot of a backpack.
struct UpgradeSlot {
    ItemStackData  item;           ///< the upgrade item (empty = free slot); its id selects the upgrade type
    bool           enabled = true; ///< per-upgrade on/off toggle
    nlohmann::json settings = nlohmann::json::object(); ///< owned by the upgrade implementation

    [[nodiscard]] bool empty() const { return item.empty(); }
};

/// Everything persisted about one backpack, keyed by `uuid`.
///
/// Settings blobs are free-form JSON owned by feature units:
/// - `UpgradeSlot::settings`: per installed upgrade (travels with the upgrade item when removed).
/// - `settings`: backpack-wide settings; each unit uses its own top-level key (e.g. "sort").
struct BackpackData {
    static constexpr int kVersion = 1; ///< bump + add a migration in `fromJson` on format changes

    std::string              uuid;
    Tier                     tier        = Tier::Leather;
    std::uint32_t            mainColor   = 0xFFCC613A; ///< ARGB
    std::uint32_t            accentColor = 0xFF622E1A; ///< ARGB
    BackpackInventory        inventory;
    std::vector<UpgradeSlot> upgrades;
    nlohmann::json           settings = nlohmann::json::object();

    /// A fresh backpack of `tier` with the tier's slot counts.
    [[nodiscard]] static BackpackData create(std::string uuid, Tier tier);

    /// Changes tier, growing (never shrinking) storage and upgrade slots.
    void setTier(Tier t);

    [[nodiscard]] nlohmann::json toJson() const;
    /// Returns nullopt for unreadable data (wrong type, newer version, bad tier).
    [[nodiscard]] static std::optional<BackpackData> fromJson(nlohmann::json const& j);

    [[nodiscard]] std::string                        serialize() const;
    [[nodiscard]] static std::optional<BackpackData> deserialize(std::string_view text);
};

} // namespace abp::core
