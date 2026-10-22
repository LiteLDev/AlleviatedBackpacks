#pragma once

// Shared native filter control: Sophisticated Core's `FilterLogicControl` (Basic / Advanced),
// `ContentsFilterControl` and SB's `DepositFilterLogicControl`, drawn and laid out like SC.
//
// Layout (relative to the control's position, i.e. SC's `Position` passed to the control):
//   buttons at y+0: type at x+0, primary match at x+18, durability at x+36, NBT at x+54
//   (durability / NBT hidden in TAGS mode; basic variants only show the type button);
//   filter slots 21 below the buttons (0 without buttons), `perRow` per row;
//   TAGS mode: tag list (control background, `perRow`*18 wide, (rows-1)*18 high) where the slots
//   were, below it the tag selection slot, Add Tag (x+18), Remove Tag (x+36), Match Any/All (x+54).
//
// The server owns the filter (see `upgrade/filter/FilterTab.h`): it publishes the state in the
// upgrade's tab data (`tab[key]`, `filtering::tabJson`) and applies the actions the control sends
// through the tab (`{"filter":key,"op":...}`, `filtering::applyAction`). The tag selection slot
// and the tag selection indices are client-side, like in SC.
//
// Use it in a native tab (client/ui/screen/Tabs.h):
//
//     Size build(TabContext const& ctx, Rect area, WidgetGroup& widgets) override {
//         mFilter = &widgets.add(std::make_unique<FilterControl>("filter", ctx.sendTabAction));
//         mFilter->setPosition(area.x + 3, area.y + 24);
//         mFilter->setState(ctx.upgrade.tab["filter"]);
//         return {3 + mFilter->width() + 6, 24 + mFilter->height() + 6};
//     }
//     void update(TabContext const& ctx) override { mFilter->setState(ctx.upgrade.tab["filter"]); }
//     bool needsRebuild() override { return mFilter && mFilter->consumeSizeChange(); }
//
// Or use `FilterTab` below, which is exactly that (plus optional extra widgets above the control).

#ifdef ABP_CLIENT

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "client/ui/screen/Tabs.h"
#include "client/ui/widgets/Widgets.h"

#include "mc/world/item/ItemStack.h"

namespace abp::client::ui {

class FilterControl : public Widget {
public:
    using Send = std::function<void(nlohmann::json data)>;

    /// `key`: the name of the filter in the tab data (and in the actions); `send`: the tab's
    /// `TabContext::sendTabAction`.
    FilterControl(std::string key, Send send);

    /// The control's top-left corner (SC: tab x + 3, tab y + 24 for most upgrade tabs).
    void setPosition(float x, float y);
    /// New server state (`tab[key]`); ignored when not an object.
    void setState(nlohmann::json const& state);

    /// SC dimensions for the current primary match.
    [[nodiscard]] float width() const;
    [[nodiscard]] float height() const;
    /// True once after the height changed (primary match switched to / from TAGS): the tab must
    /// re-measure (`NativeTab::needsRebuild`).
    bool consumeSizeChange();

    // ---- state (for tabs and tests) ---------------------------------------------------------------
    enum class Type { AllowList, Contents, Deposit, None };
    [[nodiscard]] Type type() const { return mType; }
    [[nodiscard]] int  typeState() const { return mTypeState; }
    [[nodiscard]] int  primaryMatch() const { return mPrimary; }
    [[nodiscard]] bool tagsMode() const { return mAdvanced && mPrimary == 2; }
    [[nodiscard]] std::vector<std::string> const& tags() const { return mTags; }

    void render(Painter& p, float mx, float my, double time) override;
    bool mouseDown(MouseButton button, float x, float y) override;
    bool scroll(float x, float y, float delta) override;
    void tooltip(float x, float y, std::vector<TooltipLine>& out) const override;

private:
    enum class Hit { None, Type, Primary, Durability, Nbt, Slot, TagSlot, AddTag, RemoveTag, AnyTag, TagList };
    struct HitResult {
        Hit hit   = Hit::None;
        int index = -1;
    };
    [[nodiscard]] HitResult hitTest(float x, float y) const;
    [[nodiscard]] Rect      buttonRect(int column, float dy = 0) const;
    [[nodiscard]] float     slotsTop() const { return mButtonsVisible ? 21.f : 0.f; }
    [[nodiscard]] int       totalRows() const;
    [[nodiscard]] float     tagListHeight() const { return static_cast<float>(std::max(0, totalRows() - 1) * 18); }
    [[nodiscard]] float     tagButtonsY() const { return slotsTop() + tagListHeight(); }
    void                    send(nlohmann::json data) const;
    void                    setTagSelection(ItemStack const& stack);
    void                    refreshTagsToAdd();
    void                    drawButton(Painter& p, Rect const& r, bool hovered, std::function<void(Painter&, float, float)> const& icon) const;
    [[nodiscard]] std::vector<TooltipLine> addTagTooltip() const;
    [[nodiscard]] std::vector<TooltipLine> removeTagTooltip() const;
    [[nodiscard]] std::vector<TooltipLine> tagListTooltip() const;

    std::string mKey;
    Send        mSend;
    float       mX = 0, mY = 0;
    bool        mHasState = false;

    // server state
    int                      mSlots    = 9;
    int                      mPerRow   = 3;
    bool                     mAdvanced = false;
    Type                     mType     = Type::Contents;
    int                      mTypeState = 1;
    int                      mPrimary   = 0;
    bool                     mDurability = false;
    bool                     mNbt        = false;
    bool                     mAnyTag     = true;
    bool                     mSlotsEnabled = true;
    bool                     mButtonsVisible = true;
    std::vector<std::string> mTags;
    std::vector<ItemStack>   mItems;

    // client-side tag selection (SC TagSelectionSlot / FilterLogicContainerBase)
    ItemStack                mTagItem;
    std::vector<std::string> mTagsToAdd;
    int                      mSelectedToAdd    = 0;
    int                      mSelectedToRemove = 0;

    float mLastHeight   = -1;
    int   mLastPrimary  = -1;
    bool  mSizeChanged  = false;
};

/// A complete upgrade settings tab around one `FilterControl` (SC `PickupUpgradeTab`,
/// `RestockUpgradeTab`, `DepositUpgradeTab`, `FilterUpgradeTab`): the control at (x+3, y+controlY),
/// plus optional extra widgets added by `extra` (e.g. the Filter upgrade's direction button).
class FilterTab : public NativeTab {
public:
    using Extra = std::function<void(TabContext const& ctx, Rect area, WidgetGroup& widgets)>;
    explicit FilterTab(std::string key = "filter", float controlY = 24, Extra extra = {});

    Size build(TabContext const& ctx, Rect area, WidgetGroup& widgets) override;
    void update(TabContext const& ctx) override;
    bool needsRebuild() override;

protected:
    std::string    mKey;
    float          mControlY;
    Extra          mExtra;
    FilterControl* mControl = nullptr; ///< owned by the tab's widget group
};

/// SC-style 16x16 icons of the filter buttons (original pixel art), drawn at (x+1, y+1) of an
/// 18x18 button. Exposed for other tabs that reuse the same buttons.
namespace filter_icons {
enum class Icon {
    Allow,
    Block,
    MatchStorage,   ///< "Match Backpack Contents"
    MatchInventory, ///< "Match Inventory Contents"
    MatchItem,
    MatchMod,
    MatchTags,
    MatchDurability,
    IgnoreDurability,
    MatchNbt,
    IgnoreNbt,
    MatchAnyTag,
    MatchAllTags,
    AddTag,
    RemoveTag,
    DirectionBoth,
    DirectionInput,
    DirectionOutput,
};
void draw(Painter& p, Icon icon, float x, float y);
} // namespace filter_icons

} // namespace abp::client::ui

#endif // ABP_CLIENT
