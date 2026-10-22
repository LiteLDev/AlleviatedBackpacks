#ifdef ABP_CLIENT

// Native SB tab of the Stonecutter upgrade (SC BlockConverterUpgradeTab + BlockConverterRecipeControl):
// the shift-click target and "Refill Input" toggles at (3,24) / (21,24), and the 81x108 recipe
// control at (3,46): the input slot centred over the 66x56 result list (4x3 recipes of 16x18,
// recently taken results first), the 14x56 scroll bar (14x14 "Open expanded output list with
// search" button on top of it from 13 results), and the output in SC's 26x26 result frame.

#include <algorithm>
#include <cmath>
#include <climits>
#include <numeric>

#include "mc/world/item/ItemStack.h"

#include "client/Client.h"
#include "client/ui/screen/Tabs.h"
#include "client/ui/tabs/crafting/StationWidgets.h"
#include "client/ui/tabs/stonecutter/LargeResultSelector.h"
#include "client/ui/widgets/SbStyle.h"

namespace abp::client::ui::station {

namespace {

// SC BlockConverterRecipeControl.
constexpr float kListYOffset       = 22;
constexpr float kInputSlotHeight   = 18;
constexpr float kSpacing           = 4;
constexpr float kScrollbarXOffset  = 67;
constexpr float kBrowseButtonSize  = 14;
constexpr float kListW             = 66; ///< RESULT_BACKGROUND width
constexpr float kListH             = 56; ///< LIST_BACKGROUND height
constexpr float kSliderH           = 15;
constexpr int   kVisibleRecipes    = 12;
constexpr int   kResultCountThreshold = 13; ///< SC LargeResultSelector.RESULT_COUNT_THRESHOLD

struct StonecutterData {
    ItemStack              input;
    ItemStack              output;
    std::vector<ItemStack> recipes;
    std::vector<int>       recent; ///< per recipe: order in the recent results, -1 = not recent
    int                    selected         = -1;
    bool                   shiftIntoStorage = true;
    bool                   refill           = false;
};

/// The recipe list with its scroll bar and browse button (SC BlockConverterRecipeControl).
class RecipeControl final : public Widget {
public:
    StonecutterData const*         data = nullptr;
    std::function<void(int index)> onSelect;
    std::function<void()>          onBrowse;
    int                            offset   = 0;
    float                          progress = 0;

    [[nodiscard]] bool hasInput() const { return data && !data->input.isNull() && !data->recipes.empty(); }
    [[nodiscard]] int  count() const { return data ? static_cast<int>(data->recipes.size()) : 0; }
    [[nodiscard]] bool showBrowse() const { return hasInput() && count() >= kResultCountThreshold; }
    [[nodiscard]] bool canScroll() const { return hasInput() && count() > kVisibleRecipes; }
    [[nodiscard]] int  hiddenRows() const { return (count() + 4 - 1) / 4 - 3; }

    [[nodiscard]] float listTop() const { return rect.y + kListYOffset; }
    [[nodiscard]] Rect  browseRect() const { return {rect.x + kScrollbarXOffset, listTop(), kBrowseButtonSize, kBrowseButtonSize}; }
    [[nodiscard]] float scrollBgY() const { return showBrowse() ? listTop() + kBrowseButtonSize + 1 : listTop(); }
    [[nodiscard]] float scrollBgH() const { return listTop() + kListH - scrollBgY(); }
    [[nodiscard]] float scrollY() const { return scrollBgY() + 1; }
    [[nodiscard]] float scrollH() const { return scrollBgH() - 2; }
    [[nodiscard]] float sliderTravel() const { return scrollH() - kSliderH; }

    /// Recipe indexes in display order (SC getSortedRecipeIndexes: recent results first).
    [[nodiscard]] std::vector<int> sorted() const {
        std::vector<int> idx(static_cast<std::size_t>(count()));
        std::iota(idx.begin(), idx.end(), 0);
        auto order = [&](int i) {
            int r = data && i < static_cast<int>(data->recent.size()) ? data->recent[static_cast<std::size_t>(i)] : -1;
            return r < 0 ? INT_MAX : r;
        };
        std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) { return order(a) != order(b) ? order(a) < order(b) : a < b; });
        return idx;
    }

    /// Display index under (x, y) (SC mouseClicked: rows start 1 below the list top).
    [[nodiscard]] int displayAt(float x, float y, float rowOffset) const {
        if (!hasInput()) return -1;
        auto list = sorted();
        for (int d = offset; d < offset + kVisibleRecipes && d < static_cast<int>(list.size()); ++d) {
            int   v  = d - offset;
            float rx = rect.x + 1 + static_cast<float>(v % 4 * 16);
            float ry = listTop() + rowOffset + static_cast<float>(v / 4 * 18);
            if (x >= rx && x < rx + 16 && y >= ry && y < ry + 18) return d;
        }
        return -1;
    }

    void render(Painter& p, float mx, float my, double) override {
        // SC renderBg: input slot frame, list, scroll bar, result frame, browse button, slider.
        sb::slotsBackground(p, rect.x + (kListW - 18) / 2, rect.y, 1, 1, 0);
        listBackground(p, {rect.x, listTop(), kListW, kListH});
        scrollBackground(p, {rect.x + kScrollbarXOffset, scrollBgY(), 14, scrollBgH()});
        resultSlotFrame(p, rect.x + (kListW - 26) / 2, rect.y + kInputSlotHeight + kSpacing + kListH + kSpacing);
        if (showBrowse()) {
            auto b = browseRect();
            sb::buttonBackground(p, b, b.contains(mx, my));
            icon(p, Icon::Browse, b.x + 1, b.y + 1);
        }
        slider(p, rect.x + kScrollbarXOffset + 1, scrollY() + static_cast<float>(static_cast<int>(sliderTravel() * progress)), canScroll());
        if (!hasInput()) return;
        auto list = sorted();
        for (int d = offset; d < offset + kVisibleRecipes && d < static_cast<int>(list.size()); ++d) {
            int   v      = d - offset;
            int   recipe = list[static_cast<std::size_t>(d)];
            float rx     = rect.x + 1 + static_cast<float>(v % 4 * 16);
            float ry     = listTop() + static_cast<float>(v / 4 * 18) + 2;
            RecipeBg bg  = RecipeBg::Normal;
            if (recipe == data->selected) bg = RecipeBg::Selected;
            else if (mx >= rx && my >= ry && mx < rx + 16 && my < ry + 18) bg = RecipeBg::Hovered;
            else if (recipe < static_cast<int>(data->recent.size()) && data->recent[static_cast<std::size_t>(recipe)] >= 0) bg = RecipeBg::Recent;
            recipeBackground(p, rx, ry - 1, bg);
        }
        for (int d = offset; d < offset + kVisibleRecipes && d < static_cast<int>(list.size()); ++d) {
            int   v  = d - offset;
            float rx = rect.x + 1 + static_cast<float>(v % 4 * 16);
            float ry = listTop() + static_cast<float>(v / 4 * 18) + 2;
            auto const& s = data->recipes[static_cast<std::size_t>(list[static_cast<std::size_t>(d)])];
            if (!s.isNull()) p.item(s, rx, ry); // SC: the stonecutter list shows no counts
        }
    }

    bool mouseDown(MouseButton button, float x, float y) override {
        mDragging = false;
        if (!hovered(x, y) || !hasInput()) return hovered(x, y) && listArea().contains(x, y);
        if (showBrowse() && browseRect().contains(x, y)) {
            if (button == MouseButton::Left && onBrowse) onBrowse();
            return true;
        }
        if (int d = displayAt(x, y, 1); d >= 0) {
            auto list = sorted();
            if (onSelect) onSelect(list[static_cast<std::size_t>(d)]);
            return true;
        }
        float sx = rect.x + 1 + 67;
        if (x >= sx && x < sx + 12 && y >= scrollY() && y < scrollY() + scrollH()) {
            mDragging = true;
            return true;
        }
        return listArea().contains(x, y);
    }

    bool mouseUp(MouseButton, float, float) override {
        mDragging = false;
        return false;
    }

    bool mouseMove(float, float y) override {
        if (!mDragging || !canScroll()) return false;
        progress = std::clamp((y - scrollY() - kSliderH / 2.f) / sliderTravel(), 0.f, 1.f);
        offset   = static_cast<int>(progress * static_cast<float>(hiddenRows()) + 0.5f) * 4;
        return true;
    }

    bool scroll(float x, float y, float delta) override {
        if (!hovered(x, y)) return false;
        if (canScroll()) {
            int hidden = hiddenRows();
            progress   = std::clamp(progress - delta / static_cast<float>(hidden), 0.f, 1.f);
            offset     = static_cast<int>(progress * static_cast<float>(hidden) + 0.5f) * 4;
        }
        return true;
    }

    void tooltip(float x, float y, std::vector<TooltipLine>& out) const override {
        if (showBrowse() && browseRect().contains(x, y)) {
            out.push_back({ctr("abp.stonecutter.browse_results", "Open expanded output list with search"), 0xFFFFFF});
            return;
        }
        if (!hasInput()) return;
        if (int d = displayAt(x, y, 2); d >= 0) {
            auto list = sorted();
            auto l    = itemTooltipLines(data->recipes[static_cast<std::size_t>(list[static_cast<std::size_t>(d)])]);
            out.insert(out.end(), l.begin(), l.end());
        }
    }

    /// SC onInventoryUpdate: reset the scroll without input, else keep the selection in view.
    void inventoryUpdated() {
        if (!hasInput() || !canScroll()) {
            progress = 0;
            offset   = 0;
            return;
        }
        auto list = sorted();
        auto it   = std::find(list.begin(), list.end(), data->selected);
        if (it == list.end()) {
            offset = std::min(offset, hiddenRows() * 4);
            return;
        }
        int row   = static_cast<int>(it - list.begin()) / 4;
        int first = offset / 4;
        if (row < first) setFirstRow(row);
        else if (row >= first + 3) setFirstRow(row - 2);
    }

private:
    [[nodiscard]] Rect listArea() const { return {rect.x, listTop(), kScrollbarXOffset + 14, kListH}; }

    void setFirstRow(int row) {
        int hidden = hiddenRows();
        int first  = std::clamp(row, 0, hidden);
        offset     = first * 4;
        progress   = hidden > 0 ? static_cast<float>(first) / static_cast<float>(hidden) : 0;
    }

    bool mDragging = false;
};

class StonecutterTab final : public NativeTab {
public:
    Size build(TabContext const& ctx, Rect area, WidgetGroup& widgets) override {
        mControl = nullptr; // widgets of an earlier build are gone (NativeTab contract)
        reportTabOpen(ctx, widgets);
        auto  send = ctx.sendTabAction;
        float x0 = area.x, y0 = area.y;

        auto& shift   = widgets.add(std::make_unique<ToggleButton>());
        shift.size    = Button::Size::Normal;
        shift.rect    = {x0 + 3, y0 + 24, 18, 18};
        shift.states  = {
            {[](Painter& p, float x, float y) { icon(p, Icon::IntoBackpack, x + 1, y + 1); },
             lines(ctr("abp.crafting.shift_click_into_backpack", "Shift Click Result Into\nBackpack"))},
            {[](Painter& p, float x, float y) { icon(p, Icon::IntoInventory, x + 1, y + 1); },
             lines(ctr("abp.crafting.shift_click_into_inventory", "Shift Click Result Into\nPlayer's Inventory"))},
        };
        shift.current = [this] { return mData.shiftIntoStorage ? 0 : 1; };
        shift.onClick = [this, send](int) {
            mData.shiftIntoStorage = !mData.shiftIntoStorage;
            if (send) send({{"toggle", "shiftTarget"}});
        };
        auto& refill   = widgets.add(std::make_unique<ToggleButton>());
        refill.size    = Button::Size::Normal;
        refill.rect    = {x0 + 21, y0 + 24, 18, 18};
        refill.states  = {
            {[](Painter& p, float x, float y) { icon(p, Icon::RefillInput, x + 1, y + 1); },
             lines(ctr("abp.stonecutter.refill_input", "Refill Input"))},
            {[](Painter& p, float x, float y) { icon(p, Icon::NoRefillInput, x + 1, y + 1); },
             lines(ctr("abp.stonecutter.do_not_refill_input", "Do Not Refill Input"))},
        };
        refill.current = [this] { return mData.refill ? 0 : 1; };
        refill.onClick = [this, send](int) {
            mData.refill = !mData.refill;
            if (send) send({{"toggle", "refill"}});
        };

        float cx = x0 + 3, cy = y0 + 46;
        auto& control    = widgets.add(std::make_unique<RecipeControl>());
        control.rect     = {cx, cy, 81, 108};
        control.data     = &mData;
        control.onSelect = [this, send](int recipe) {
            mData.selected = recipe; // SC selects on the client right away
            if (send) send({{"recipe", recipe}});
        };
        control.onBrowse = [this, send] {
            std::vector<ResultEntry> entries;
            for (int i : mControl->sorted()) {
                bool recent = i < static_cast<int>(mData.recent.size()) && mData.recent[static_cast<std::size_t>(i)] >= 0;
                entries.push_back({i, mData.recipes[static_cast<std::size_t>(i)], recent});
            }
            // The overlay may outlive this tab object: the callback only talks to the server (the
            // next model update selects the recipe and scrolls it into view).
            openLargeResultSelector(std::move(entries), mData.selected, [send](int recipe) {
                if (send) send({{"recipe", recipe}});
            });
        };
        mControl = &control;

        auto& input   = widgets.add(std::make_unique<ItemSlot>());
        input.frame   = false; // the control draws the frame
        input.rect    = {cx + (kListW - 16) / 2 - 1, cy, 18, 18};
        input.item    = [this] { return &mData.input; };
        input.onClick = [send](int button, bool shift) {
            if (send) send({{"input", true}, {"button", button}, {"shift", shift}});
        };
        auto& output   = widgets.add(std::make_unique<ItemSlot>());
        output.frame   = false;
        output.rect    = {cx + (kListW - 16) / 2 - 1, cy + kInputSlotHeight + kSpacing + kListH + kSpacing + 4, 18, 18};
        output.item    = [this] { return &mData.output; };
        output.onClick = [send](int button, bool shift) {
            if (send) send({{"output", true}, {"button", button}, {"shift", shift}});
        };
        update(ctx);
        return {3 + 81 + 6, 46 + 108 + 6};
    }

    void update(TabContext const& ctx) override {
        auto const& t      = ctx.upgrade.tab;
        auto        before = mData.input;
        mData.input        = stackFromJson(t.value("input", nlohmann::json()));
        mData.output       = stackFromJson(t.value("output", nlohmann::json()));
        mData.recipes.clear();
        if (auto r = t.find("recipes"); r != t.end() && r->is_array()) {
            for (auto const& e : *r) mData.recipes.push_back(stackFromJson(e));
        }
        mData.recent.clear();
        if (auto r = t.find("recent"); r != t.end() && r->is_array()) {
            for (auto const& e : *r) mData.recent.push_back(e.is_number_integer() ? e.get<int>() : -1);
        }
        mData.selected         = t.value("selected", -1);
        mData.shiftIntoStorage = t.value("shiftIntoStorage", true);
        mData.refill           = t.value("refill", false);
        if (mControl) mControl->inventoryUpdated();
    }

private:
    StonecutterData mData;
    RecipeControl*  mControl = nullptr;
};

} // namespace

} // namespace abp::client::ui::station

ABP_REGISTER_NATIVE_TAB("stonecutter", [] { return std::make_unique<abp::client::ui::station::StonecutterTab>(); });

#endif // ABP_CLIENT
