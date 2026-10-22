#pragma once

// Server side of the shared native filter control (client: `client/ui/widgets/FilterControl.h`).
//
// An upgrade with a filter publishes the filter's state in its native tab data and applies the
// widget's actions to the stored filter:
//
//     // tab data: tab["filter"] = filtering::tabJson(filter, spec)
//     ABP_REGISTER_TAB_DATA(net::TabDataProvider{"magnet", [](Player&, UpgradeContext const& ctx) {
//         return nlohmann::json{{"filter", filtering::tabJson(loadFilter(ctx), specFor(ctx))}};
//     }});
//     // actions: {"filter":"filter","op":...}
//     ABP_REGISTER_TAB_ACTION(net::TabActionHandler{"magnet", [](Player& p, UpgradeContext const& ctx, nlohmann::json const& d) {
//         if (d.value("filter", "") != "filter") return false;
//         auto f = loadFilter(ctx);
//         if (!filtering::applyAction(f, specFor(ctx), d, filtering::cursorOf(p))) return false;
//         storeFilter(ctx, f);
//         return true;
//     }});
//
// Most upgrades keep their filter in `ctx.settings()[key]`; `registerSettingsFilterTab` does the
// two registrations above for that case.

#include <functional>
#include <string>

#include "nlohmann/json.hpp"

#include "core/Filter.h"
#include "upgrade/IUpgrade.h"

class Player;

namespace abp::upgrade::filtering {

/// The first button of the control (SC `FilterLogicControl` ALLOW_LIST toggle,
/// `ContentsFilterControl` type toggle, SB `DepositFilterLogicControl` type toggle).
enum class TypeButton : int {
    AllowList = 0, ///< Allow / Block
    Contents  = 1, ///< Allow / Block / Match Backpack Contents (STORAGE)
    Deposit   = 2, ///< Allow / Block / Match Inventory Contents (INVENTORY)
    None      = 3, ///< no type button (slots start at the top)
};

/// What the control shows (SC `FilterLogicControl.Basic` / `.Advanced` and the slot layout).
struct FilterSpec {
    int        slots       = 9;
    int        slotsPerRow = 3;
    bool       advanced    = false; ///< primary match (ITEM/MOD/TAGS), durability and NBT buttons
    TypeButton type        = TypeButton::Contents;
};

/// The control's state for the client widget:
/// {"slots","perRow","advanced","type","typeState","primary","dur","nbt","anyTag","tags":[..],
///  "items":[ItemStackData json|null ..],"slotsEnabled"}.
[[nodiscard]] nlohmann::json tabJson(core::Filter const& filter, FilterSpec const& spec);

/// Applies one widget action (`data["op"]`: slot, type, primary, dur, nbt, anyTag, addTag,
/// removeTag) to `filter`. `cursor` is the player's cursor item ("slot": empty clears the slot,
/// an item sets a 1-count copy unless it is already in the filter). Returns true when `filter`
/// changed.
bool applyAction(core::Filter& filter, FilterSpec const& spec, nlohmann::json const& data, core::ItemStackData const& cursor);

/// The player's cursor item (server side).
[[nodiscard]] core::ItemStackData cursorOf(Player& player);

/// Registers tab data + tab actions for a filter kept in `ctx.settings()[key]` of every upgrade
/// whose id or group is `upgrade`. `load` reads the filter (with the upgrade's defaults), `store`
/// writes it back (and rebuilds hooks when needed). The client widget uses `key` as its name.
struct SettingsFilterTab {
    std::string                                                     upgrade;
    std::string                                                     key = "filter";
    std::function<FilterSpec(UpgradeContext const&)>                spec;
    std::function<core::Filter(UpgradeContext const&)>              load;
    std::function<void(UpgradeContext const&, core::Filter const&)> store;
};
void registerSettingsFilterTab(SettingsFilterTab tab);

/// SB's `FilteredUpgradeConfig` (filterSlots 1..20, slotsInRow 1..6).
struct FilteredUpgradeConfig {
    int filterSlots = 9;
    int slotsInRow  = 3;
};
/// Clamped to SB's ranges.
[[nodiscard]] FilterSpec specOf(FilteredUpgradeConfig const& config, bool advanced, TypeButton type);

/// Item tags for matching on the server: the item registry's tags, with "match NBT" ignoring the
/// durability (SC `areItemStackComponentsEqualIgnoreDurability`).
[[nodiscard]] core::ITagProvider const& matcher();

} // namespace abp::upgrade::filtering
