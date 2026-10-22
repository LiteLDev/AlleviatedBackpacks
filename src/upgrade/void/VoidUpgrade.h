#pragma once

#include "api/abp/Fluid.h"
#include "core/BackpackInventory.h"
#include "core/Filter.h"
#include "upgrade/IUpgrade.h"
#include "upgrade/void/VoidRules.h"

namespace abp::upgrade::voiding {

/// Upgrade ids / items.
inline constexpr std::string_view kBasicId    = "void";
inline constexpr std::string_view kAdvancedId = "advanced_void";

/// Settings keys in the upgrade's settings JSON.
inline constexpr std::string_view kFilterKey    = "filter";
inline constexpr std::string_view kVoidTypeKey  = "voidType";  ///< "always" | "slot_overflow" | "storage_overflow"
inline constexpr std::string_view kWorkInGuiKey = "workInGui"; ///< SC SHOULD_WORK_IN_GUI (default false)

/// SB `VoidUpgradeConfig` of one tier.
struct VoidTierConfig {
    int  filterSlots       = 9;    ///< Number of filter slots (1..20)
    int  slotsInRow        = 3;    ///< Number of filter slots displayed in a row (1..6)
    bool voidAlwaysEnabled = true; ///< Determines whether void upgrade allows voiding always or it only has overflow options
};

/// `config/void.json` (SB server config sections `voidUpgrade` / `advancedVoidUpgrade`).
struct VoidConfig {
    int            version             = 1;
    VoidTierConfig voidUpgrade         = {9, 3, true};
    VoidTierConfig advancedVoidUpgrade = {16, 4, true};
};

[[nodiscard]] VoidTierConfig tierConfig(bool advanced);

/// Runs after the foundation rules (priority 1000) and after other upgrades' filters, so a Deny
/// from them (e.g. a Filter upgrade) wins over voiding.
inline constexpr int kInsertPriority = 1001;

/// Parsed settings of one installed Void upgrade.
struct VoidSettings {
    core::Filter filter;
    bool         advanced          = false;
    VoidType     storedType        = VoidType::Always; ///< as stored
    bool         voidAlwaysEnabled = true;
    bool         workInGui         = false;

    /// SC `getVoidType`.
    [[nodiscard]] VoidType type() const { return effective(storedType, voidAlwaysEnabled); }
    /// SC `shouldVoidOverflow`.
    [[nodiscard]] bool shouldVoidOverflow() const { return !voidAlwaysEnabled || storedType != VoidType::Always; }
    /// SC `filterLogic.matchesFilter` (allow list by default; an empty allow list matches nothing,
    /// an empty block list everything).
    [[nodiscard]] bool matches(core::ItemStackData const& item) const;
    /// SC `shouldVoidFluid` for this upgrade's own type: buckets and other filled containers in
    /// the filter stand for their fluid.
    [[nodiscard]] bool voidsFluid(api::FluidStack const& fluid) const;
    [[nodiscard]] VoidRules rules() const;

    [[nodiscard]] static VoidSettings read(UpgradeContext const& ctx);
};

/// The fluid one filter item stands for (SC `FluidFilterContainer.getContainedFluid`: what its
/// item fluid handler would drain, up to a bucket). Empty for anything else.
[[nodiscard]] api::FluidStack containedFluid(core::ItemStackData const& item);

} // namespace abp::upgrade::voiding
