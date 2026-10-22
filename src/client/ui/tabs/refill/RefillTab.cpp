// Native tab of the refill upgrades (SB RefillUpgradeTab.Basic / .Advanced).
//
// Layout (SB): the filter slots (FilterLogicControl without buttons) at (x+3, y+24), 3 per row
// (basic, 6 slots) or 4 per row (advanced, 12 slots). Advanced: every filled slot shows the green
// acronym of its target slot (A / M / O / 1..9) at +10,+2, scrolling over a slot cycles the target
// (up: next, down: previous) and its tooltip adds "Refill <target>" and "Scroll to change target
// slot". Clicking a slot puts a copy of the cursor item in (an empty cursor clears it).
//
// Server side: upgrade/refill/Refill.cpp (tab data / action "refill").

#ifdef ABP_CLIENT

#include <algorithm>
#include <memory>
#include <vector>

#include "mc/world/item/ItemStack.h"

#include "client/Client.h"
#include "client/ui/screen/Tabs.h"
#include "core/ItemStackData.h"
#include "game/ItemConv.h"

namespace abp::client::ui {

namespace {

constexpr int kAny = 0, kMainHand = 1, kOffhand = 2, kHotbar1 = 3, kTargets = 12;

std::string acronym(int target) {
    switch (target) {
    case kAny:
        return ctr("abp.refill.target_slot.any", "A");
    case kMainHand:
        return ctr("abp.refill.target_slot.main_hand", "M");
    case kOffhand:
        return ctr("abp.refill.target_slot.off_hand", "O");
    default:
        return std::to_string(target - kHotbar1 + 1);
    }
}

std::string description(int target) {
    switch (target) {
    case kAny:
        return ctr("abp.refill.target_slot.any.tooltip", "Any slot");
    case kMainHand:
        return ctr("abp.refill.target_slot.main_hand.tooltip", "Main hand");
    case kOffhand:
        return ctr("abp.refill.target_slot.off_hand.tooltip", "Off hand");
    default:
        return ctr("abp.refill.target_slot.hotbar.tooltip", "Hotbar slot {}", target - kHotbar1 + 1);
    }
}

/// The filter slots: SlotGrid plus SB's target acronyms, scrolling and tooltip lines.
class RefillGrid final : public SlotGrid {
public:
    bool                                        advanced = false;
    std::vector<ItemStack>*                     ghosts   = nullptr;
    std::vector<int>*                           targets  = nullptr;
    std::function<void(int index, int dir)>     onScroll;

    void render(Painter& p, float mx, float my, double time) override {
        SlotGrid::render(p, mx, my, time);
        if (!advanced || !ghosts || !targets) return;
        for (int i = 0; i < count && i < static_cast<int>(ghosts->size()); ++i) {
            if ((*ghosts)[static_cast<std::size_t>(i)].isNull()) continue;
            float x = rect.x + static_cast<float>(i % columns) * 18 + 10;
            float y = rect.y + static_cast<float>(i / columns) * 18 + 2;
            int   t = i < static_cast<int>(targets->size()) ? (*targets)[static_cast<std::size_t>(i)] : kAny;
            p.text(acronym(t), x, y, Color::rgb(0x00FF00), true); // DyeColor.GREEN text colour
        }
    }

    bool scroll(float x, float y, float delta) override {
        int i = slotAt(x, y);
        if (i < 0) return hovered(x, y);
        if (!advanced) return true;
        if (onScroll) onScroll(i, delta > 0 ? 1 : -1);
        return true;
    }

    void tooltip(float x, float y, std::vector<TooltipLine>& out) const override {
        SlotGrid::tooltip(x, y, out);
        int i = slotAt(x, y);
        if (!advanced || i < 0 || !ghosts || i >= static_cast<int>(ghosts->size()) || (*ghosts)[static_cast<std::size_t>(i)].isNull()) return;
        int t = targets && i < static_cast<int>(targets->size()) ? (*targets)[static_cast<std::size_t>(i)] : kAny;
        out.push_back({"§7" + ctr("abp.refill.target_slot.tooltip", "Refill {}", "§2" + description(t)), 0xAAAAAA});
        out.push_back({"§8§o" + ctr("abp.refill.scroll.tooltip", "Scroll to change target slot"), 0x555555});
    }
};

class RefillTab final : public NativeTab {
public:
    Size build(TabContext const& ctx, Rect area, WidgetGroup& widgets) override {
        auto j    = data(ctx);
        mAdvanced = j.value("advanced", false);
        int perRow = std::max(1, j.value("slotsInRow", mAdvanced ? 4 : 3));
        update(ctx);
        int count = std::max(1, static_cast<int>(mGhosts.size()));
        int rows  = (count + perRow - 1) / perRow;

        auto& grid    = widgets.add(std::make_unique<RefillGrid>());
        grid.columns  = perRow;
        grid.count    = count;
        grid.rect     = {area.x + 3, area.y + 24, static_cast<float>(perRow) * 18, static_cast<float>(rows) * 18};
        grid.advanced = mAdvanced;
        grid.ghosts   = &mGhosts;
        grid.targets  = &mTargets;
        grid.item     = [this](int i) -> ItemStack const* {
            return i >= 0 && i < static_cast<int>(mGhosts.size()) && !mGhosts[static_cast<std::size_t>(i)].isNull() ? &mGhosts[static_cast<std::size_t>(i)] : nullptr;
        };
        auto send     = ctx.sendTabAction;
        grid.onClick  = [send](int i, int, bool) {
            if (send) send(nlohmann::json{{"refill", "ghost"}, {"index", i}});
        };
        grid.onScroll = [this, send](int i, int dir) {
            if (i < static_cast<int>(mTargets.size())) {
                auto& t = mTargets[static_cast<std::size_t>(i)];
                t       = (t + (dir > 0 ? 1 : kTargets - 1)) % kTargets; // shown right away; the server confirms
            }
            if (send) send(nlohmann::json{{"refill", "target"}, {"index", i}, {"dir", dir}});
        };
        // SB SettingsTabBase: content extent + 6.
        return {3 + static_cast<float>(perRow) * 18 + 6, 24 + static_cast<float>(rows) * 18 + 6};
    }

    void update(TabContext const& ctx) override {
        auto j = data(ctx);
        mGhosts.clear();
        for (auto const& g : j.value("ghosts", nlohmann::json::array())) {
            core::ItemStackData item;
            try {
                if (!g.is_null()) item = core::ItemStackData::fromJson(g);
            } catch (...) {
            }
            mGhosts.push_back(item.empty() ? ItemStack{} : game::ItemConv::toStack(item, 1));
        }
        mTargets.clear();
        for (auto const& t : j.value("targets", nlohmann::json::array())) mTargets.push_back(t.is_number_integer() ? t.get<int>() : kAny);
    }

private:
    static nlohmann::json data(TabContext const& ctx) {
        auto it = ctx.upgrade.tab.find("refill");
        return it != ctx.upgrade.tab.end() && it->is_object() ? *it : nlohmann::json::object();
    }

    bool                   mAdvanced = false;
    std::vector<ItemStack> mGhosts;
    std::vector<int>       mTargets;
};

} // namespace

ABP_REGISTER_NATIVE_TAB("refill", [] { return std::make_unique<RefillTab>(); });
ABP_REGISTER_NATIVE_TAB("advanced_refill", [] { return std::make_unique<RefillTab>(); });

} // namespace abp::client::ui

#endif // ABP_CLIENT
