#pragma once

#include "core/BackpackInventory.h"
#include "core/Filter.h"
#include "upgrade/IUpgrade.h"

namespace abp::upgrade::voiding {

/// Upgrade ids / items.
inline constexpr std::string_view kBasicId    = "void";
inline constexpr std::string_view kAdvancedId = "advanced_void";

/// Settings keys in the upgrade's settings JSON.
inline constexpr std::string_view kFilterKey    = "filter";
inline constexpr std::string_view kOverflowKey  = "overflow";  ///< advanced only: void only what doesn't fit
inline constexpr std::string_view kWorkInGuiKey = "workInGui"; ///< void manual inserts in the GUI too

inline constexpr int kBasicSlots    = 9;
inline constexpr int kAdvancedSlots = 16;

/// Runs after the foundation rules (priority 1000) and after other upgrades' filters, so a Deny
/// from them (e.g. a Filter upgrade) wins over voiding.
inline constexpr int kInsertPriority = 1001;

/// Parsed settings of one installed Void upgrade.
struct VoidSettings {
    core::Filter filter;
    bool         advanced  = false;
    bool         overflow  = false;
    bool         workInGui = false;

    [[nodiscard]] static VoidSettings read(UpgradeContext const& ctx);
};

/// True if `item` may ever be voided: never backpacks or upgrade items.
[[nodiscard]] bool isProtected(core::ItemStackData const& item);

/// True if the settings say `item` should be voided (filter match, not protected).
[[nodiscard]] bool matches(VoidSettings const& s, core::ItemStackData const& item);

/// The insert verdict a Void upgrade with settings `s` gives for `q`.
[[nodiscard]] core::InsertVerdict verdict(VoidSettings const& s, core::InsertQuery const& q);

} // namespace abp::upgrade::voiding
