#pragma once

// The Auto cooking upgrades' filter controls (SC FilterLogicControl): the input filter is the
// Advanced variant (Allow / primary match / durability / NBT buttons in a row above the slots, the
// tag list with add / remove buttons in "Match Tags" mode), the fuel filter shows its slots only.
// Server data: `tab.inputFilter` / `tab.fuelFilter` (core::Filter JSON + slots, perRow, tagItem,
// tagsToAdd); actions go to the cooking tab handler as {"filter":"input|fuel", ...}.

#include <functional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "mc/world/item/ItemStack.h"

#include "client/ui/widgets/Widgets.h"

namespace abp::client::ui::station {

class FilterControl {
public:
    /// `which`: "input" or "fuel"; `advanced`: SC FilterLogicControl.Advanced (buttons).
    FilterControl(std::string which, bool advanced) : mWhich(std::move(which)), mAdvanced(advanced) {}

    /// Reads the server's filter JSON (data only).
    void setData(nlohmann::json const& j);
    /// Shows / hides the widgets for the current mode. Only while the widgets of the last
    /// `build()` live (the tab is open).
    void refresh();
    /// Adds the control's widgets at (x, y); returns its height (SC getHeightForPrimaryMatch).
    float build(WidgetGroup& widgets, float x, float y, std::function<void(nlohmann::json)> send);
    /// SC height for the current data (buttons row + slot rows).
    [[nodiscard]] float height() const;
    [[nodiscard]] float width() const;
    [[nodiscard]] bool  tagsMode() const { return mAdvanced && mMatchBy == 2; }

    // data (read by the widgets)
    int                      mode    = 0; ///< 0 allow, 1 deny
    int                      mMatchBy = 0; ///< 0 item, 1 mod, 2 tags
    bool                     nbt = false, durability = false;
    int                      slots = 4, perRow = 4;
    std::vector<ItemStack>   ghosts;
    std::vector<std::string> tags;
    std::vector<std::string> tagsToAdd;
    ItemStack                tagItem;
    int                      selectedAdd = 0, selectedRemove = 0;

private:
    std::string mWhich;
    bool        mAdvanced;
    // Widgets of the last build (owned by the tab's widget group).
    Widget* mDur     = nullptr;
    Widget* mNbt     = nullptr;
    Widget* mGrid    = nullptr;
    Widget* mList    = nullptr;
    Widget* mTagSlot = nullptr;
    Widget* mAdd     = nullptr;
    Widget* mRemove  = nullptr;
};

} // namespace abp::client::ui::station
