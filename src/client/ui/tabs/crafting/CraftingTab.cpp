#ifdef ABP_CLIENT

// Native SB tab of the Crafting upgrade (SC CraftingUpgradeTab): 63x142, the shift-click target and
// refill toggles at (3,24) / (21,24), the 3x3 grid at (3,44), the arrow at (22,101), the result
// slot in SC's 26x26 frame at (17,111), and for grids matching several recipes the result
// cycler: Previous (9,118) and Next (43,118) 8x12 buttons, "Select Result" 16x12 at (22,99)
// showing the number of results, which opens a picker (3 per row) above it.

#include <algorithm>
#include <cmath>

#include "mc/world/item/ItemStack.h"

#include "client/Client.h"
#include "client/ui/screen/Tabs.h"
#include "client/ui/tabs/crafting/StationWidgets.h"
#include "client/ui/widgets/SbStyle.h"

namespace abp::client::ui::station {

namespace {

constexpr int kBorder = 3; ///< SC RESULT_SELECTION_BORDER_WIDTH

struct CraftingData {
    std::vector<ItemStack> grid = std::vector<ItemStack>(9);
    ItemStack              result;
    std::vector<ItemStack> results;
    int                    selected         = 0;
    bool                   shiftIntoStorage = true;
    bool                   refill           = false;
};

/// The result picker above "Select Result" (SC renderResultSelectionBackground / Choices).
class ResultPicker final : public Widget {
public:
    CraftingData const*                 data = nullptr;
    Rect                                anchor; ///< the Select Result button
    bool                                open = false;
    std::function<void(int index)>      onSelect;

    [[nodiscard]] int count() const { return data ? static_cast<int>(data->results.size()) : 0; }

    [[nodiscard]] Rect box() const {
        int   n      = count();
        int   cols   = std::min(n, 3);
        float height = static_cast<float>(kBorder + ((n - 1) / 3 + 1) * 18 + kBorder);
        float width  = static_cast<float>(kBorder + cols * 18 + kBorder);
        float left   = anchor.x + 8 - kBorder - static_cast<float>(static_cast<int>(static_cast<float>(cols) / 2.f * 18));
        return {left, anchor.y - height, width, height};
    }

    [[nodiscard]] int choiceAt(float x, float y) const {
        if (!open || count() <= 1) return -1;
        auto b = box();
        float sx = b.x + kBorder, sy = b.y + kBorder;
        if (x < sx || x >= b.right() - kBorder || y < sy || y >= b.bottom() - kBorder) return -1;
        for (int i = 0; i < count(); ++i) {
            float cx = sx + static_cast<float>(i % 3) * 18, cy = sy + static_cast<float>(i / 3) * 18;
            if (x >= cx && x < cx + 18 && y >= cy && y < cy + 18) return i;
        }
        return -1;
    }

    void render(Painter& p, float mx, float my, double) override {
        if (!open || count() <= 1) return;
        p.flush(); // SC draws the picker at z+410, above the tab
        auto b = box();
        sb::panel(p, b);
        int n = count();
        sb::slotsBackground(p, b.x + kBorder, b.y + kBorder, 3, n / 3, n % 3);
        for (int i = 0; i < n; ++i) {
            float x = b.x + kBorder + 1 + static_cast<float>(i % 3) * 18;
            float y = b.y + kBorder + 1 + static_cast<float>(i / 3) * 18;
            auto const& s = data->results[static_cast<std::size_t>(i)];
            if (!s.isNull()) {
                p.item(s, x, y);
                drawCount(p, s.mCount, x, y);
            }
        }
        if (int h = choiceAt(mx, my); h >= 0) {
            float x = b.x + kBorder + 1 + static_cast<float>(h % 3) * 18;
            float y = b.y + kBorder + 1 + static_cast<float>(h / 3) * 18;
            p.fillRect({x, y, 16, 16}, Color::argb(0x80FFFFFF), Painter::Layer::Foreground);
        }
        p.flush();
    }

    bool mouseDown(MouseButton, float x, float y) override {
        int i = choiceAt(x, y);
        if (i < 0) return open && count() > 1 && box().contains(x, y); // covers the slots below
        open = false;
        if (onSelect) onSelect(i);
        return true;
    }

    void tooltip(float x, float y, std::vector<TooltipLine>& out) const override {
        int i = choiceAt(x, y);
        if (i < 0) return;
        out.clear(); // SC renders the choice's tooltip on top of everything else
        auto l = itemTooltipLines(data->results[static_cast<std::size_t>(i)]);
        out.insert(out.end(), l.begin(), l.end());
    }
};

/// Static backgrounds of the tab (SC renderBg): slots, arrow, result frame. Added first so the
/// widgets draw over them.
class Backdrop final : public Widget {
public:
    void render(Painter& p, float, float, double) override {
        sb::slotsBackground(p, rect.x + 3, rect.y + 44, 3, 3, 0);
        icon(p, Icon::CraftArrow, rect.x + 3 + 19, rect.y + 101);
        resultSlotFrame(p, rect.x + 3 + 14, rect.y + 111);
    }
};

class CraftingTab final : public NativeTab {
public:
    Size build(TabContext const& ctx, Rect area, WidgetGroup& widgets) override {
        // Widgets of an earlier build are gone (NativeTab contract).
        mPrev = mNext = mSelect = nullptr;
        mPicker = nullptr;
        auto& bg = widgets.add(std::make_unique<Backdrop>());
        bg.rect  = {area.x, area.y, 63, 142};
        reportTabOpen(ctx, widgets);
        update(ctx);
        auto send = ctx.sendTabAction;
        float x0 = area.x, y0 = area.y;

        // Shift-click target and refill toggles (SBPButtonDefinitions).
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
            {[](Painter& p, float x, float y) { icon(p, Icon::RefillGrid, x + 1, y + 1); },
             lines(ctr("abp.crafting.refill_crafting_grid", "Refill Crafting Grid"))},
            {[](Painter& p, float x, float y) { icon(p, Icon::NoRefillGrid, x + 1, y + 1); },
             lines(ctr("abp.crafting.do_not_refill_crafting_grid", "Do Not Refill Crafting Grid"))},
        };
        refill.current = [this] { return mData.refill ? 0 : 1; };
        refill.onClick = [this, send](int) {
            mData.refill = !mData.refill;
            if (send) send({{"toggle", "refill"}});
        };

        // The 3x3 grid.
        for (int i = 0; i < 9; ++i) {
            auto& s   = widgets.add(std::make_unique<ItemSlot>());
            s.rect    = {x0 + 3 + static_cast<float>(i % 3) * 18, y0 + 44 + static_cast<float>(i / 3) * 18, 18, 18};
            s.item    = [this, i] { return &mData.grid[static_cast<std::size_t>(i)]; };
            s.onClick = [send, i](int button, bool shift) {
                if (send) send({{"grid", i}, {"button", button}, {"shift", shift}});
            };
        }
        // The result (no frame: the 26x26 one is drawn in render()).
        auto& result   = widgets.add(std::make_unique<ItemSlot>());
        result.frame   = false;
        result.rect    = {x0 + 3 + 19 - 1, y0 + 44 + 72 - 1, 18, 18};
        result.item    = [this] { return &mData.result; };
        result.onClick = [send](int button, bool shift) {
            if (send) send({{"result", true}, {"button", button}, {"shift", shift}});
        };

        // Result cycler.
        auto& prev        = widgets.add(std::make_unique<StationButton>());
        prev.rect         = {x0 + 3 + 6, y0 + 118, 8, 12};
        prev.icon         = [](Painter& p, float x, float y) { icon(p, Icon::Previous, x, y); };
        prev.tooltipLines = {{ctr("abp.crafting.previous_result", "Previous"), 0xFFFFFF}};
        prev.onClick      = [send](int button) {
            if (button == 0 && send) send({{"previous", true}});
        };
        mPrev              = &prev;
        auto& next         = widgets.add(std::make_unique<StationButton>());
        next.rect          = {x0 + 3 + 6 + 8 + 26, y0 + 118, 8, 12};
        next.icon          = [](Painter& p, float x, float y) { icon(p, Icon::Next, x, y); };
        next.tooltipLines  = {{ctr("abp.crafting.next_result", "Next"), 0xFFFFFF}};
        next.onClick       = [send](int button) {
            if (button == 0 && send) send({{"next", true}});
        };
        mNext              = &next;
        auto& select       = widgets.add(std::make_unique<StationButton>());
        select.rect        = {x0 + 3 + 6 + 13, y0 + 99, 16, 12};
        select.tooltipLines = {{ctr("abp.crafting.select_result", "Select Result"), 0xFFFFFF}};
        select.label       = [this] { return std::to_string(mData.results.size()); };
        mSelect            = &select;

        auto& picker   = widgets.add(std::make_unique<ResultPicker>());
        picker.data    = &mData;
        picker.anchor  = select.rect;
        picker.rect    = {x0, y0, 63, 142};
        picker.onSelect = [send](int i) {
            if (send) send({{"select", i}});
        };
        mPicker        = &picker;
        select.onClick = [this](int button) {
            if (button == 0 && mPicker) mPicker->open = !mPicker->open;
        };
        refresh();
        return {63, 142};
    }

    void update(TabContext const& ctx) override {
        auto const& t = ctx.upgrade.tab;
        if (auto g = t.find("grid"); g != t.end() && g->is_array()) {
            for (std::size_t i = 0; i < 9 && i < g->size(); ++i) mData.grid[i] = stackFromJson((*g)[i]);
        }
        mData.result = stackFromJson(t.value("result", nlohmann::json()));
        mData.results.clear();
        if (auto r = t.find("results"); r != t.end() && r->is_array()) {
            for (auto const& e : *r) mData.results.push_back(stackFromJson(e));
        }
        mData.selected         = t.value("selected", 0);
        mData.shiftIntoStorage = t.value("shiftIntoStorage", true);
        mData.refill           = t.value("refill", false);
        refresh();
    }

private:
    void refresh() {
        bool cycler = mData.results.size() > 1;
        if (mPrev) mPrev->visible = cycler;
        if (mNext) mNext->visible = cycler;
        if (mSelect) mSelect->visible = cycler;
        if (mPicker && !cycler) mPicker->open = false;
    }

    CraftingData   mData;
    StationButton* mPrev   = nullptr;
    StationButton* mNext   = nullptr;
    StationButton* mSelect = nullptr;
    ResultPicker*  mPicker = nullptr;
};

} // namespace

} // namespace abp::client::ui::station

ABP_REGISTER_NATIVE_TAB("crafting", [] { return std::make_unique<abp::client::ui::station::CraftingTab>(); });

#endif // ABP_CLIENT
