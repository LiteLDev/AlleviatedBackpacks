#ifdef ABP_CLIENT

#include "client/ui/screen/Tabs.h"

#include <algorithm>
#include <unordered_map>

#include "client/Client.h"
#include "client/input/InputRouter.h"
#include "client/ui/UiRoot.h"
#include "game/ItemConv.h"

#include "mc/world/item/ItemStack.h"

namespace abp::client::ui {

namespace {
std::unordered_map<std::string, NativeTabFactory>& registry() {
    static std::unordered_map<std::string, NativeTabFactory> r;
    return r;
}

/// The grid of a server settings menu shown inside a tab (decoration slots left out).
class MenuGrid final : public Widget {
public:
    int                                                     columns = 9;
    std::vector<ItemStack>                                  stacks;
    std::vector<bool>                                       filler;
    std::vector<long long>                                  counts;
    std::function<void(int index, int button, bool shift)> onClick;

    [[nodiscard]] int slotAt(float x, float y) const {
        if (!hovered(x, y)) return -1;
        int i = static_cast<int>((y - rect.y) / 18) * columns + static_cast<int>((x - rect.x) / 18);
        return i >= 0 && i < static_cast<int>(stacks.size()) && !filler[static_cast<std::size_t>(i)] ? i : -1;
    }
    void render(Painter& p, float mx, float my, double) override {
        int hov = slotAt(mx, my);
        for (std::size_t i = 0; i < stacks.size(); ++i) {
            if (filler[i]) continue;
            float x = rect.x + 1 + static_cast<float>(i % static_cast<std::size_t>(columns)) * 18;
            float y = rect.y + 1 + static_cast<float>(i / static_cast<std::size_t>(columns)) * 18;
            sb::slotFrame(p, x, y);
            if (!stacks[i].isNull()) {
                p.item(stacks[i], x, y);
                if (counts[i] > 1) {
                    auto s = std::to_string(counts[i]);
                    p.text(s, x + 17 - p.textWidth(s), y + 9, Color::rgb(0xFFFFFF), true);
                }
            }
            if (static_cast<int>(i) == hov) p.fillRect({x, y, 16, 16}, sb::color::HOVER, Painter::Layer::Foreground);
        }
    }
    bool mouseDown(MouseButton button, float x, float y) override {
        int i = slotAt(x, y);
        if (i < 0) return hovered(x, y);
        if (onClick) onClick(i, button == MouseButton::Right ? 1 : 0, false);
        return true;
    }
    void tooltip(float x, float y, std::vector<TooltipLine>& out) const override {
        int i = slotAt(x, y);
        if (i < 0 || stacks[static_cast<std::size_t>(i)].isNull()) return;
        auto lines = itemTooltipLines(stacks[static_cast<std::size_t>(i)]);
        out.insert(out.end(), lines.begin(), lines.end());
    }
};

/// Built-in tab for upgrades without their own native tab: the upgrade's server settings menu
/// (filter editors, ...) as a slot grid inside the SB tab, or its description and a button that
/// opens its server-side settings when it has no menu.
class GenericUpgradeTab final : public NativeTab {
public:
    Size build(TabContext const& ctx, Rect area, WidgetGroup& widgets) override {
        float y     = area.y + 22;
        auto  menu  = ctx.upgrade.tab.find("menu");
        if (menu != ctx.upgrade.tab.end() && menu->is_object()) {
            auto& grid   = widgets.add(std::make_unique<MenuGrid>());
            mGrid        = &grid;
            int rows     = std::max(1, menu->value("rows", 1));
            // Server menus are 9 wide; wrap them when the tab would leave the screen.
            auto const& screen = UiRoot::get().lastFrame().screen;
            int  cols    = 9;
            if (!screen.empty()) {
                float avail = screen.x + screen.w - (area.x + 3) - 6;
                cols        = std::clamp(static_cast<int>(avail / 18), 1, 9);
            }
            if (cols < 9) rows = (rows * 9 + cols - 1) / cols;
            grid.columns = cols;
            grid.rect    = {area.x + 3, y, static_cast<float>(cols) * 18, static_cast<float>(rows) * 18};
            int  slot    = ctx.slot;
            auto send    = ctx.sendTabAction;
            grid.onClick = [send](int i, int button, bool) {
                if (send) send(nlohmann::json{{"menuClick", i}, {"button", button}, {"shift", input::shiftDown()}});
            };
            (void)slot;
            update(ctx);
            return {static_cast<float>(cols) * 18 + 3 + 6, 22 + static_cast<float>(rows) * 18 + 6};
        }
        mGrid       = nullptr;
        float width = 120;
        for (auto const& line : ctx.upgrade.lore) {
            auto& l  = widgets.add(std::make_unique<Label>());
            l.text   = line;
            l.rect   = {area.x + 6, y, 0, 9};
            l.color  = Color::rgb(0x404040);
            y       += 10;
        }
        auto& b         = widgets.add(std::make_unique<Button>());
        b.size          = Button::Size::Normal;
        b.rect          = {area.x + 6, y + 2, 18, 18};
        b.icon          = [](Painter& p, float x, float yy) { sb::icon(p, sb::Icon::Settings, x + 1, yy + 1); };
        b.tooltipLines  = {{ctr("abp.companion.tab.open_settings", "Open upgrade settings"), 0xFFFFFF}};
        int slot        = ctx.slot;
        auto send       = ctx.sendScreenAction;
        b.onClick       = [slot, send](int button) {
            if (button == 0 && send) send(nlohmann::json{{"action", "upgradeSettings"}, {"slot", slot}});
        };
        return {width, y + 2 + 18 + 6 - area.y};
    }

    void update(TabContext const& ctx) override {
        if (!mGrid) return;
        auto menu = ctx.upgrade.tab.find("menu");
        if (menu == ctx.upgrade.tab.end()) return;
        auto const& slots = menu->value("slots", nlohmann::json::array());
        mGrid->stacks.clear();
        mGrid->filler.clear();
        mGrid->counts.clear();
        for (auto const& s : slots) {
            core::ItemStackData d;
            try {
                if (!s.is_null()) d = core::ItemStackData::fromJson(s);
            } catch (...) {
            }
            bool filler = d.id == core::screen::kFillerId;
            mGrid->filler.push_back(filler);
            mGrid->counts.push_back(d.count);
            mGrid->stacks.push_back(filler || d.empty() ? ItemStack{} : game::ItemConv::toStack(d, 1));
        }
    }

private:
    MenuGrid* mGrid = nullptr; ///< owned by the tab's widget group
};} // namespace

void registerNativeTab(std::string upgradeId, NativeTabFactory factory) { registry()[std::move(upgradeId)] = std::move(factory); }

bool hasNativeTab(std::string const& upgradeId) { return registry().contains(upgradeId); }

std::unique_ptr<NativeTab> createNativeTab(std::string const& upgradeId) {
    auto& r = registry();
    if (auto it = r.find(upgradeId); it != r.end() && it->second) return it->second();
    if (auto it = r.find("*"); it != r.end() && it->second) return it->second();
    return std::make_unique<GenericUpgradeTab>();
}

} // namespace abp::client::ui

#endif // ABP_CLIENT
