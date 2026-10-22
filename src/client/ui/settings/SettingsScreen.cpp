#ifdef ABP_CLIENT

#include "client/ui/settings/SettingsScreen.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <sstream>
#include <vector>

#include "mc/client/player/LocalPlayer.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/item/ItemStack.h"

#include "client/Client.h"
#include "client/net/ClientNet.h"
#include "client/ui/settings/ItemTransform.h"
#include "client/tooltip/ContentsTooltip.h"
#include "client/ui/gfx/Painter.h"
#include "client/ui/settings/SettingsIcons.h"
#include "client/ui/widgets/SbStyle.h"
#include "client/ui/widgets/TextBox.h"
#include "client/ui/widgets/Tooltip.h"
#include "client/ui/widgets/Widgets.h"
#include "core/Appearance.h"
#include "core/screen/StorageLayout.h"
#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "mod/Log.h"

namespace abp::client::ui::settings {

namespace {
namespace cs = core::screen;
using nlohmann::json;

constexpr int VK_RETURN_ = 0x0D;
constexpr int VK_ESCAPE_ = 0x1B;

double now() {
    static auto const start = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

float easeInOutCubic(float t) { return t < 0.5f ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3.f) / 2; }

json settingsOf(cs::BackpackScreenModel const& m) {
    auto it = m.extra.find("settings");
    return it != m.extra.end() && it->is_object() ? *it : json::object();
}

void send(json body) {
    body["action"] = "setting";
    net::sendScreenAction(std::move(body));
}

/// Translated text split on '\n' into tooltip lines of `rgb`.
std::vector<TooltipLine> lines(std::string const& text, std::uint32_t rgb) {
    std::vector<TooltipLine> out;
    std::stringstream        ss(text);
    std::string              line;
    while (std::getline(ss, line, '\n')) out.push_back({line, rgb});
    return out;
}

constexpr std::uint32_t kWhite = 0xFFFFFF;
constexpr std::uint32_t kGray  = 0xAAAAAA;

std::string tr(std::string_view key, std::string_view fallback) { return ctr(key, fallback); }

std::string truncated(Painter& p, std::string const& text, float maxWidth) {
    if (p.textWidth(text) <= maxWidth) return text;
    std::string out = text;
    while (!out.empty() && p.textWidth(out + "...") > maxWidth) {
        std::size_t i = out.size() - 1;
        while (i > 0 && (static_cast<unsigned char>(out[i]) & 0xC0) == 0x80) --i;
        out.erase(i);
    }
    return out + "...";
}

int buttonIndex(MouseButton b) { return b == MouseButton::Left ? 0 : b == MouseButton::Right ? 1 : 2; }

// ---- SC controls --------------------------------------------------------------------------------------

/// SC `Button` / `ToggleButton` with a 16x16 ButtonDefinition: 16x16 hit area, the 18x18 default
/// button background, the icon at +1,+1.
class SbButton final : public Widget {
public:
    std::function<int()>                                  state;   ///< for toggles (index into icons/tips)
    std::vector<IconFn>                                   icons;   ///< per state (or one)
    std::function<std::vector<TooltipLine>()>            tip;
    std::function<void(int button)>                       onClick;

    void render(Painter& p, float mx, float my, double) override {
        if (!visible) return;
        sb::buttonBackground(p, {rect.x, rect.y, 18, 18}, hovered(mx, my));
        int i = state ? state() : 0;
        if (i >= 0 && i < static_cast<int>(icons.size()) && icons[static_cast<std::size_t>(i)]) icons[static_cast<std::size_t>(i)](p, rect.x + 1, rect.y + 1);
    }
    bool mouseDown(MouseButton button, float x, float y) override {
        if (!hovered(x, y)) return false;
        if (onClick) onClick(buttonIndex(button));
        return true;
    }
    void tooltip(float x, float y, std::vector<TooltipLine>& out) const override {
        if (!hovered(x, y) || !tip) return;
        auto l = tip();
        out.insert(out.end(), l.begin(), l.end());
    }
};

/// SC ColorToggleButton: 18x18; the dye colour (alpha 200) in a 12x12 square at +3,+3.
class ColorToggle final : public Widget {
public:
    std::function<int()>            dye;
    std::function<void(int button)> onClick;

    void render(Painter& p, float mx, float my, double) override {
        sb::buttonBackground(p, rect, hovered(mx, my));
        int d = dye ? std::clamp(dye(), 0, 15) : 0;
        sb::colorSquare(p, rect.x, rect.y, core::kDyes[static_cast<std::size_t>(d)].argb & 0xFFFFFF);
    }
    bool mouseDown(MouseButton button, float x, float y) override {
        if (!hovered(x, y)) return false;
        if (onClick) onClick(buttonIndex(button));
        return true;
    }
    void tooltip(float x, float y, std::vector<TooltipLine>& out) const override {
        if (!hovered(x, y)) return;
        out.push_back({tr("abp.sbsettings.buttons.toggle_color", "Toggle Color"), kWhite});
        auto d = lines(tr("abp.sbsettings.buttons.toggle_color_detail", "Next = Left Click\nPrevious = Right Click"), kGray);
        out.insert(out.end(), d.begin(), d.end());
    }
};

/// SC ContextButton: 62x18, the context's name centred in white, tooltip of the context.
class ContextButton final : public Widget {
public:
    std::function<bool()> playerContext;

    void render(Painter& p, float mx, float my, double) override {
        sb::buttonBackground(p, rect, hovered(mx, my));
        auto text = playerContext() ? tr("abp.sbsettings.buttons.context_player", "Player")
                                    : tr("abp.sbsettings.buttons.context_backpack", "Backpack");
        p.text(text, rect.x + rect.w / 2, rect.y - 4 + rect.h / 2, Color::rgb(0xFFFFFF), true, 1, TextAlign::Center);
    }
    bool mouseDown(MouseButton button, float x, float y) override {
        if (!hovered(x, y)) return false;
        (void)button;
        send({{"what", "context"}});
        return true;
    }
    void tooltip(float x, float y, std::vector<TooltipLine>& out) const override {
        if (!hovered(x, y)) return;
        if (playerContext()) {
            out.push_back({tr("abp.sbsettings.buttons.context_player.tooltip", "Player level settings"), kWhite});
            out.push_back({tr("abp.sbsettings.buttons.context_player.tooltip_detail", "Apply to all backpacks/storages unless overriden"), kGray});
        } else {
            out.push_back({tr("abp.sbsettings.buttons.context_backpack.tooltip", "This Backpack's settings"), kWhite});
            out.push_back({tr("abp.sbsettings.buttons.context_backpack.tooltip_detail", "Inherited from player or overriden for this backpack"), kGray});
        }
    }
};

IconFn sbIcon(sb::Icon i) {
    return [i](Painter& p, float x, float y) { sb::icon(p, i, x, y); };
}
IconFn ownIcon(Icon i) {
    return [i](Painter& p, float x, float y) { icon(p, i, x, y); };
}

enum class TabKind { Back, Main, ItemDisplay, NoSort, Memory };

struct Tab {
    TabKind     kind;
    Rect        rect; ///< current (closed 21x24 or open size)
    bool        open = false;
    WidgetGroup widgets;
    float       openW = 0, openH = 0;
};

} // namespace

// ---------------------------------------------------------------------------------------------------------

struct SettingsScreen::Impl {
    bool          opened       = false;
    bool          layoutLogged = false;
    std::uint64_t revision = ~std::uint64_t{0};
    std::uint32_t session  = 0;
    json          settings = json::object();

    std::vector<ItemStack> slotStacks;
    std::vector<ItemStack> memoryStacks;
    std::vector<ItemStack> templateMemoryStacks;
    ItemStack              backpackStack;
    int                    backpackTier = -1;

    cs::StorageLayout            layout;
    float                        left = 0, top = 0;
    std::vector<cs::PlacedSlot>  placed;
    int                          visibleCount = 0;
    cs::ScrollState              scrollState;
    bool                         draggingBar = false;
    float                        dragY       = 0;

    std::vector<Tab> tabs;
    int              openTab = -1;
    bool             tabsItemDisplay = true;

    int currentDisplaySlot = -1;

    // RotatablePreviewWidget
    float  rotX = 30, rotY = 45, fromX = 30, fromY = 45, targetX = 30, targetY = 45;
    double targetSetAt   = -10;
    bool   previewDrag   = false;
    float  lastMx = 0, lastMy = 0;
    Rect   previewRect;

    // TemplatePersistanceControl
    Rect    saveBtn, loadBtn, exportBtn;
    TextBox saveBox, exportBox;
    bool    saveHover = false, loadHover = false, exportHover = false;

    // slot selection by click / drag
    int dragButton   = -1;
    int dragLastSlot = -1;
    int hoverSlot    = -1;

    float mouseX = -1, mouseY = -1;

    // ---- model -------------------------------------------------------------------------------------
    void sync(cs::BackpackScreenModel const& m) {
        auto rev = net::screenRevision();
        if (rev == revision && m.session == session) return;
        revision = rev;
        session  = m.session;
        settings = settingsOf(m);
        slotStacks.clear();
        for (auto const& d : m.slots) slotStacks.push_back(d.empty() ? ItemStack{} : game::ItemConv::toStack(d, 1));
        memoryStacks.clear();
        for (auto const& d : m.memory) memoryStacks.push_back(d.empty() ? ItemStack{} : game::ItemConv::toStack(d, 1));
        templateMemoryStacks.assign(m.slots.size(), ItemStack{});
        {
            auto prev = preview();
            if (auto mem = prev.find("memory"); mem != prev.end() && mem->is_object()) {
                for (auto const& [k, v] : mem->items()) {
                    int s = std::atoi(k.c_str());
                    if (s >= 0 && s < static_cast<int>(templateMemoryStacks.size())) {
                        auto d = core::ItemStackData::fromJson(v);
                        if (!d.empty()) templateMemoryStacks[static_cast<std::size_t>(s)] = game::ItemConv::toStack(d, 1);
                    }
                }
            }
        }
        // Keep the item display selection frame on a selected slot (template loads, refused selects).
        auto sel = displaySlots(false);
        if (std::find(sel.begin(), sel.end(), currentDisplaySlot) == sel.end()) currentDisplaySlot = sel.empty() ? -1 : sel.front();
        if (backpackTier != m.tier) {
            backpackTier  = m.tier;
            backpackStack = game::makeBackpackItem(static_cast<core::Tier>(std::clamp(m.tier, 0, 5)));
        }
    }

    [[nodiscard]] bool loadHovered() const {
        auto t = settings.value("templates", json::object());
        return loadHover && t.is_object() && t.value("load", json::object()).value("index", -1) >= 0;
    }

    /// The selected load template's preview (`templates.load.preview`), {} when none.
    [[nodiscard]] json preview() const {
        auto t = settings.value("templates", json::object());
        if (!t.is_object()) return json::object();
        auto l = t.value("load", json::object());
        if (!l.is_object()) return json::object();
        auto p = l.value("preview", json::object());
        return p.is_object() ? p : json::object();
    }

    [[nodiscard]] json displayJson(bool fromTemplate) const {
        auto j = fromTemplate ? preview().value("itemDisplay", json::object()) : settings.value("itemDisplay", json::object());
        return j.is_object() ? j : json::object();
    }

    [[nodiscard]] std::vector<int> displaySlots(bool fromTemplate) const {
        std::vector<int> out;
        auto             j = displayJson(fromTemplate);
        if (auto it = j.find("slots"); it != j.end() && it->is_array()) {
            for (auto const& v : *it) {
                if (v.is_number_integer()) out.push_back(v.get<int>());
            }
        }
        return out;
    }

    [[nodiscard]] int displayRotation(int slot, bool fromTemplate) const {
        auto j = displayJson(fromTemplate);
        if (fromTemplate) {
            auto rot = j.value("rotations", json::object());
            return rot.is_object() ? rot.value(std::to_string(slot), 0) : 0;
        }
        auto slots = j.value("slots", std::vector<int>{});
        auto rots  = j.value("rotations", std::vector<int>{});
        for (std::size_t i = 0; i < slots.size() && i < rots.size(); ++i) {
            if (slots[i] == slot) return rots[i];
        }
        return 0;
    }

    [[nodiscard]] bool memorySelected(int slot) const {
        return slot >= 0 && slot < static_cast<int>(memoryStacks.size()) && !memoryStacks[static_cast<std::size_t>(slot)].isNull();
    }

    // ---- tabs ------------------------------------------------------------------------------------
    void buildTabs() {
        tabs.clear();
        openTab         = -1;
        tabsItemDisplay = settings.value("itemDisplayEnabled", true);
        tabs.push_back({TabKind::Back, {}, false, {}, 0, 0});
        tabs.push_back({TabKind::Main, {}, false, {}, 0, 0});
        if (tabsItemDisplay) tabs.push_back({TabKind::ItemDisplay, {}, false, {}, 0, 0});
        tabs.push_back({TabKind::NoSort, {}, false, {}, 0, 0});
        tabs.push_back({TabKind::Memory, {}, false, {}, 0, 0});
    }

    void placeTabs() {
        float tx = left + static_cast<float>(layout.imageWidth), ty = top + 4;
        for (std::size_t i = 0; i < tabs.size(); ++i) {
            auto& t = tabs[i];
            Rect  r{tx, ty + static_cast<float>(i) * 25, 21, 24};
            if (t.open) {
                r.w = std::max(t.openW, 21.f);
                r.h = t.openH;
            }
            if (t.open && (t.rect.x != r.x || t.rect.y != r.y)) {
                t.rect = r;
                buildTabWidgets(t); // the screen moved (resize): widgets follow
            }
            t.rect = r;
        }
    }

    /// SettingsTabBase.updateOpenTabDimension over the hideable children.
    static void fit(Tab& t, Rect const& widget) {
        t.openW = std::max(t.openW, widget.x + widget.w + 6 - t.rect.x);
        t.openH = std::max(t.openH, widget.y + widget.h + 6 - t.rect.y);
    }

    template <class W>
    W& add(Tab& t, std::unique_ptr<W> w) {
        fit(t, w->rect);
        return t.widgets.add(std::move(w));
    }

    void buildTabWidgets(Tab& t) {
        t.widgets.widgets.clear();
        t.openW = 0;
        t.openH = 0;
        float x = t.rect.x, y = t.rect.y;
        auto  mainValue = [this](char const* key, bool def) {
            auto m = settings.value("main", json::object());
            return m.value(key, def);
        };
        switch (t.kind) {
        case TabKind::Back:
            return;
        case TabKind::Main: {
            auto c           = std::make_unique<ContextButton>();
            c->rect          = {x + 3, y + 24, 62, 18};
            c->playerContext = [this] { return settings.value("context", std::string("player")) != "storage"; };
            add(t, std::move(c));
            struct Def {
                char const* key;
                bool        def;
                IconFn      on, off;
                char const* tkey;
                char const* onText;
                char const* onTip;
                char const* offText;
                char const* offTip;
            };
            std::vector<Def> defs{
                {"shiftClickOpenTab", false, sbIcon(sb::Icon::ShiftClickTabOn), sbIcon(sb::Icon::ShiftClickTabOff), "shift_click_open_tab",
                 "Shift Click Into Open Tab First",
                 "Shift click from storage/inventory will first try to put the stack in an open tab and only then into inventory/storage.",
                 "Shift Click Into Inventory First",
                 "Shift click from storage/inventory will first try to put the stack in inventory/storage and only then into an open tab."},
                {"keepTabOpen", true, sbIcon(sb::Icon::KeepTabOpenOn), sbIcon(sb::Icon::KeepTabOpenOff), "keep_tab_open", "Keep Tab Open: ON",
                 "On close of its gui the backpack/storage records which upgrade tab was last open and opens it when the gui is open next time",
                 "Keep Tab Open: OFF",
                 "Open upgrade tab gets closed when the backpack/storage gui is closed and when the gui is next open all of the tabs are closed"},
                {"keepSearchPhrase", true, sbIcon(sb::Icon::KeepSearchOn), sbIcon(sb::Icon::KeepSearchOff), "keep_search_phrase",
                 "Keep Search Phrase: ON", "Backpack / Storage gui keeps the search phrase and prefills it / filters by it when open again",
                 "Keep Search Phrase: OFF", "Backpack / Storage gui clears the search phrase when closed and shows all unfiltered item when open again"},
            };
            if (settings.value("showAnotherPlayerCanOpen", true)) {
                defs.push_back({"anotherPlayerCanOpen", true, ownIcon(Icon::AnotherPlayerOn), ownIcon(Icon::AnotherPlayerOff), "another_player_can_open",
                                "Another player can open",
                                "When this backpack is worn and visible another player can open it when right clicking on this player's back",
                                "Another player can NOT open",
                                "Backpack can not be open by another player when right clicking on this player's back even if the backpack is worn there"});
            }
            float bx = x + 3;
            for (auto const& d : defs) {
                auto b     = std::make_unique<SbButton>();
                b->rect    = {bx, y + 46, 16, 16};
                b->icons   = {d.off, d.on};
                b->state   = [mainValue, d] { return mainValue(d.key, d.def) ? 1 : 0; };
                b->tip     = [mainValue, d]() -> std::vector<TooltipLine> {
                    bool        on   = mainValue(d.key, d.def);
                    std::string base = std::string("abp.sbsettings.buttons.") + d.tkey + (on ? ".on" : ".off");
                    return {{ctr(base, on ? d.onText : d.offText), kWhite}, {ctr(base + ".tooltip", on ? d.onTip : d.offTip), kGray}};
                };
                b->onClick = [key = std::string(d.key)](int) { send({{"what", "main"}, {"name", key}}); };
                add(t, std::move(b));
                bx += 18;
            }
            return;
        }
        case TabKind::NoSort:
        case TabKind::Memory: {
            bool        memory = t.kind == TabKind::Memory;
            char const* what   = memory ? "memory" : "noSort";
            auto        all    = std::make_unique<SbButton>();
            all->rect          = {x + 3, y + 24, 16, 16};
            all->icons         = {sbIcon(sb::Icon::SelectAll)};
            all->tip           = [] { return std::vector<TooltipLine>{{tr("abp.sbsettings.buttons.select_all_slots", "Select All Slots"), kWhite}}; };
            all->onClick       = [what](int) { send({{"what", what}, {"op", "selectAll"}}); };
            add(t, std::move(all));
            auto none     = std::make_unique<SbButton>();
            none->rect    = {x + 21, y + 24, 16, 16};
            none->icons   = {sbIcon(sb::Icon::UnselectAll)};
            none->tip     = [] { return std::vector<TooltipLine>{{tr("abp.sbsettings.buttons.unselect_all_slots", "Unselect All Slots"), kWhite}}; };
            none->onClick = [what](int) { send({{"what", what}, {"op", "unselectAll"}}); };
            add(t, std::move(none));
            if (memory) {
                auto nbt     = std::make_unique<SbButton>();
                nbt->rect    = {x + 39, y + 24, 16, 16};
                nbt->icons   = {sbIcon(sb::Icon::IgnoreNbt), sbIcon(sb::Icon::MatchNbt)};
                nbt->state   = [this] { return settings.value("memoryIgnoreNbt", true) ? 0 : 1; };
                nbt->tip     = [this]() -> std::vector<TooltipLine> {
                    if (settings.value("memoryIgnoreNbt", true)) return {{tr("abp.sbsettings.buttons.ignore_nbt", "Ignore NBT"), kWhite}};
                    return {{tr("abp.sbsettings.buttons.match_nbt", "Match NBT"), kWhite}};
                };
                nbt->onClick = [this](int) { send({{"what", "memoryIgnoreNbt"}, {"value", !settings.value("memoryIgnoreNbt", true)}}); };
                add(t, std::move(nbt));
            } else {
                auto c     = std::make_unique<ColorToggle>();
                c->rect    = {x + 39, y + 24, 18, 18};
                c->dye     = [this] { return settings.value("noSortColor", static_cast<int>(core::DyeColor::Lime)); };
                c->onClick = [this](int button) {
                    if (button > 1) return;
                    int n   = 16;
                    int cur = settings.value("noSortColor", static_cast<int>(core::DyeColor::Lime));
                    send({{"what", "noSortColor"}, {"dye", (cur + (button == 0 ? 1 : n - 1)) % n}});
                };
                add(t, std::move(c));
            }
            return;
        }
        case TabKind::ItemDisplay: {
            previewRect = {x + 3, y + 24, 72, 72};
            fit(t, previewRect);
            float bx = x + 3, by = y + 24 + 72 + 1;
            auto  c   = std::make_unique<ColorToggle>();
            c->rect   = {bx, by, 18, 18};
            c->dye    = [this] { return settings.value("itemDisplay", json::object()).value("color", static_cast<int>(core::DyeColor::Red)); };
            c->onClick = [this](int button) {
                if (button > 1) return;
                int cur = settings.value("itemDisplay", json::object()).value("color", static_cast<int>(core::DyeColor::Red));
                send({{"what", "itemDisplay"}, {"op", "color"}, {"dye", (cur + (button == 0 ? 1 : 15)) % 16}});
            };
            add(t, std::move(c));
            bx += 16 + 2;
            auto rot     = std::make_unique<SbButton>();
            rot->rect    = {bx, by, 16, 16};
            rot->icons   = {ownIcon(Icon::Rotate)};
            rot->tip     = [] {
                auto out = std::vector<TooltipLine>{{tr("abp.sbsettings.buttons.rotate", "Rotate"), kWhite}};
                auto d   = lines(tr("abp.sbsettings.buttons.rotate_detail", "Clockwise = Left Click\nCounter-Clockwise = Right Click"), kGray);
                out.insert(out.end(), d.begin(), d.end());
                return out;
            };
            rot->onClick = [this](int button) {
                if (button > 1) return;
                send({{"what", "itemDisplay"}, {"op", "rotate"}, {"index", currentDisplaySlot}, {"clockwise", button == 0}});
            };
            add(t, std::move(rot));
            bx += 16 + 2;
            auto z     = std::make_unique<SbButton>();
            z->rect    = {bx, by, 16, 16};
            z->icons   = {ownIcon(Icon::Depth)};
            z->tip     = [] {
                auto out = std::vector<TooltipLine>{{tr("abp.sbsettings.buttons.z_offset", "Depth"), kWhite}};
                auto d   = lines(tr("abp.sbsettings.buttons.z_offset_detail", "Out = Left Click\nIn = Right Click"), kGray);
                out.insert(out.end(), d.begin(), d.end());
                return out;
            };
            z->onClick = [this](int button) {
                send({{"what", "itemDisplay"}, {"op", "zOffset"}, {"index", currentDisplaySlot}, {"change", button == 0 ? 1 : -1}});
            };
            add(t, std::move(z));
            return;
        }
        }
    }

    [[nodiscard]] Tab* openTabPtr() { return openTab >= 0 && openTab < static_cast<int>(tabs.size()) ? &tabs[static_cast<std::size_t>(openTab)] : nullptr; }

    void setOpen(int i, bool open) {
        if (auto* cur = openTabPtr(); cur && open && openTab != i) {
            cur->open = false;
            cur->widgets.widgets.clear();
        }
        auto& t = tabs[static_cast<std::size_t>(i)];
        t.open  = open;
        if (open) {
            openTab = i;
            t.rect.w = 21;
            t.rect.h = 24;
            buildTabWidgets(t);
            if (t.kind == TabKind::ItemDisplay) {
                // updateTargetRotation: FRONT -> (30, 45).
                setTargetRotation(30, 45);
            }
        } else {
            t.widgets.widgets.clear();
            if (openTab == i) openTab = -1;
        }
        placeTabs();
    }

    void setTargetRotation(float x, float y) {
        if (targetX == x && targetY == y) return;
        fromX       = rotX;
        fromY       = rotY;
        targetX     = x;
        targetY     = y;
        targetSetAt = now();
    }

    void updateRotation() {
        double t = now() - targetSetAt;
        if (t <= 1.0) {
            float r = easeInOutCubic(static_cast<float>(t));
            rotX    = fromX + (targetX - fromX) * r;
            rotY    = fromY + (targetY - fromY) * r;
        } else {
            rotX = targetX;
            rotY = targetY;
        }
    }

    // SettingsTabControl: a closed tab isn't drawn while the open one covers it fully, and shows no
    // tooltip while the open one covers its top.
    [[nodiscard]] bool tabVisible(Tab const& t) const {
        auto const* o = openTab >= 0 ? &tabs[static_cast<std::size_t>(openTab)] : nullptr;
        if (!o || o == &t) return true;
        return o->rect.bottom() < t.rect.bottom() || o->rect.y > t.rect.y;
    }
    [[nodiscard]] bool tabTooltipVisible(Tab const& t) const {
        auto const* o = openTab >= 0 ? &tabs[static_cast<std::size_t>(openTab)] : nullptr;
        if (!o) return true;
        return o->rect.bottom() < t.rect.y || o->rect.y > t.rect.y;
    }

    // ---- slots -----------------------------------------------------------------------------------
    [[nodiscard]] int slotAt(float x, float y) const {
        for (auto const& ps : placed) {
            Rect r{left + static_cast<float>(ps.pos.x) - 1, top + static_cast<float>(ps.pos.y) - 1, 18, 18};
            if (r.contains(x, y)) return ps.index;
        }
        return -1;
    }

    void slotClick(int slot, int button) {
        auto* t = openTabPtr();
        if (!t || slot < 0 || (button != 0 && button != 1)) return;
        switch (t->kind) {
        case TabKind::NoSort:
        case TabKind::Memory:
            send({{"what", t->kind == TabKind::Memory ? "memory" : "noSort"}, {"op", button == 0 ? "select" : "unselect"}, {"index", slot}});
            break;
        case TabKind::ItemDisplay: {
            auto sel = displaySlots(false);
            bool has = std::find(sel.begin(), sel.end(), slot) != sel.end();
            if (button == 0) {
                if (!has) send({{"what", "itemDisplay"}, {"op", "select"}, {"index", slot}});
                // SC: the selection frame moves to the slot when it is (or becomes) selected; the
                // server allows one slot.
                if (has || sel.empty()) currentDisplaySlot = slot;
            } else if (has) {
                send({{"what", "itemDisplay"}, {"op", "unselect"}, {"index", slot}});
                if (currentDisplaySlot == slot) {
                    sel.erase(std::remove(sel.begin(), sel.end(), slot), sel.end());
                    currentDisplaySlot = sel.empty() ? -1 : sel.front();
                }
            }
            break;
        }
        default:
            break;
        }
    }

    // ---- template persistence -----------------------------------------------------------------------
    [[nodiscard]] json templates() const {
        auto t = settings.value("templates", json::object());
        return t.is_object() ? t : json::object();
    }

    [[nodiscard]] std::vector<TooltipLine> saveTooltip() const {
        auto        save = templates().value("save", json::object());
        std::string name = save.value("custom", false) ? tr("abp.sbsettings.buttons.save_template.custom_name_slot", "Enter Slot Name")
                                                       : save.value("name", std::string("1"));
        return {{ctr("abp.sbsettings.buttons.save_template", "Save Settings to slot {}", "§a" + name + "§r"), kWhite},
                {"§o" + tr("abp.sbsettings.buttons.save_template.controls", "Scroll to change slot"), 0x555555}};
    }

    [[nodiscard]] std::vector<TooltipLine> loadTooltip() const {
        auto load = templates().value("load", json::object());
        if (load.value("index", -1) < 0) return {{tr("abp.sbsettings.buttons.load_template.no_save", "No Settings to Load exist"), 0xFF5555}};
        std::vector<TooltipLine> out{
            {ctr("abp.sbsettings.buttons.load_template", "Load Settings from slot {}", "§a" + load.value("name", std::string{}) + "§r"), kWhite}
        };
        if (auto source = load.value("source", std::string{}); !source.empty()) {
            out.push_back({"§o" + ctr("abp.sbsettings.buttons.load_template.source", "From {} data pack", "§7§o" + source + "§8§o"), 0x555555});
        }
        out.push_back({"§o" + tr("abp.sbsettings.buttons.load_template.controls", "Scroll to change slot"), 0x555555});
        if (load.value("tooLarge", false)) {
            out.push_back({tr("abp.sbsettings.buttons.load_template.too_many_setting_slots", "Settings don't fit in this storage"), 0xFF5555});
        }
        return out;
    }

    [[nodiscard]] std::vector<TooltipLine> exportTooltip() const {
        return {{ctr("abp.sbsettings.buttons.export_template", "Export Settings to {}",
                     "§a" + tr("abp.sbsettings.buttons.export_template.enter_file_name", "Enter file name") + "§r"),
                 kWhite},
                {"§o" + tr("abp.sbsettings.buttons.export_template.additional_info", "Creates data pack with snbt file"), 0x555555}};
    }

    void save() {
        send({{"what", "template"}, {"op", "save"}, {"name", saveBox.text()}});
        saveBox.setText({});
    }
    void exportTemplate() {
        send({{"what", "template"}, {"op", "export"}, {"fileName", exportBox.text()}});
        exportBox.setText({});
    }
};

// ---------------------------------------------------------------------------------------------------------

SettingsScreen::SettingsScreen() : mImpl(std::make_unique<Impl>()) {
    for (auto* box : {&mImpl->saveBox, &mImpl->exportBox}) {
        box->bordered  = true;
        box->maxLength = 64;
    }
}
SettingsScreen::~SettingsScreen() = default;

bool SettingsScreen::isOpen() const { return mImpl->opened; }

void SettingsScreen::open(core::screen::BackpackScreenModel const& model) {
    auto& d = *mImpl;
    d.opened       = true;
    d.layoutLogged = false;
    d.revision     = ~std::uint64_t{0};
    d.scrollState = {};
    d.draggingBar = false;
    d.dragButton  = -1;
    d.previewDrag = false;
    d.saveBox.setText({});
    d.exportBox.setText({});
    d.saveBox.setFocused(false);
    d.exportBox.setFocused(false);
    d.sync(model);
    d.buildTabs();
    auto sel             = d.displaySlots(false);
    d.currentDisplaySlot = sel.empty() ? -1 : sel.front();
    d.rotX = d.fromX = d.targetX = 30;
    d.rotY = d.fromY = d.targetY = 45;
    net::sendScreenAction({{"action", "settings"}});
    log().info("settings screen: opened (session {})", model.session);
}

void SettingsScreen::close() {
    auto& d = *mImpl;
    if (d.opened) send({{"what", "closed"}});
    d.opened = false;
    d.tabs.clear();
    d.openTab = -1;
    d.saveBox.setFocused(false);
    d.exportBox.setFocused(false);
}

bool SettingsScreen::wantsText() const { return mImpl->saveBox.focused() || mImpl->exportBox.focused(); }

void SettingsScreen::render(Painter& p, core::screen::BackpackScreenModel const& m, FrameInfo const& f) {
    auto& d = *mImpl;
    d.sync(m);
    if (d.tabs.empty() || d.tabsItemDisplay != d.settings.value("itemDisplayEnabled", true)) d.buildTabs();

    cs::LayoutInput in;
    in.storageSlots = m.storageSlots;
    in.upgradeSlots = 0;
    in.screenHeight = static_cast<int>(f.screen.h);
    in.sortButtons  = cs::SortButtonsPosition::Hidden;
    d.layout        = cs::computeLayout(in);
    d.left          = std::floor((f.screen.w - static_cast<float>(d.layout.imageWidth)) / 2);
    d.top           = std::floor((f.screen.h - static_cast<float>(d.layout.imageHeight)) / 2);
    cs::scrollBy(d.scrollState, d.layout, m.storageSlots, 0);
    d.placed = cs::placeStorageSlots(d.layout, m.storageSlots, {}, d.scrollState, &d.visibleCount);
    d.placeTabs();
    if (!d.layoutLogged) {
        d.layoutLogged = true;
        log().info(
            "settings screen: layout left={} top={} width={} height={} invLabel={},{} tabs={} scroll={}",
            d.left, d.top, d.layout.imageWidth, d.layout.imageHeight, d.layout.inventoryLabelX, d.layout.inventoryLabelY, d.tabs.size(),
            d.layout.scroll
        );
    }

    float  mx = f.mouseX, my = f.mouseY;
    double t  = now();
    d.mouseX  = mx;
    d.mouseY  = my;
    float L = d.left, T = d.top;

    // Template persistence control (SettingsScreen.initializeTemplatePersistanceControl).
    float cx = L + static_cast<float>(d.layout.inventoryLabelX) - 29, cy = T + static_cast<float>(d.layout.inventoryLabelY) + 29;
    d.saveBtn   = {cx, cy, 16, 16};
    d.loadBtn   = {cx, cy + 18, 16, 16};
    d.exportBtn = {cx, cy + 36, 16, 16};
    d.saveHover   = d.saveBtn.contains(mx, my);
    d.loadHover   = d.loadBtn.contains(mx, my);
    d.exportHover = d.exportBtn.contains(mx, my);
    bool customSave = d.templates().value("save", json::object()).value("custom", false);
    // The save / export text boxes show (and take the keyboard) while their button is hovered.
    bool showSave   = d.saveHover && customSave;
    d.saveBox.rect   = {cx + 20, cy, 192, 18};
    d.exportBox.rect = {cx + 20, cy + 36, 171, 18};
    if (showSave != d.saveBox.focused()) d.saveBox.setFocused(showSave);
    if (d.exportHover != d.exportBox.focused()) d.exportBox.setFocused(d.exportHover);

    auto* open = d.openTabPtr();
    bool  overOpenTab = open && open->rect.contains(mx, my);
    d.hoverSlot       = overOpenTab ? -1 : d.slotAt(mx, my);
    bool  loadHovered = d.loadHovered();

    // Screen.renderTransparentBackground.
    p.gradientRect(f.screen, Color::argb(0xC0101010), Color::argb(0xD0101010));
    p.flush();

    // ---- tabs (closed first, the open one on top) ----
    auto drawTab = [&](Tab& tab) {
        sb::tabBackground(p, tab.rect);
        float ix = tab.rect.x + 1, iy = tab.rect.y + 4;
        switch (tab.kind) {
        case TabKind::Back:
            sb::icon(p, sb::Icon::BackToBackpack, ix, iy);
            break;
        case TabKind::Main:
            sb::icon(p, sb::Icon::BackpackSettings, ix, iy);
            break;
        case TabKind::ItemDisplay:
            icon(p, Icon::ItemDisplayTab, ix, iy);
            break;
        case TabKind::NoSort:
            sb::icon(p, sb::Icon::NoSortTab, ix, iy);
            break;
        case TabKind::Memory:
            sb::icon(p, sb::Icon::MemoryTab, ix, iy);
            break;
        }
        if (!tab.open) return;
        std::string title = tab.kind == TabKind::Main          ? tr("abp.sbsettings.backpack", "Backpack")
                          : tab.kind == TabKind::ItemDisplay ? tr("abp.sbsettings.item_display", "Item Display")
                          : tab.kind == TabKind::NoSort      ? tr("abp.sbsettings.no_sort", "No Sort")
                                                             : tr("abp.sbsettings.memory", "Memory");
        p.text(truncated(p, title, std::max(0.f, tab.rect.w - 26)), tab.rect.x + 20, tab.rect.y + 8, Color::rgb(0x404040));
        tab.widgets.render(p, mx, my, t);
        if (tab.kind == TabKind::ItemDisplay) {
            // RotatablePreviewWidget: black square with the backpack (item display preview provider:
            // the backpack itself, turned 180 degrees).
            p.fillRect(d.previewRect, Color::argb(0xFF000000), Painter::Layer::Foreground);
            d.updateRotation();
            // The open backpack's own item when it is carried, else a plain item of its tier.
            ItemStack const* pv = &d.backpackStack;
            if (auto* player = currentPlayer(); player && m.lockedSlot >= 0 && m.lockedSlot < 36) {
                auto const& carried = player->getInventory().getItem(m.lockedSlot);
                if (!carried.isNull() && game::isBackpack(carried)) pv = &carried;
            }
            if (!pv->isNull()) {
                float size  = std::min(d.previewRect.w, d.previewRect.h) - 16;
                // SB turns its backpack model by 180 degrees (the model faces backwards); the Bedrock
                // icon faces the viewer, and like Java's flat items it is shown from both sides.
                float      ry = d.rotY, rx = -d.rotX;
                auto const facesAway = [](float deg) { return std::cos(deg * 3.14159265f / 180.f) < 0; };
                if (facesAway(ry) != facesAway(rx)) ry += 180;
                drawItemTransformed(p, *pv, d.previewRect.x + d.previewRect.w / 2, d.previewRect.y + d.previewRect.h / 2, size / 16.f, rx,
                                    ry, 0);
            }
        }
    };
    for (std::size_t i = 0; i < d.tabs.size(); ++i) {
        if (static_cast<int>(i) == d.openTab || !d.tabVisible(d.tabs[i])) continue;
        drawTab(d.tabs[i]);
    }
    if (open) drawTab(*open);
    p.flush();

    // ---- storage background, slots, labels ----
    sb::storageBackground(p, {L, T, static_cast<float>(d.layout.imageWidth), static_cast<float>(d.layout.imageHeight)});
    p.flush();
    // Template control buttons (SC draws them above the background, beside the player inventory).
    sb::buttonBackground(p, {d.saveBtn.x, d.saveBtn.y, 18, 18}, d.saveHover);
    icon(p, Icon::SaveTemplate, d.saveBtn.x + 1, d.saveBtn.y + 1);
    sb::buttonBackground(p, {d.loadBtn.x, d.loadBtn.y, 18, 18}, d.loadHover);
    icon(p, Icon::LoadTemplate, d.loadBtn.x + 1, d.loadBtn.y + 1);
    sb::buttonBackground(p, {d.exportBtn.x, d.exportBtn.y, 18, 18}, d.exportHover);
    icon(p, Icon::ExportTemplate, d.exportBtn.x + 1, d.exportBtn.y + 1);
    int cols = d.layout.storageColumns;
    sb::slotsBackground(p, L + 7, T + 17, cols, d.visibleCount / cols, d.visibleCount % cols);
    for (int i = 0; i < 36; ++i) {
        auto const& pos = d.layout.playerSlots[static_cast<std::size_t>(i)];
        sb::slotFrame(p, L + static_cast<float>(pos.x), T + static_cast<float>(pos.y));
    }
    auto displaySel  = d.displaySlots(loadHovered);
    auto displayDye  = d.displayJson(loadHovered).value("color", 14);
    std::vector<int> templateNoSort;
    int              templateNoSortDye = 5;
    if (loadHovered) {
        auto ns           = d.preview().value("noSort", json::object());
        templateNoSort    = ns.value("slots", std::vector<int>{});
        templateNoSortDye = ns.value("color", 5);
    }
    bool showDisplayStripes = d.tabsItemDisplay;
    for (auto const& ps : d.placed) {
        auto  i = static_cast<std::size_t>(ps.index);
        float x = L + static_cast<float>(ps.pos.x), y = T + static_cast<float>(ps.pos.y);
        // renderSlot: the slot's stack, else the memory override (the template's while Load is hovered).
        ItemStack const* stack = nullptr;
        if (i < d.slotStacks.size() && !d.slotStacks[i].isNull()) stack = &d.slotStacks[i];
        else if (loadHovered && i < d.templateMemoryStacks.size() && !d.templateMemoryStacks[i].isNull()) stack = &d.templateMemoryStacks[i];
        else if (!loadHovered && d.memorySelected(ps.index)) stack = &d.memoryStacks[i];
        if (stack) {
            int rotation = d.displayRotation(ps.index, loadHovered);
            if (rotation != 0) drawItemTransformed(p, *stack, x + 8, y + 8, 1, 0, 0, static_cast<float>(rotation));
            else p.item(*stack, x, y);
        }
        bool memorized = loadHovered ? (i < d.templateMemoryStacks.size() && !d.templateMemoryStacks[i].isNull()) : d.memorySelected(ps.index);
        if (memorized) memoryOverlay(p, x, y);
        // renderSlotOverlays: one stripe per category colour (item display, no sort), alpha 0x50.
        std::vector<std::uint32_t> colors;
        if (showDisplayStripes && std::find(displaySel.begin(), displaySel.end(), ps.index) != displaySel.end()) {
            colors.push_back(core::kDyes[static_cast<std::size_t>(std::clamp(displayDye, 0, 15))].argb & 0xFFFFFF);
        }
        bool noSort = loadHovered ? std::find(templateNoSort.begin(), templateNoSort.end(), ps.index) != templateNoSort.end()
                                  : (i < m.noSort.size() && m.noSort[i]);
        if (noSort) {
            int dye = loadHovered ? templateNoSortDye : d.settings.value("noSortColor", 5);
            colors.push_back(core::kDyes[static_cast<std::size_t>(std::clamp(dye, 0, 15))].argb & 0xFFFFFF);
        }
        if (!colors.empty()) {
            float stripe = std::floor(16.f / static_cast<float>(colors.size()));
            for (std::size_t c = 0; c < colors.size(); ++c) {
                float yo = static_cast<float>(c) * stripe;
                float h  = c == colors.size() - 1 ? 16 - yo : stripe;
                p.fillRect({x, y + yo, 16, h}, Color::rgb(colors[c], 0x50 / 255.f), Painter::Layer::Foreground);
            }
        }
        if (d.hoverSlot == ps.index) p.fillRect({x, y, 16, 16}, sb::color::HOVER, Painter::Layer::Foreground);
        // renderExtra: the item display tab's selection frame.
        if (open && open->kind == TabKind::ItemDisplay && ps.index == d.currentDisplaySlot) slotSelection(p, x - 4, y - 4);
    }
    auto title = tr("abp.sbsettings.title", "Backpack Settings");
    float titleMax = std::max(0.f, static_cast<float>(d.layout.imageWidth) - 8 - 7);
    p.text(truncated(p, title, titleMax), L + 8, T + 6, Color::rgb(0x404040));
    p.text(ctr("abp.companion.screen.inventory", "Inventory"), L + static_cast<float>(d.layout.inventoryLabelX), T + static_cast<float>(d.layout.inventoryLabelY),
           Color::rgb(0x404040));
    if (d.layout.scroll) {
        auto bar   = d.layout.scrollBar();
        auto thumb = cs::scrollThumb(d.layout, m.storageSlots, d.scrollState);
        sb::scrollBar(p, {L + static_cast<float>(bar.x), T + static_cast<float>(bar.y), static_cast<float>(bar.w), static_cast<float>(bar.h)},
                      {L + static_cast<float>(thumb.x), T + static_cast<float>(thumb.y), static_cast<float>(thumb.w), static_cast<float>(thumb.h)});
    }
    p.flush();

    // ---- text boxes (TextBox z +100) ----
    if (d.saveBox.focused()) d.saveBox.render(p, t);
    if (d.exportBox.focused()) d.exportBox.render(p, t);
    p.flush();

    // ---- tooltips ----
    std::vector<TooltipLine> tip;
    if (open) open->widgets.tooltip(mx, my, tip);
    if (!tip.empty()) {
        drawTooltip(p, tip, mx, my);
        return;
    }
    for (std::size_t i = 0; i < d.tabs.size(); ++i) {
        auto const& tab = d.tabs[i];
        if (!d.tabVisible(tab)) continue;
        std::string base;
        std::string fallbackTitle, detail, openDetail;
        switch (tab.kind) {
        case TabKind::Back:
            if (!tab.rect.contains(mx, my) || !d.tabTooltipVisible(tab)) continue;
            drawTooltip(p, {{tr("abp.sbsettings.back_to_backpack.tooltip", "Back To Backpack"), kWhite}}, mx, my);
            return;
        case TabKind::Main:
            if (!tab.open && tab.rect.contains(mx, my) && d.tabTooltipVisible(tab)) {
                drawTooltip(p, {{tr("abp.sbsettings.backpack.tooltip", "Backpack Settings"), kWhite}}, mx, my);
                return;
            }
            continue;
        case TabKind::ItemDisplay:
            base          = "abp.sbsettings.item_display";
            fallbackTitle = "Item Display Settings";
            detail        = "Allows selecting a slot that will be used to show its item on top of storage";
            openDetail    = "Allows selecting a slot that will be used to show its item on top of storage\nSelect slot = left click/drag\nUnselect slot = right click/drag";
            break;
        case TabKind::NoSort:
            base          = "abp.sbsettings.no_sort";
            fallbackTitle = "No Sort Slot Settings";
            detail        = "Allows selecting slots that are ignored by sorting\nOpen tab to modify slot settings";
            openDetail    = "Allows selecting slots that are ignored by sorting\nSelect all / Unselect all = buttons\nSelect slot = left click/drag\nUnselect slot = right click/drag";
            break;
        case TabKind::Memory:
            base          = "abp.sbsettings.memory";
            fallbackTitle = "Slot Memory Settings";
            detail        = "Allows selecting slots that remember their contents and only allow matching stacks in them\nOpen tab to modify slot settings";
            openDetail    = "Allows selecting slots that remember their contents and only allow matching stacks in them\nSelect all / Unselect all = buttons\nSelect slot = left click/drag\nUnselect slot = right click/drag";
            break;
        }
        bool closedTip = !tab.open && tab.rect.contains(mx, my) && d.tabTooltipVisible(tab);
        bool openTip   = tab.open && mx > tab.rect.x && my > tab.rect.y + 3 && mx < tab.rect.x + 18 && my < tab.rect.y + 21;
        if (!closedTip && !openTip) continue;
        std::vector<TooltipLine> l{{ctr(base + ".tooltip", fallbackTitle), kWhite}};
        auto more = lines(ctr(base + (openTip ? ".tooltip_open_detail" : ".tooltip_detail"), openTip ? openDetail : detail), kGray);
        l.insert(l.end(), more.begin(), more.end());
        drawTooltip(p, l, mx, my);
        return;
    }
    if (d.saveHover) {
        drawTooltip(p, d.saveTooltip(), d.saveBtn.x + 10, d.saveBtn.y + (customSave ? -13 : 6));
        return;
    }
    if (d.loadHover) {
        drawTooltip(p, d.loadTooltip(), d.loadBtn.x + 10, d.loadBtn.y + 6);
        return;
    }
    if (d.exportHover) {
        drawTooltip(p, d.exportTooltip(), d.exportBtn.x + 10, d.exportBtn.y - 13);
        return;
    }
    // Truncated title.
    if (p.textWidth(title) > titleMax && Rect{L + 8, T + 6, titleMax, 9}.contains(mx, my)) {
        drawTooltip(p, {{title, kWhite}}, mx, my);
        return;
    }
    // Hovered slot: the stack in it (ghost memory items have no tooltip, like SC's view-only slots).
    if (d.hoverSlot >= 0 && static_cast<std::size_t>(d.hoverSlot) < d.slotStacks.size()) {
        auto const& s = d.slotStacks[static_cast<std::size_t>(d.hoverSlot)];
        if (!s.isNull() && !tooltip::drawBackpackTooltip(p, s, mx, my)) drawTooltip(p, itemTooltipLines(s), mx, my);
    }
}

bool SettingsScreen::mouseButton(core::screen::BackpackScreenModel const& m, MouseButton button, bool down, float x, float y) {
    auto& d = *mImpl;
    if (!down) {
        d.dragButton   = -1;
        d.dragLastSlot = -1;
        d.draggingBar  = false;
        d.previewDrag  = false;
        if (auto* t = d.openTabPtr()) t->widgets.mouseUp(button, x, y);
        return true;
    }
    int b = buttonIndex(button);
    // Text boxes take clicks inside them.
    if (d.saveBox.focused() && d.saveBox.rect.contains(x, y)) return d.saveBox.onMouseButton(button == MouseButton::Right, true, x, y), true;
    if (d.exportBox.focused() && d.exportBox.rect.contains(x, y)) return d.exportBox.onMouseButton(button == MouseButton::Right, true, x, y), true;
    auto* open = d.openTabPtr();
    if (open && open->widgets.mouseDown(button, x, y)) return true;
    // Tab icons (ImageButton 16x16 at +1,+4).
    for (std::size_t i = 0; i < d.tabs.size(); ++i) {
        auto& t = d.tabs[i];
        if (!d.tabVisible(t)) continue;
        Rect iconRect{t.rect.x + 1, t.rect.y + 4, 16, 16};
        if (!iconRect.contains(x, y)) continue;
        if (t.kind == TabKind::Back) {
            if (onBack) onBack();
            return true;
        }
        if (b != 0) return true; // SettingsTabBase.onTabIconClicked: left button only
        d.setOpen(static_cast<int>(i), !t.open);
        return true;
    }
    if (open && open->rect.contains(x, y)) {
        if (open->kind == TabKind::ItemDisplay && d.previewRect.contains(x, y) && b == 0) {
            d.previewDrag = true;
            d.lastMx      = x;
            d.lastMy      = y;
        }
        return true;
    }
    // Template persistence buttons (any mouse button).
    if (d.saveBtn.contains(x, y)) {
        d.save();
        return true;
    }
    if (d.loadBtn.contains(x, y)) {
        send({{"what", "template"}, {"op", "load"}});
        return true;
    }
    if (d.exportBtn.contains(x, y)) {
        d.exportTemplate();
        return true;
    }
    // Scroll bar.
    if (d.layout.scroll && b == 0) {
        auto bar = d.layout.scrollBar();
        Rect r{d.left + static_cast<float>(bar.x), d.top + static_cast<float>(bar.y), static_cast<float>(bar.w), static_cast<float>(bar.h)};
        if (r.contains(x, y)) {
            d.draggingBar = true;
            d.dragY       = y;
            return true;
        }
    }
    int slot = d.slotAt(x, y);
    if (slot >= 0 && (b == 0 || b == 1)) {
        d.dragButton   = b;
        d.dragLastSlot = slot;
        d.slotClick(slot, b);
    }
    (void)m;
    return true;
}

bool SettingsScreen::mouseMove(float x, float y) {
    auto& d = *mImpl;
    if (d.previewDrag) {
        // RotatablePreviewWidget.mouseDragged: y += 2*dx, x += 2*dy (mod 360).
        d.rotY    = std::fmod(d.rotY + 2 * (x - d.lastMx), 360.f);
        d.rotX    = std::fmod(d.rotX + 2 * (y - d.lastMy), 360.f);
        d.targetX = d.rotX;
        d.targetY = d.rotY;
        d.lastMx  = x;
        d.lastMy  = y;
        return true;
    }
    if (d.draggingBar) {
        if (auto const* m = net::screenModel()) cs::scrollDrag(d.scrollState, d.layout, m->storageSlots, y - d.dragY);
        d.dragY = y;
        return true;
    }
    if (d.dragButton >= 0) {
        // SC SettingsScreen.mouseDragged: dragging over slots selects / unselects them too.
        int slot = d.slotAt(x, y);
        auto* open = d.openTabPtr();
        bool  overTab = open && open->rect.contains(x, y);
        if (slot >= 0 && slot != d.dragLastSlot && !overTab) {
            d.dragLastSlot = slot;
            d.slotClick(slot, d.dragButton);
        }
        return true;
    }
    if (auto* t = d.openTabPtr()) t->widgets.mouseMove(x, y);
    return false;
}

bool SettingsScreen::scroll(float x, float y, float delta) {
    auto& d = *mImpl;
    if (d.saveBtn.contains(x, y)) {
        send({{"what", "template"}, {"op", "scrollSave"}, {"next", delta > 0}});
        return true;
    }
    if (d.loadBtn.contains(x, y)) {
        send({{"what", "template"}, {"op", "scrollLoad"}, {"next", delta > 0}});
        return true;
    }
    if (d.layout.scroll) {
        auto const& panel = d.layout.scrollPanel;
        Rect        r{d.left + static_cast<float>(panel.x), d.top + static_cast<float>(panel.y), static_cast<float>(panel.w), static_cast<float>(panel.h)};
        if (auto const* m = net::screenModel(); m && r.contains(x, y)) cs::scrollBy(d.scrollState, d.layout, m->storageSlots, delta > 0 ? 1 : -1);
    }
    return true;
}

bool SettingsScreen::key(int vk, bool down) {
    auto& d = *mImpl;
    for (auto* box : {&d.saveBox, &d.exportBox}) {
        if (!box->focused()) continue;
        if (vk == VK_RETURN_) {
            if (down) {
                // SC TextBox.onEnterPressed: save / export with the typed name, then clear.
                if (box == &d.saveBox) d.save();
                else d.exportTemplate();
            }
            return true;
        }
        if (vk == VK_ESCAPE_) return false; // ESC still leaves the settings screen
        return box->onKey(vk, down);
    }
    return false;
}

bool SettingsScreen::chr(std::string const& utf8) {
    auto& d = *mImpl;
    if (d.saveBox.focused()) return d.saveBox.onChar(utf8);
    if (d.exportBox.focused()) return d.exportBox.onChar(utf8);
    return false;
}

} // namespace abp::client::ui::settings

#endif // ABP_CLIENT
