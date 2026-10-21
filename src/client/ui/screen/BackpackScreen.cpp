#ifdef ABP_CLIENT

#include "client/ui/screen/BackpackScreen.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <optional>
#include <unordered_map>
#include <string>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/client/ClientExitLevelEvent.h"
#include "ll/api/event/render/UIRenderEvent.h"
#include "ll/api/memory/Hook.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/gui/GuiData.h"
#include "mc/client/gui/screens/ScreenController.h"
#include "mc/client/gui/screens/ScreenView.h"
#include "mc/client/gui/screens/AbstractScene.h"
#include "mc/client/gui/screens/SceneFactory.h"
#include "mc/client/gui/screens/UIScene.h"
#include "mc/client/gui/screens/interfaces/ISceneStack.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/renderer/screen/MinecraftUIRenderContext.h"
#include "mc/world/PlayerUIContainer.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/item/ItemStack.h"

#include "client/Client.h"
#include "client/input/InputRouter.h"
#include "client/input/Keys.h"
#include "client/net/ClientNet.h"
#include "client/render/RenderHooks.h"
#include "client/tooltip/ContentsTooltip.h"
#include "client/ui/UiRoot.h"
#include "client/ui/screen/Tabs.h"
#include "client/ui/widgets/SbStyle.h"
#include "client/ui/widgets/TextInput.h"
#include "client/ui/widgets/Widgets.h"
#include "core/screen/CountFormat.h"
#include "core/screen/SearchFilter.h"
#include "core/screen/StorageLayout.h"
#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace abp::client::ui {

namespace {

namespace cs = core::screen;
using cs::geometry::kTextColor;

constexpr int VK_ESCAPE_ = 0x1B;

double now() {
    static auto const start = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

float easeInOutCubic(float t) { return t < 0.5f ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3.f) / 2; }

struct SlotRef {
    enum class Area { None, Storage, Inventory, Upgrade, Menu } area = Area::None;
    int  index = -1;
    [[nodiscard]] bool operator==(SlotRef const&) const = default;
};

char const* areaName(SlotRef::Area a) {
    switch (a) {
    case SlotRef::Area::Storage:
        return "storage";
    case SlotRef::Area::Inventory:
        return "inventory";
    case SlotRef::Area::Upgrade:
        return "upgrade";
    case SlotRef::Area::Menu:
        return "menu";
    default:
        return "";
    }
}

/// "Name..." fitting into `maxWidth` (SC Label.getTruncatedText).
std::string truncated(Painter& p, std::string const& text, float maxWidth) {
    if (p.textWidth(text) <= maxWidth) return text;
    std::string out = text;
    while (!out.empty() && p.textWidth(out + "...") > maxWidth) {
        // drop one UTF-8 code point
        std::size_t i = out.size() - 1;
        while (i > 0 && (static_cast<unsigned char>(out[i]) & 0xC0) == 0x80) --i;
        out.erase(i);
    }
    return out + "...";
}

/// SB count overlay: vanilla style up to 99, abbreviated and scaled to 16 units above.
void drawCount(Painter& p, long long count, float x, float y, Color c = Color::rgb(0xFFFFFF)) {
    if (count <= 1) return;
    if (count <= 99) {
        auto s = std::to_string(count);
        p.text(s, x + 19 - 2 - p.textWidth(s), y + 6 + 3, c, true);
        return;
    }
    auto  s     = cs::abbreviateCount(count);
    float w     = p.textWidth(s);
    float scale = std::min(1.f, 16.f / std::max(1.f, w));
    p.text(s, x + 19 - 2 - w * scale, y + 6 + 3 + (1 / (scale * scale) - 1), c, true, scale);
}

void requestClose(); // host: close the screen now (and tell the server)

bool isFillerItem(core::ItemStackData const& d) { return d.id == cs::kFillerId; }

class BackpackScreenLayer final : public IUiLayer {
public:
    // ---- host ---------------------------------------------------------------------------------
    bool rendering = false; ///< set by the host while it draws the screen scene

    [[nodiscard]] char const* name() const override { return "abp.backpack_screen"; }
    [[nodiscard]] int         zOrder() const override { return 1000; }
    [[nodiscard]] bool        visible(FrameInfo const&) const override { return rendering && net::displayModel(); }
    [[nodiscard]] bool        contains(float, float) const override { return true; }
    [[nodiscard]] bool        wantsTextInput() const override { return mSearch.focused() || openTabWantsText(); }

    void resetFocus() {
        mSearch.setFocused(false);
        mDraggingBar = false;
        mDragButton  = -1;
    }

    [[nodiscard]] bool capturesKey(int vk) const override {
        auto const* m = net::screenModel();
        if (!m) return false;
        if (vk == VK_ESCAPE_) return m->nested || mView == View::Settings;
        if (vk == 'E') return true; // the inventory key closes container screens
        if (m->kind == "menu") return vk == 'Q' || (vk >= '1' && vk <= '9');
        using input::KeyAction;
        if (input::keyMatches(KeyAction::OpenBackpack, vk) || input::keyMatches(KeyAction::Sort, vk)
            || input::keyMatches(KeyAction::TransferToStorage, vk) || input::keyMatches(KeyAction::TransferToInventory, vk)) {
            return true;
        }
        return vk == 'Q' || (vk >= '1' && vk <= '9');
    }

    void render(Painter& p, FrameInfo const& f) override;
    bool onMouseButton(MouseButton button, bool down, float x, float y) override;
    bool onMouseMove(float x, float y) override;
    bool onScroll(float x, float y, float delta) override;
    bool onKey(int vk, bool down) override;
    bool onChar(std::string const& utf8) override;

private:
    // ---- model caches ---------------------------------------------------------------------------
    void syncModel(cs::BackpackScreenModel const& m);
    void rebuildTabs(cs::BackpackScreenModel const& m);
    void updateSearch(cs::BackpackScreenModel const& m);
    void layout(cs::BackpackScreenModel const& m, FrameInfo const& f);
    [[nodiscard]] bool openTabWantsText() const;

    // ---- drawing ---------------------------------------------------------------------------------
    void drawTabs(Painter& p, cs::BackpackScreenModel const& m, float mx, float my, double t);
    void drawSlots(Painter& p, cs::BackpackScreenModel const& m, float mx, float my);
    void drawSearch(Painter& p, double t);
    void drawErrorOverlay(Painter& p, cs::BackpackScreenModel const& m);
    void drawTooltips(Painter& p, cs::BackpackScreenModel const& m, float mx, float my);
    void buildControls(cs::BackpackScreenModel const& m);

    // ---- input ------------------------------------------------------------------------------------
    [[nodiscard]] SlotRef slotAt(cs::BackpackScreenModel const& m, float x, float y) const;
    [[nodiscard]] Rect    slotRect(SlotRef const& s) const;
    void                  send(nlohmann::json body) const { net::sendScreenAction(std::move(body)); }
    void                  openBackpackKey(cs::BackpackScreenModel const& m);
    [[nodiscard]] ItemStack const* stackAt(SlotRef const& s) const;

    // model caches
    std::uint32_t                  mSession  = 0;
    std::uint64_t                  mRevision = ~std::uint64_t{0};
    std::vector<ItemStack>         mSlotStacks;
    std::vector<ItemStack>         mMemoryStacks;
    std::vector<ItemStack>         mUpgradeStacks;
    std::vector<cs::SearchItem>    mSearchItems; // lazily filled per slot
    std::vector<bool>              mSearchReady;

    // geometry (absolute GUI units)
    cs::StorageLayout             mLayout;
    float                         mLeft = 0, mTop = 0;
    std::vector<cs::PlacedSlot>   mPlaced;
    int                           mVisibleCount = 0;
    cs::ScrollState               mScroll;
    bool                          mDraggingBar = false;
    float                         mDragY       = 0;

    // search
    TextBox            mSearch;
    double             mSearchFocusAt = -10;
    bool               mSearchWasFocused = false;
    std::string        mPhrase;
    cs::SearchFilter   mFilter;
    std::vector<bool>  mMatches;
    bool               mMatchesDirty = true;

    // tabs
    struct TabState {
        int                        slot = -1; ///< -1 = backpack settings tab
        std::unique_ptr<NativeTab> native;
        WidgetGroup                widgets;
        Rect                       rect;
        NativeTab::Size            openSize;
    };
    std::vector<TabState> mTabs;
    int                   mOpenTab = -1;
    std::string           mTabsSig;

    // controls (rebuilt every frame from the model)
    WidgetGroup mControls;

    std::uint32_t mLoggedLayout = 0;

    // hover / clicks
    SlotRef mHover;
    SlotRef mLastClick;
    double  mLastClickAt = -10;

    // ---- generic menus ("menu" kind) ---------------------------------------------------------------
    std::vector<ItemStack> mMenuStacks;
    void renderMenu(Painter& p, cs::BackpackScreenModel const& m, FrameInfo const& f);
    bool menuMouse(cs::BackpackScreenModel const& m, MouseButton button, float x, float y);

    // ---- settings screen (SC SettingsScreen) ------------------------------------------------------
    enum class View { Storage, Settings };
    enum SettingsTab { kBackTab = -2, kMainTab = -3, kNoSortTab = -4, kMemoryTab = -5 };
    View                  mView = View::Storage;
    std::vector<TabState> mSettingsTabs;
    int                   mSettingsOpen = -1;
    int                   mDragButton   = -1; ///< mouse button held for drag-selecting slots
    int                   mDragLastSlot = -1;
    void showSettings(bool on);
    void buildSettingsTab(cs::BackpackScreenModel const& m, TabState& tab);
    void renderSettings(Painter& p, cs::BackpackScreenModel const& m, FrameInfo const& f);
    bool settingsMouse(cs::BackpackScreenModel const& m, MouseButton button, bool down, float x, float y);
    void settingsSlotClick(int index, int button);
    [[nodiscard]] nlohmann::json settingsValues(cs::BackpackScreenModel const& m) const {
        auto it = m.extra.find("settings");
        return it != m.extra.end() && it->is_object() ? *it : nlohmann::json::object();
    }

    // SC "keep search phrase" / "keep tab open" (per backpack, this game session)
    std::unordered_map<std::string, std::string> mKeptSearch;
    std::unordered_map<std::string, int>         mKeptTab;
    int                                          mPendingTab = -1;

    // vanilla quick craft (drag the cursor stack over slots)
    int                  mDistButton = -1;
    std::vector<SlotRef> mDistSlots;
    void                 finishDistribution();
};

BackpackScreenLayer gLayer;
ABP_REGISTER_UI_LAYER(gLayer);

// ---- model ------------------------------------------------------------------------------------------

void BackpackScreenLayer::syncModel(cs::BackpackScreenModel const& m) {
    if (m.session != mSession) {
        // A new screen: SC starts with an empty search box and the scroll panel at the top
        // (unless "keep search phrase" / "keep tab open" are on for this backpack).
        mSession = m.session;
        mScroll  = {};
        mSearch.setText({});
        mSearch.setFocused(false);
        mPhrase.clear();
        mFilter       = {};
        mOpenTab      = -1;
        mTabsSig.clear();
        mMatchesDirty = true;
        mView         = View::Storage;
        mSettingsTabs.clear();
        mSettingsOpen = -1;
        mDragButton   = -1;
        mSearch.bordered       = false;
        mSearch.padding        = 1;
        mSearch.backgroundArgb = 0x00000000;
        mSearch.maxLength      = 50;
        mSearch.onChange       = [this](std::string const& text) {
            mMatchesDirty = true;
            if (auto const* cur = net::screenModel(); cur && !cur->uuid.empty()) mKeptSearch[cur->uuid] = text;
        };
        auto settings = settingsValues(m);
        if (!m.uuid.empty() && settings.value("keepSearchPhrase", false)) {
            if (auto it = mKeptSearch.find(m.uuid); it != mKeptSearch.end()) mSearch.setText(it->second);
        }
        mPendingTab = -1;
        if (!m.uuid.empty() && settings.value("keepTabOpen", true)) {
            if (auto it = mKeptTab.find(m.uuid); it != mKeptTab.end()) mPendingTab = it->second;
        }
    }
    auto rev = net::screenRevision();
    if (rev == mRevision) return;
    mRevision = rev;
    mSlotStacks.clear();
    for (auto const& d : m.slots) mSlotStacks.push_back(d.empty() ? ItemStack{} : game::ItemConv::toStack(d, 1));
    mMemoryStacks.clear();
    for (auto const& d : m.memory) mMemoryStacks.push_back(d.empty() ? ItemStack{} : game::ItemConv::toStack(d, 1));
    mUpgradeStacks.clear();
    for (auto const& u : m.upgrades) mUpgradeStacks.push_back(u.item.empty() ? ItemStack{} : game::ItemConv::toStack(u.item, 1));
    mMenuStacks.clear();
    for (auto const& d : m.menuSlots) mMenuStacks.push_back(d.empty() || isFillerItem(d) ? ItemStack{} : game::ItemConv::toStack(d, 1));
    mSearchItems.assign(m.slots.size(), {});
    mSearchReady.assign(m.slots.size(), false);
    mMatchesDirty = true;
    rebuildTabs(m);
    for (auto& t : mTabs) {
        if (!t.native || t.slot < 0 || t.slot >= static_cast<int>(m.upgrades.size())) continue;
        TabContext ctx{m, t.slot, m.upgrades[static_cast<std::size_t>(t.slot)], &mUpgradeStacks[static_cast<std::size_t>(t.slot)], {}, {}};
        ctx.sendTabAction    = [slot = t.slot](nlohmann::json d) { net::sendScreenAction({{"action", "tab"}, {"slot", slot}, {"data", std::move(d)}}); };
        ctx.sendScreenAction = [](nlohmann::json b) { net::sendScreenAction(std::move(b)); };
        t.native->update(ctx);
    }
}

void BackpackScreenLayer::rebuildTabs(cs::BackpackScreenModel const& m) {
    std::string sig;
    for (std::size_t i = 0; i < m.upgrades.size(); ++i) {
        auto const& u = m.upgrades[i];
        if (u.hasSettings || (!u.id.empty() && hasNativeTab(u.id))) sig += std::to_string(i) + ":" + u.id + ";";
    }
    if (sig == mTabsSig && !mTabs.empty()) return;
    mTabsSig = sig;
    mTabs.clear();
    mOpenTab = -1;
    mTabs.push_back(TabState{}); // backpack settings
    for (std::size_t i = 0; i < m.upgrades.size(); ++i) {
        auto const& u = m.upgrades[i];
        if (!(u.hasSettings || (!u.id.empty() && hasNativeTab(u.id)))) continue;
        TabState t;
        t.slot   = static_cast<int>(i);
        t.native = createNativeTab(u.id);
        mTabs.push_back(std::move(t));
    }
}

void BackpackScreenLayer::updateSearch(cs::BackpackScreenModel const& m) {
    if (mSearch.text() != mPhrase) {
        mPhrase = mSearch.text();
        mFilter = cs::SearchFilter(mPhrase);
        mScroll = {}; // SC resets the scroll distance on a new phrase
        mMatchesDirty = true;
    }
    if (!mMatchesDirty) return;
    mMatchesDirty = false;
    mMatches.clear();
    if (mFilter.blank()) return;
    bool needTooltip = mFilter.needsTooltip();
    mMatches.resize(m.slots.size(), false);
    for (std::size_t i = 0; i < m.slots.size(); ++i) {
        auto& item = mSearchItems[i];
        if (!mSearchReady[i] || (needTooltip && item.tooltipLines.empty() && !item.empty)) {
            item          = {};
            item.empty    = m.slots[i].empty();
            if (!item.empty) {
                auto const& stack  = mSlotStacks[i];
                item.hoverName     = stack.getHoverName();
                item.itemNamespace = std::string(m.slots[i].itemNamespace());
                if (needTooltip) {
                    for (auto& l : itemTooltipLines(stack)) item.tooltipLines.push_back(std::move(l.text));
                }
            }
            mSearchReady[i] = true;
        }
        mMatches[i] = mFilter.matches(item);
    }
}

bool BackpackScreenLayer::openTabWantsText() const {
    return mOpenTab >= 0 && mOpenTab < static_cast<int>(mTabs.size()) && mTabs[static_cast<std::size_t>(mOpenTab)].widgets.wantsText();
}

// ---- layout -----------------------------------------------------------------------------------------

void BackpackScreenLayer::layout(cs::BackpackScreenModel const& m, FrameInfo const& f) {
    cs::LayoutInput in;
    in.storageSlots = m.storageSlots;
    in.upgradeSlots = static_cast<int>(m.upgrades.size());
    in.screenHeight = static_cast<int>(f.screen.h);
    for (auto const& u : m.upgrades) in.upgradeSwitches.push_back(!u.item.empty() && u.toggleable);
    mLayout = cs::computeLayout(in);
    mLeft   = std::floor((f.screen.w - static_cast<float>(mLayout.imageWidth)) / 2);
    mTop    = std::floor((f.screen.h - static_cast<float>(mLayout.imageHeight)) / 2);
    // Keep the scroll position valid after resizes / model changes.
    cs::scrollBy(mScroll, mLayout, m.storageSlots, 0);
    mPlaced = cs::placeStorageSlots(mLayout, m.storageSlots, mMatches, mScroll, &mVisibleCount);
    if (m.session != mLoggedLayout) {
        mLoggedLayout = m.session;
        float inv     = 1;
        if (auto* c = currentClient()) inv = c->getGuiData()->mInvGuiScale;
        log().info(
            "companion: screen layout session={} left={} top={} width={} height={} rows={}/{} scroll={} invGuiScale={} screen={}x{} "
            "invLabel={},{} upgrades={} tabs={}",
            m.session, mLeft, mTop, mLayout.imageWidth, mLayout.imageHeight, mLayout.visibleRows, mLayout.rows, mLayout.scroll, inv,
            f.screen.w, f.screen.h, mLayout.inventoryLabelX, mLayout.inventoryLabelY, m.upgrades.size(), mTabs.size()
        );
    }

    // Tabs: closed tabs are 21x24, 25 apart from (imageWidth, 4); the open one grows.
    float tx = mLeft + static_cast<float>(mLayout.tabsOrigin.x);
    float ty = mTop + static_cast<float>(mLayout.tabsOrigin.y);
    for (std::size_t i = 0; i < mTabs.size(); ++i) {
        auto& t = mTabs[i];
        t.rect  = {tx, ty + static_cast<float>(i) * (cs::geometry::kTabHeight + cs::geometry::kTabSpacing), cs::geometry::kTabWidth, cs::geometry::kTabHeight};
        if (static_cast<int>(i) == mOpenTab && t.openSize.w > 0) {
            t.rect.w = std::max<float>(t.openSize.w, cs::geometry::kTabWidth);
            t.rect.h = t.openSize.h;
        }
    }
}

// ---- controls -----------------------------------------------------------------------------------------

void BackpackScreenLayer::buildControls(cs::BackpackScreenModel const& m) {
    mControls.widgets.clear();
    auto R = [&](cs::Rect const& r) { return Rect{mLeft + static_cast<float>(r.x), mTop + static_cast<float>(r.y), static_cast<float>(r.w), static_cast<float>(r.h)}; };

    // Transfer buttons (shift: everything; else only items already on the other side).
    auto addTransfer = [&](cs::Rect const& r, bool toStorage) {
        auto b  = std::make_unique<Button>();
        b->size = Button::Size::Small;
        b->rect = R(r);
        b->icon = [toStorage](Painter& p, float x, float y) {
            bool shift = input::shiftDown();
            sb::Icon i = toStorage ? (shift ? sb::Icon::TransferToStorage : sb::Icon::TransferToStorageFiltered)
                                   : (shift ? sb::Icon::TransferToInventory : sb::Icon::TransferToInventoryFiltered);
            sb::icon(p, i, x, y);
        };
        b->dynamicTooltip = [toStorage]() -> std::vector<TooltipLine> {
            if (input::shiftDown()) {
                return {{toStorage ? ctr("abp.companion.button.transfer_to_storage", "Transfer to Storage")
                                   : ctr("abp.companion.button.transfer_to_inventory", "Transfer to Inventory"),
                         0xFFFFFF}};
            }
            return {
                {toStorage ? ctr("abp.companion.button.transfer_to_storage_filtered", "Transfer Matching to Storage")
                           : ctr("abp.companion.button.transfer_to_inventory_filtered", "Transfer Matching to Inventory"),
                 0xFFFFFF},
                {ctr("abp.companion.button.transfer_all", "Shift To Transfer All"), 0x555555},
            };
        };
        b->onClick = [toStorage](int button) {
            if (button != 0) return;
            net::sendScreenAction({{"action", "transfer"}, {"to", toStorage ? "storage" : "inventory"}, {"matching", !input::shiftDown()}});
        };
        mControls.add(std::move(b));
    };
    addTransfer(mLayout.transferToStorage, true);
    addTransfer(mLayout.transferToInventory, false);

    if (mLayout.sortButton.w > 0) {
        auto sort          = std::make_unique<Button>();
        sort->size         = Button::Size::Small;
        sort->rect         = R(mLayout.sortButton);
        sort->icon         = [](Painter& p, float x, float y) { sb::icon(p, sb::Icon::TransferToStorage, x, y); }; // SC's SORT icon
        sort->tooltipLines = {{ctr("abp.companion.button.sort_action", "Sort Inventory"), 0xFFFFFF}};
        sort->onClick      = [](int button) {
            if (button == 0) net::sendScreenAction({{"action", "sort"}});
        };
        mControls.add(std::move(sort));

        auto by     = std::make_unique<ToggleButton>();
        by->size    = Button::Size::Small;
        by->rect    = R(mLayout.sortByButton);
        auto letter = [](sb::Icon i) { return [i](Painter& p, float x, float y) { sb::icon(p, i, x, y); }; };
        by->states  = {
            {letter(sb::Icon::SortByName), {{ctr("abp.companion.button.sort_by_name", "By Name"), 0xFFFFFF}}},
            {letter(sb::Icon::SortByCount), {{ctr("abp.companion.button.sort_by_count", "By Count"), 0xFFFFFF}}},
            {letter(sb::Icon::SortByMod), {{ctr("abp.companion.button.sort_by_mod", "By Mod"), 0xFFFFFF}}},
            {letter(sb::Icon::SortByTags), {{ctr("abp.companion.button.sort_by_tags", "By Tags"), 0xFFFFFF}}},
        };
        auto sortBy = m.sortBy;
        by->current = [sortBy] {
            if (sortBy == "count") return 1;
            if (sortBy == "mod") return 2;
            if (sortBy == "tag") return 3;
            return 0;
        };
        by->onClick = [](int button) {
            if (button == 0) net::sendScreenAction({{"action", "sortBy"}});
        };
        mControls.add(std::move(by));
    }

    // Upgrade switches.
    for (std::size_t i = 0; i < m.upgrades.size() && i < mLayout.upgradeSwitches.size(); ++i) {
        auto const& r = mLayout.upgradeSwitches[i];
        if (r.w <= 0) continue;
        auto const& u    = m.upgrades[i];
        auto        sw   = std::make_unique<UpgradeSwitch>();
        sw->rect         = R(r);
        int  state       = !u.runnable ? -1 : (u.enabled ? 1 : 0);
        sw->state        = [state] { return state; };
        int  slot        = static_cast<int>(i);
        sw->onToggle     = [slot] { net::sendScreenAction({{"action", "toggleUpgrade"}, {"slot", slot}}); };
        sw->onTooltip    = {{"§a" + ctr("abp.companion.button.upgrade_switch_enabled", "ON"), 0xFFFFFF}};
        sw->offTooltip   = {{"§c" + ctr("abp.companion.button.upgrade_switch_disabled", "OFF"), 0xFFFFFF}};
        sw->inactiveTooltip = {{"§7" + ctr("abp.companion.button.upgrade_switch_inactive", "INACTIVE"), 0xFFFFFF}};
        mControls.add(std::move(sw));
    }
}

// ---- drawing ------------------------------------------------------------------------------------------------

void BackpackScreenLayer::drawTabs(Painter& p, cs::BackpackScreenModel const& m, float mx, float my, double t) {
    auto const* open = mOpenTab >= 0 && mOpenTab < static_cast<int>(mTabs.size()) ? &mTabs[static_cast<std::size_t>(mOpenTab)] : nullptr;
    auto covered     = [&](TabState const& tab) {
        // SettingsTabControl: a tab is not drawn while the open tab fully covers it.
        if (!open || &tab == open) return false;
        return !(open->rect.bottom() < tab.rect.y || open->rect.y > tab.rect.y);
    };
    auto drawOne = [&](TabState& tab, bool isOpen) {
        sb::tabBackground(p, tab.rect);
        float ix = tab.rect.x + 1, iy = tab.rect.y + 4;
        if (tab.slot < 0) {
            sb::icon(p, sb::Icon::Settings, ix, iy);
        } else if (tab.slot < static_cast<int>(mUpgradeStacks.size()) && !mUpgradeStacks[static_cast<std::size_t>(tab.slot)].isNull()) {
            p.item(mUpgradeStacks[static_cast<std::size_t>(tab.slot)], ix, iy);
        }
        if (!isOpen || tab.slot < 0) return;
        auto const& u     = m.upgrades[static_cast<std::size_t>(tab.slot)];
        float       maxW  = std::max(0.f, tab.rect.w - 20 - 6);
        p.text(truncated(p, u.name, maxW), tab.rect.x + 20, tab.rect.y + 8, Color::rgb(kTextColor));
        tab.widgets.render(p, mx, my, t);
        if (tab.native) {
            TabContext ctx{m, tab.slot, u, &mUpgradeStacks[static_cast<std::size_t>(tab.slot)], {}, {}};
            tab.native->render(p, ctx, tab.rect, mx, my);
        }
    };
    for (std::size_t i = 0; i < mTabs.size(); ++i) {
        if (static_cast<int>(i) == mOpenTab || covered(mTabs[i])) continue;
        drawOne(mTabs[i], false);
    }
    if (open) drawOne(mTabs[static_cast<std::size_t>(mOpenTab)], true);
}

void BackpackScreenLayer::drawSlots(Painter& p, cs::BackpackScreenModel const& m, float mx, float my) {
    auto const L = mLeft, T = mTop;
    // Slot backgrounds: full rows of the visible slots plus the rest (SC renderSlotsBackground).
    int cols = mLayout.storageColumns;
    sb::slotsBackground(p, L + 7, T + 17, cols, mVisibleCount / cols, mVisibleCount % cols);

    // No-sort stripes (drawSlotOverlays): slot colour with alpha 0x50 over the 16x16 area.
    for (auto const& ps : mPlaced) {
        if (static_cast<std::size_t>(ps.index) < m.noSort.size() && m.noSort[static_cast<std::size_t>(ps.index)]) {
            p.fillRect({L + ps.pos.x, T + ps.pos.y, 16, 16}, Color::rgb(m.noSortColor, 0x50 / 255.f));
        }
    }

    // Upgrade column (drawn over the storage background's left border).
    std::vector<bool> switches;
    for (auto const& r : mLayout.upgradeSwitches) switches.push_back(r.w > 0);
    sb::upgradeColumn(p, L - cs::geometry::kUpgradeInventoryOffset, T, static_cast<int>(m.upgrades.size()), switches);

    // Labels.
    auto title = truncated(p, m.title, static_cast<float>(mLayout.titleMaxWidth()));
    p.text(title, L + cs::geometry::kTitleX, T + cs::geometry::kTitleY, Color::rgb(kTextColor));
    p.text(
        ctr("abp.companion.screen.inventory", "Inventory"),
        L + static_cast<float>(mLayout.inventoryLabelX),
        T + static_cast<float>(mLayout.inventoryLabelY),
        Color::rgb(kTextColor)
    );

    auto highlight = [&](float x, float y) { p.fillRect({x, y, 16, 16}, sb::color::HOVER, Painter::Layer::Foreground); };

    // Upgrade slots.
    for (std::size_t i = 0; i < mLayout.upgradeSlots.size(); ++i) {
        float x = L + static_cast<float>(mLayout.upgradeSlots[i].x), y = T + static_cast<float>(mLayout.upgradeSlots[i].y);
        if (i < mUpgradeStacks.size() && !mUpgradeStacks[i].isNull()) p.item(mUpgradeStacks[i], x, y);
        if (mHover.area == SlotRef::Area::Upgrade && mHover.index == static_cast<int>(i)) highlight(x, y);
    }

    // Storage slots.
    for (auto const& ps : mPlaced) {
        float x = L + static_cast<float>(ps.pos.x), y = T + static_cast<float>(ps.pos.y);
        auto  i = static_cast<std::size_t>(ps.index);
        auto const& d = m.slots[i];
        if (d.empty()) {
            // Memory slot: the remembered item faded (SC renderSlotBackground + drawStackOverlay).
            if (i < mMemoryStacks.size() && !mMemoryStacks[i].isNull()) {
                p.item(mMemoryStacks[i], x, y);
                sb::icon(p, sb::Icon::Memory, x, y);
            }
        } else {
            p.item(mSlotStacks[i], x, y);
            render::decorateItemIcon(p, mSlotStacks[i], x, y);
            drawCount(p, d.count, x, y);
        }
        if (mHover.area == SlotRef::Area::Storage && mHover.index == ps.index) highlight(x, y);
        else if (mDistSlots.size() > 1 && std::find(mDistSlots.begin(), mDistSlots.end(), SlotRef{SlotRef::Area::Storage, ps.index}) != mDistSlots.end()) highlight(x, y);
    }

    // Player inventory.
    if (auto* player = currentPlayer()) {
        auto& inv = player->getInventory();
        for (int i = 0; i < 36; ++i) {
            auto const& pos = mLayout.playerSlots[static_cast<std::size_t>(i)];
            float       x = L + static_cast<float>(pos.x), y = T + static_cast<float>(pos.y);
            sb::slotFrame(p, x, y); // part of SC's storage background texture
            auto const& s = inv.getItem(i);
            if (!s.isNull()) {
                p.item(s, x, y);
                render::decorateItemIcon(p, s, x, y);
                drawCount(p, s.mCount, x, y);
            }
            if (mHover.area == SlotRef::Area::Inventory && mHover.index == i) highlight(x, y);
        }
    }

    // Scroll bar (InventoryScrollPanel).
    if (mLayout.scroll) {
        auto bar   = mLayout.scrollBar();
        auto thumb = cs::scrollThumb(mLayout, m.storageSlots, mScroll);
        sb::scrollBar(
            p,
            {L + static_cast<float>(bar.x), T + static_cast<float>(bar.y), static_cast<float>(bar.w), static_cast<float>(bar.h)},
            {L + static_cast<float>(thumb.x), T + static_cast<float>(thumb.y), static_cast<float>(thumb.w), static_cast<float>(thumb.h)}
        );
    }
    (void)mx;
    (void)my;
}

void BackpackScreenLayer::drawSearch(Painter& p, double t) {
    auto const& max = mLayout.searchBox;
    float       maxX = mLeft + static_cast<float>(max.x), maxW = static_cast<float>(max.w);
    float       minW = cs::geometry::kSearchMinWidth;
    bool        focused = mSearch.focused();
    if (focused != mSearchWasFocused) {
        mSearchWasFocused = focused;
        mSearchFocusAt    = t;
    }
    float ratio = easeInOutCubic(static_cast<float>(std::min((t - mSearchFocusAt) / 0.2, 1.0)));
    float width = maxW;
    if (focused) width = minW + (maxW - minW) * ratio;
    else if (mSearch.text().empty()) width = maxW - (maxW - minW) * ratio;
    Rect r{maxX + maxW - width, mTop + static_cast<float>(max.y), std::floor(width), static_cast<float>(max.h)};
    p.flush(); // above the title (SC draws the box at z+100)
    p.fillRect(r, sb::color::SEARCH_BG);
    mSearch.rect      = r;
    mSearch.textColor = focused ? 0xFFFFFF : 0xBBBBBB;
    mSearch.render(p, t);
    if (mSearch.text().empty() && !focused) sb::icon(p, sb::Icon::Search, r.x + r.w / 2 + 2 - 3.5f, r.y + 1.5f);
    if (!mFilter.blank() && mVisibleCount == 0) {
        p.text(ctr("abp.companion.screen.no_results", "No items match the search"), mLeft + 7, mTop + 18, Color::rgb(kTextColor));
    }
}

void BackpackScreenLayer::drawErrorOverlay(Painter& p, cs::BackpackScreenModel const& m) {
    if (m.errorMessage.empty()) return;
    p.flush();
    for (int i : m.errorUpgradeSlots) {
        if (i < 0 || i >= static_cast<int>(mLayout.upgradeSlots.size())) continue;
        p.fillRect(slotRect({SlotRef::Area::Upgrade, i}).inset(1), sb::color::ERROR_SLOT, Painter::Layer::Foreground);
    }
    for (int i : m.errorStorageSlots) {
        for (auto const& ps : mPlaced) {
            if (ps.index == i) p.fillRect({mLeft + ps.pos.x, mTop + ps.pos.y, 16, 16}, sb::color::ERROR_SLOT, Painter::Layer::Foreground);
        }
    }
    // Message box centred under the storage, at the inventory label (SC renderErrorMessage).
    float w  = std::min(260.f, p.textWidth(m.errorMessage));
    float cx = p.screen().w / 2;
    float y  = mTop + static_cast<float>(mLayout.inventoryLabelY) + 4;
    Rect  body{std::floor(cx - w / 2), y, w, 8};
    sb::tooltipBox(p, body, sb::color::ERROR_BG, sb::color::ERROR_EDGE, sb::color::ERROR_EDGE);
    p.text(m.errorMessage, body.x, body.y, sb::color::ERROR_EDGE, true);
    p.flush();
}

void BackpackScreenLayer::drawTooltips(Painter& p, cs::BackpackScreenModel const& m, float mx, float my) {
    std::vector<TooltipLine> lines;
    // Open tab widgets, controls.
    if (mOpenTab >= 0 && mOpenTab < static_cast<int>(mTabs.size())) mTabs[static_cast<std::size_t>(mOpenTab)].widgets.tooltip(mx, my, lines);
    mControls.tooltip(mx, my, lines);
    if (!lines.empty()) {
        drawTooltip(p, lines, mx, my);
        return;
    }
    // Closed tabs (SC Tab.renderTooltip): settings tab text / upgrade name.
    for (std::size_t i = 0; i < mTabs.size(); ++i) {
        auto const& t = mTabs[i];
        if (static_cast<int>(i) == mOpenTab || !t.rect.contains(mx, my)) continue;
        if (t.slot < 0) lines.push_back({ctr("abp.companion.tab.settings", "Backpack Settings"), 0xFFFFFF});
        else lines.push_back({m.upgrades[static_cast<std::size_t>(t.slot)].name, 0xFFFFFF});
        drawTooltip(p, lines, mx, my);
        return;
    }
    // Search box hint while not focused.
    if (!mSearch.focused() && mSearch.rect.contains(mx, my)) {
        drawTooltip(
            p,
            {{ctr("abp.companion.search.title", "Click to search"), 0xFFFFFF},
             {ctr("abp.companion.search.detail", "@ prefix to search by mod name"), 0xAAAAAA}},
            mx,
            my
        );
        return;
    }
    // Truncated title.
    if (!mSearch.focused() && mSearch.rect.w <= cs::geometry::kSearchMinWidth + 1) {
        Rect titleRect{mLeft + cs::geometry::kTitleX, mTop + cs::geometry::kTitleY, static_cast<float>(mLayout.titleMaxWidth()), 9};
        if (titleRect.contains(mx, my) && p.textWidth(m.title) > titleRect.w) {
            drawTooltip(p, {{m.title, 0xFFFFFF}}, mx, my);
            return;
        }
    }
    // Hovered slot item (only with an empty cursor, like SC).
    auto* player = currentPlayer();
    if (!player) return;
    if (!player->mPlayerUIContainer->getItem(0).isNull()) {
        // Carrying something: SB only shows the contents tooltip of backpacks.
        auto const* s = stackAt(mHover);
        if (s && game::isBackpack(*s)) tooltip::drawBackpackTooltip(p, *s, mx, my);
        return;
    }
    auto const* s = stackAt(mHover);
    if (!s || s->isNull()) return;
    std::vector<TooltipLine> extra;
    if (mHover.area == SlotRef::Area::Storage) {
        auto const& d   = m.slots[static_cast<std::size_t>(mHover.index)];
        int         lim = m.slotLimits[static_cast<std::size_t>(mHover.index)];
        if (lim > 0 && lim != d.maxStack) {
            extra.push_back(
                {ctr("abp.companion.tooltip.stack_count", "§7Count: {}", "§3" + cs::groupedCount(d.count) + "§7 / §3" + cs::groupedCount(lim)), 0xAAAAAA}
            );
        }
    } else if (mHover.area == SlotRef::Area::Upgrade) {
        auto const& u = m.upgrades[static_cast<std::size_t>(mHover.index)];
        for (auto const& l : u.lore) extra.push_back({l, 0xAAAAAA});
    }
    if (tooltip::drawBackpackTooltip(p, *s, mx, my, extra)) return;
    auto lines2 = itemTooltipLines(*s);
    lines2.insert(lines2.end(), extra.begin(), extra.end());
    drawTooltip(p, lines2, mx, my);
}

void BackpackScreenLayer::render(Painter& p, FrameInfo const& f) {
    auto const* m = net::displayModel();
    if (!m) return;
    syncModel(*m);
    if (m->kind == "menu") {
        renderMenu(p, *m, f);
        return;
    }
    if (mView == View::Settings) {
        renderSettings(p, *m, f);
        return;
    }
    updateSearch(*m);
    layout(*m, f);
    buildControls(*m);
    if (mPendingTab >= 0) {
        // "Keep tab open": reopen the upgrade tab that was open last time.
        for (std::size_t i = 0; i < mTabs.size(); ++i) {
            auto& t = mTabs[i];
            if (t.slot != mPendingTab || !t.native) continue;
            mOpenTab = static_cast<int>(i);
            t.widgets.widgets.clear();
            TabContext ctx{*m, t.slot, m->upgrades[static_cast<std::size_t>(t.slot)], &mUpgradeStacks[static_cast<std::size_t>(t.slot)], {}, {}};
            ctx.sendTabAction    = [slot = t.slot](nlohmann::json d) { net::sendScreenAction({{"action", "tab"}, {"slot", slot}, {"data", std::move(d)}}); };
            ctx.sendScreenAction = [](nlohmann::json b) { net::sendScreenAction(std::move(b)); };
            t.openSize = t.native->build(ctx, t.rect, t.widgets);
            layout(*m, f);
        }
        mPendingTab = -1;
    }
    float mx = f.mouseX, my = f.mouseY;
    mHover   = slotAt(*m, mx, my);
    double t = now();

    // Screen.renderTransparentBackground: 0xC0101010 -> 0xD0101010.
    p.gradientRect(f.screen, Color::argb(0xC0101010), Color::argb(0xD0101010));
    p.flush();
    drawTabs(p, *m, mx, my, t);
    p.flush();
    sb::storageBackground(p, {mLeft, mTop, static_cast<float>(mLayout.imageWidth), static_cast<float>(mLayout.imageHeight)});
    drawSlots(p, *m, mx, my);
    p.flush();
    mControls.render(p, mx, my, t);
    drawSearch(p, t);
    p.flush();
    drawErrorOverlay(p, *m);

    // The item on the cursor (follows the mouse, centred).
    if (auto* player = currentPlayer()) {
        auto const& cursor = player->mPlayerUIContainer->getItem(0);
        if (!cursor.isNull()) {
            p.item(cursor, mx - 8, my - 8);
            drawCount(p, cursor.mCount, mx - 8, my - 8);
            p.flush();
        }
    }
    drawTooltips(p, *m, mx, my);
}

// ---- hit testing ----------------------------------------------------------------------------------------------

Rect BackpackScreenLayer::slotRect(SlotRef const& s) const {
    switch (s.area) {
    case SlotRef::Area::Upgrade: {
        auto const& pos = mLayout.upgradeSlots[static_cast<std::size_t>(s.index)];
        return {mLeft + static_cast<float>(pos.x) - 1, mTop + static_cast<float>(pos.y) - 1, 18, 18};
    }
    case SlotRef::Area::Inventory: {
        auto const& pos = mLayout.playerSlots[static_cast<std::size_t>(s.index)];
        return {mLeft + static_cast<float>(pos.x) - 1, mTop + static_cast<float>(pos.y) - 1, 18, 18};
    }
    case SlotRef::Area::Storage:
        for (auto const& ps : mPlaced) {
            if (ps.index == s.index) return {mLeft + static_cast<float>(ps.pos.x) - 1, mTop + static_cast<float>(ps.pos.y) - 1, 18, 18};
        }
        return {};
    default:
        return {};
    }
}

SlotRef BackpackScreenLayer::slotAt(cs::BackpackScreenModel const&, float x, float y) const {
    // Open tab content covers what is under it.
    if (mOpenTab >= 0 && mOpenTab < static_cast<int>(mTabs.size()) && mTabs[static_cast<std::size_t>(mOpenTab)].rect.contains(x, y)) return {};
    for (std::size_t i = 0; i < mLayout.upgradeSlots.size(); ++i) {
        SlotRef s{SlotRef::Area::Upgrade, static_cast<int>(i)};
        if (slotRect(s).contains(x, y)) return s;
    }
    for (auto const& ps : mPlaced) {
        Rect r{mLeft + static_cast<float>(ps.pos.x) - 1, mTop + static_cast<float>(ps.pos.y) - 1, 18, 18};
        if (r.contains(x, y)) return {SlotRef::Area::Storage, ps.index};
    }
    for (int i = 0; i < 36; ++i) {
        SlotRef s{SlotRef::Area::Inventory, i};
        if (slotRect(s).contains(x, y)) return s;
    }
    return {};
}

ItemStack const* BackpackScreenLayer::stackAt(SlotRef const& s) const {
    switch (s.area) {
    case SlotRef::Area::Storage:
        return s.index >= 0 && static_cast<std::size_t>(s.index) < mSlotStacks.size() ? &mSlotStacks[static_cast<std::size_t>(s.index)] : nullptr;
    case SlotRef::Area::Upgrade:
        return s.index >= 0 && static_cast<std::size_t>(s.index) < mUpgradeStacks.size() ? &mUpgradeStacks[static_cast<std::size_t>(s.index)] : nullptr;
    case SlotRef::Area::Inventory:
        if (auto* player = currentPlayer()) return &player->getInventory().getItem(s.index);
        return nullptr;
    default:
        return nullptr;
    }
}

// ---- input ----------------------------------------------------------------------------------------------------

bool BackpackScreenLayer::onMouseButton(MouseButton button, bool down, float x, float y) {
    auto const* m = net::screenModel();
    if (!m || !rendering) return false;
    if (m->kind == "menu") return down ? menuMouse(*m, button, x, y) : true;
    if (mView == View::Settings) return settingsMouse(*m, button, down, x, y);
    if (!down) {
        mDraggingBar = false;
        if (mOpenTab >= 0 && mOpenTab < static_cast<int>(mTabs.size())) mTabs[static_cast<std::size_t>(mOpenTab)].widgets.mouseUp(button, x, y);
        if (mDistButton >= 0 && (button == MouseButton::Right ? 1 : 0) == mDistButton) finishDistribution();
        return true;
    }
    // Search box: left focuses, right clears (SC SearchBox); clicks elsewhere unfocus it.
    if (mSearch.rect.contains(x, y)) {
        if (button == MouseButton::Right) {
            mSearch.setText({}, true);
            mMatchesDirty = true;
        } else {
            mSearch.onMouseButton(false, true, x, y);
        }
        return true;
    }
    if (mSearch.focused()) mSearch.setFocused(false);

    // Open tab content.
    if (mOpenTab >= 0 && mOpenTab < static_cast<int>(mTabs.size())) {
        auto& t = mTabs[static_cast<std::size_t>(mOpenTab)];
        if (t.widgets.mouseDown(button, x, y)) return true;
    }
    // Tab icons (16x16 at +1,+4): settings opens the settings UI, upgrade tabs open/close.
    for (std::size_t i = 0; i < mTabs.size(); ++i) {
        auto& t = mTabs[i];
        Rect  iconRect{t.rect.x + 1, t.rect.y + 4, 16, 16};
        bool  isOpen = static_cast<int>(i) == mOpenTab;
        if (!iconRect.contains(x, y)) {
            if (isOpen && t.rect.contains(x, y)) return true; // inside the open tab
            continue;
        }
        if (button != MouseButton::Left) return true;
        if (t.slot < 0) {
            showSettings(true); // SB opens its settings screen
            return true;
        }
        if (isOpen) {
            mOpenTab = -1;
            t.widgets.widgets.clear();
            mKeptTab.erase(m->uuid);
            return true;
        }
        if (mOpenTab >= 0) mTabs[static_cast<std::size_t>(mOpenTab)].widgets.widgets.clear();
        mOpenTab        = static_cast<int>(i);
        mKeptTab[m->uuid] = t.slot;
        t.widgets.widgets.clear();
        if (t.native) {
            TabContext ctx{*m, t.slot, m->upgrades[static_cast<std::size_t>(t.slot)], &mUpgradeStacks[static_cast<std::size_t>(t.slot)], {}, {}};
            ctx.sendTabAction    = [slot = t.slot](nlohmann::json d) { net::sendScreenAction({{"action", "tab"}, {"slot", slot}, {"data", std::move(d)}}); };
            ctx.sendScreenAction = [](nlohmann::json b) { net::sendScreenAction(std::move(b)); };
            t.openSize = t.native->build(ctx, t.rect, t.widgets);
        }
        return true;
    }
    // Buttons and switches.
    if (mControls.mouseDown(button, x, y)) return true;
    // Scroll bar.
    if (mLayout.scroll && button == MouseButton::Left) {
        auto bar = mLayout.scrollBar();
        Rect r{mLeft + static_cast<float>(bar.x), mTop + static_cast<float>(bar.y), static_cast<float>(bar.w), static_cast<float>(bar.h)};
        if (r.contains(x, y)) {
            mDraggingBar = true;
            mDragY       = y;
            return true;
        }
    }
    // Middle mouse: Sort (anywhere but the player inventory, SC tryCallSort).
    auto slot = slotAt(*m, x, y);
    if (button == MouseButton::Middle && input::mouseMatches(input::KeyAction::Sort, button)) {
        if (slot.area != SlotRef::Area::Inventory) send({{"action", "sort"}});
        return true;
    }
    if (slot.area == SlotRef::Area::None) {
        Rect gui{mLeft, mTop, static_cast<float>(mLayout.imageWidth), static_cast<float>(mLayout.imageHeight)};
        Rect upgrades{mLeft - 25, mTop, 29, static_cast<float>(mLayout.upgradeColumn.h)};
        bool outside = !gui.contains(x, y) && !upgrades.contains(x, y);
        auto* player = currentPlayer();
        if (outside && player && !player->mPlayerUIContainer->getItem(0).isNull() && button != MouseButton::Middle) {
            send({{"action", "dropCursor"}, {"all", button == MouseButton::Left}});
        }
        return true;
    }
    if (button == MouseButton::Middle) return true;
    double t      = now();
    bool   shift  = input::shiftDown();
    // With an item on the cursor the click happens on release: a drag over several slots
    // distributes the stack instead (vanilla quick craft).
    if (auto* pl = currentPlayer(); pl && !shift && !pl->mPlayerUIContainer->getItem(0).isNull()
        && (slot.area == SlotRef::Area::Storage || slot.area == SlotRef::Area::Inventory)) {
        mDistButton = button == MouseButton::Right ? 1 : 0;
        mDistSlots  = {slot};
        return true;
    }
    bool   ctrl   = input::ctrlDown();
    bool   isDouble = button == MouseButton::Left && !shift && slot == mLastClick && t - mLastClickAt < 0.25;
    mLastClick    = slot;
    mLastClickAt  = isDouble ? -10 : t;
    if (isDouble && slot.area != SlotRef::Area::Upgrade) {
        send({{"action", "double"}, {"area", areaName(slot.area)}, {"index", slot.index}});
        return true;
    }
    send({
        {"action", "click"},
        {"area", areaName(slot.area)},
        {"index", slot.index},
        {"button", button == MouseButton::Right ? 1 : 0},
        {"shift", shift},
        {"ctrl", ctrl},
    });
    return true;
}

bool BackpackScreenLayer::onMouseMove(float x, float y) {
    auto const* m = net::screenModel();
    if (!m || !rendering) return false;
    if (mView == View::Settings && mDragButton >= 0) {
        // SC SettingsScreen.mouseDragged: dragging over slots selects / unselects them too.
        for (auto const& ps : mPlaced) {
            Rect r{mLeft + static_cast<float>(ps.pos.x) - 1, mTop + static_cast<float>(ps.pos.y) - 1, 18, 18};
            if (r.contains(x, y) && ps.index != mDragLastSlot) {
                mDragLastSlot = ps.index;
                settingsSlotClick(ps.index, mDragButton);
            }
        }
        return true;
    }
    if (mDraggingBar) {
        cs::scrollDrag(mScroll, mLayout, m->storageSlots, y - mDragY);
        mDragY = y;
        return true;
    }
    if (mOpenTab >= 0 && mOpenTab < static_cast<int>(mTabs.size())) mTabs[static_cast<std::size_t>(mOpenTab)].widgets.mouseMove(x, y);
    if (mDistButton >= 0 && mView == View::Storage && m->kind != "menu") {
        auto s = slotAt(*m, x, y);
        if ((s.area == SlotRef::Area::Storage || s.area == SlotRef::Area::Inventory)
            && std::find(mDistSlots.begin(), mDistSlots.end(), s) == mDistSlots.end()) {
            mDistSlots.push_back(s);
        }
    }
    return false;
}

void BackpackScreenLayer::finishDistribution() {
    int button  = mDistButton;
    mDistButton = -1;
    if (mDistSlots.empty()) return;
    if (mDistSlots.size() > 1) {
        nlohmann::json slots = nlohmann::json::array();
        for (auto const& s : mDistSlots) slots.push_back({{"area", areaName(s.area)}, {"index", s.index}});
        send({{"action", "distribute"}, {"button", button}, {"slots", std::move(slots)}});
        mDistSlots.clear();
        return;
    }
    auto   slot     = mDistSlots.front();
    double t        = now();
    bool   isDouble = button == 0 && slot == mLastClick && t - mLastClickAt < 0.25;
    mLastClick      = slot;
    mLastClickAt    = isDouble ? -10 : t;
    mDistSlots.clear();
    if (isDouble) send({{"action", "double"}, {"area", areaName(slot.area)}, {"index", slot.index}});
    else send({{"action", "click"}, {"area", areaName(slot.area)}, {"index", slot.index}, {"button", button}, {"shift", false}, {"ctrl", false}});
}

bool BackpackScreenLayer::onScroll(float x, float y, float delta) {
    auto const* m = net::screenModel();
    if (!m || !rendering) return false;
    if (mOpenTab >= 0 && mOpenTab < static_cast<int>(mTabs.size()) && mTabs[static_cast<std::size_t>(mOpenTab)].widgets.scroll(x, y, delta)) {
        return true;
    }
    if (mLayout.scroll) {
        auto const& panel = mLayout.scrollPanel;
        Rect        r{mLeft + static_cast<float>(panel.x), mTop + static_cast<float>(panel.y), static_cast<float>(panel.w), static_cast<float>(panel.h)};
        if (r.contains(x, y)) cs::scrollBy(mScroll, mLayout, m->storageSlots, delta > 0 ? 1 : -1);
    }
    return true;
}

void BackpackScreenLayer::openBackpackKey(cs::BackpackScreenModel const& m) {
    // SB KeybindHandler + BackpackScreen.keyPressed.
    if (mHover.area == SlotRef::Area::Storage) {
        auto const& d = m.slots[static_cast<std::size_t>(mHover.index)];
        if (!d.empty() && d.count == 1 && game::isBackpack(d)) {
            send({{"action", "openNested"}, {"index", mHover.index}});
            return;
        }
    }
    if (mHover.area == SlotRef::Area::Inventory) {
        auto const* s = stackAt(mHover);
        if (s && !s->isNull() && game::isBackpack(*s) && s->mCount == 1) {
            net::sendKeyAction({{"action", "openAt"}, {"where", "inventory"}, {"slot", mHover.index}});
            return;
        }
    }
    if (m.nested) send({{"action", "back"}});
    else requestClose();
}

bool BackpackScreenLayer::onKey(int vk, bool down) {
    auto const* m = net::screenModel();
    if (!m || !rendering) return false;
    if (mSearch.focused()) return mSearch.onKey(vk, down);
    if (openTabWantsText()) return mTabs[static_cast<std::size_t>(mOpenTab)].widgets.key(vk, down);
    if (!down) return true;
    using input::KeyAction;
    bool shift = input::shiftDown();
    if (vk == VK_ESCAPE_ && mView == View::Settings) {
        showSettings(false); // SC: ESC on the settings screen goes back to the storage screen
        return true;
    }
    if (vk == VK_ESCAPE_ && m->nested) {
        send({{"action", "back"}});
        return true;
    }
    if (vk == 'E') {
        requestClose();
        return true;
    }
    if (m->kind == "menu" || mView == View::Settings) {
        if (m->kind == "menu" && mHover.area == SlotRef::Area::Inventory) {
            if (vk == 'Q') send({{"action", "drop"}, {"area", "inventory"}, {"index", mHover.index}, {"all", input::ctrlDown()}});
            else if (vk >= '1' && vk <= '9') send({{"action", "hotbar"}, {"area", "inventory"}, {"index", mHover.index}, {"hotbar", vk - '1'}});
        }
        if (input::keyMatches(KeyAction::OpenBackpack, vk) && m->kind == "menu") requestClose();
        return true;
    }
    if (input::keyMatches(KeyAction::OpenBackpack, vk)) {
        openBackpackKey(*m);
        return true;
    }
    if (input::keyMatches(KeyAction::Sort, vk)) {
        if (mHover.area != SlotRef::Area::Inventory) send({{"action", "sort"}});
        return true;
    }
    if (input::keyMatches(KeyAction::TransferToStorage, vk)) {
        send({{"action", "transfer"}, {"to", "storage"}, {"matching", !shift}});
        return true;
    }
    if (input::keyMatches(KeyAction::TransferToInventory, vk)) {
        send({{"action", "transfer"}, {"to", "inventory"}, {"matching", !shift}});
        return true;
    }
    if (mHover.area == SlotRef::Area::Storage || mHover.area == SlotRef::Area::Inventory) {
        if (vk == 'Q') {
            send({{"action", "drop"}, {"area", areaName(mHover.area)}, {"index", mHover.index}, {"all", input::ctrlDown()}});
        } else if (vk >= '1' && vk <= '9') {
            send({{"action", "hotbar"}, {"area", areaName(mHover.area)}, {"index", mHover.index}, {"hotbar", vk - '1'}});
        }
    }
    return true;
}

bool BackpackScreenLayer::onChar(std::string const& utf8) {
    if (mSearch.focused()) {
        bool r = mSearch.onChar(utf8);
        mMatchesDirty = true;
        return r;
    }
    if (openTabWantsText()) return mTabs[static_cast<std::size_t>(mOpenTab)].widgets.chr(utf8);
    return false;
}

// ---- generic menus ----------------------------------------------------------------------------------------------
// Server menus that have no native screen yet (e.g. a slot picker) are drawn in SB's storage style:
// the storage background with `rows` x 9 slots (decoration slots left out) above the inventory.

void BackpackScreenLayer::renderMenu(Painter& p, cs::BackpackScreenModel const& m, FrameInfo const& f) {
    cs::LayoutInput in;
    in.storageSlots = std::max(1, m.menuRows) * 9;
    in.upgradeSlots = 0;
    in.screenHeight = std::max(static_cast<int>(f.screen.h), cs::geometry::kHeightWithoutStorageSlots + in.storageSlots / 9 * 18);
    in.sortButtons  = cs::SortButtonsPosition::Hidden;
    mLayout         = cs::computeLayout(in);
    mLeft           = std::floor((f.screen.w - static_cast<float>(mLayout.imageWidth)) / 2);
    mTop            = std::floor((f.screen.h - static_cast<float>(mLayout.imageHeight)) / 2);
    float mx = f.mouseX, my = f.mouseY;

    mHover = {};
    for (std::size_t i = 0; i < m.menuSlots.size(); ++i) {
        if (isFillerItem(m.menuSlots[i])) continue;
        Rect r{mLeft + 7 + static_cast<float>(i % 9) * 18, mTop + 17 + static_cast<float>(i / 9) * 18, 18, 18};
        if (r.contains(mx, my)) mHover = {SlotRef::Area::Menu, static_cast<int>(i)};
    }
    if (mHover.area == SlotRef::Area::None) {
        for (int i = 0; i < 36; ++i) {
            if (slotRect({SlotRef::Area::Inventory, i}).contains(mx, my)) mHover = {SlotRef::Area::Inventory, i};
        }
    }

    p.gradientRect(f.screen, Color::argb(0xC0101010), Color::argb(0xD0101010));
    p.flush();
    sb::storageBackground(p, {mLeft, mTop, static_cast<float>(mLayout.imageWidth), static_cast<float>(mLayout.imageHeight)});
    for (std::size_t i = 0; i < m.menuSlots.size(); ++i) {
        auto const& d = m.menuSlots[i];
        if (isFillerItem(d)) continue;
        float x = mLeft + 8 + static_cast<float>(i % 9) * 18, y = mTop + 18 + static_cast<float>(i / 9) * 18;
        sb::slotFrame(p, x, y);
        if (i < mMenuStacks.size() && !mMenuStacks[i].isNull()) {
            p.item(mMenuStacks[i], x, y);
            drawCount(p, d.count, x, y);
        }
        if (mHover.area == SlotRef::Area::Menu && mHover.index == static_cast<int>(i)) {
            p.fillRect({x, y, 16, 16}, sb::color::HOVER, Painter::Layer::Foreground);
        }
    }
    p.text(truncated(p, m.title, static_cast<float>(mLayout.imageWidth - 15)), mLeft + 8, mTop + 6, Color::rgb(kTextColor));
    p.text(ctr("abp.companion.screen.inventory", "Inventory"), mLeft + static_cast<float>(mLayout.inventoryLabelX), mTop + static_cast<float>(mLayout.inventoryLabelY), Color::rgb(kTextColor));
    if (auto* player = currentPlayer()) {
        auto& inv = player->getInventory();
        for (int i = 0; i < 36; ++i) {
            auto const& pos = mLayout.playerSlots[static_cast<std::size_t>(i)];
            float       x = mLeft + static_cast<float>(pos.x), y = mTop + static_cast<float>(pos.y);
            sb::slotFrame(p, x, y);
            auto const& s = inv.getItem(i);
            if (!s.isNull()) {
                p.item(s, x, y);
                drawCount(p, s.mCount, x, y);
            }
            if (mHover.area == SlotRef::Area::Inventory && mHover.index == i) p.fillRect({x, y, 16, 16}, sb::color::HOVER, Painter::Layer::Foreground);
        }
        p.flush();
        auto const& cursor = player->mPlayerUIContainer->getItem(0);
        if (!cursor.isNull()) {
            p.item(cursor, mx - 8, my - 8);
            drawCount(p, cursor.mCount, mx - 8, my - 8);
            p.flush();
            return;
        }
    }
    ItemStack const* hovered = nullptr;
    if (mHover.area == SlotRef::Area::Menu && static_cast<std::size_t>(mHover.index) < mMenuStacks.size()) hovered = &mMenuStacks[static_cast<std::size_t>(mHover.index)];
    else if (mHover.area == SlotRef::Area::Inventory) hovered = stackAt(mHover);
    if (hovered && !hovered->isNull() && !tooltip::drawBackpackTooltip(p, *hovered, mx, my)) drawTooltip(p, itemTooltipLines(*hovered), mx, my);
}

bool BackpackScreenLayer::menuMouse(cs::BackpackScreenModel const&, MouseButton button, float x, float y) {
    if (button == MouseButton::Middle) return true;
    bool shift = input::shiftDown();
    int  b     = button == MouseButton::Right ? 1 : 0;
    if (mHover.area == SlotRef::Area::Menu) {
        send({{"action", "menuClick"}, {"index", mHover.index}, {"button", b}, {"shift", shift}});
        return true;
    }
    if (mHover.area == SlotRef::Area::Inventory) {
        send({{"action", "click"}, {"area", "inventory"}, {"index", mHover.index}, {"button", b}, {"shift", shift}});
        return true;
    }
    Rect gui{mLeft, mTop, static_cast<float>(mLayout.imageWidth), static_cast<float>(mLayout.imageHeight)};
    auto* player = currentPlayer();
    if (!gui.contains(x, y) && player && !player->mPlayerUIContainer->getItem(0).isNull()) {
        send({{"action", "dropCursor"}, {"all", button == MouseButton::Left}});
    }
    return true;
}

// ---- settings screen -----------------------------------------------------------------------------------------------
// SB's SettingsScreen: the storage slots (contents, memory and no-sort marks), the player inventory,
// and tabs: Back To Backpack, Backpack (context + main toggles), No Sort and Memory. Slot clicks /
// drags in the No Sort or Memory tab select (left) / unselect (right) slots.

void BackpackScreenLayer::showSettings(bool on) {
    mView         = on ? View::Settings : View::Storage;
    mSettingsOpen = -1;
    mDragButton   = -1;
    mSettingsTabs.clear();
    mSearch.setFocused(false);
    mScroll = {};
    if (!on) mMatchesDirty = true;
}

namespace {
/// SC ContextButton: 62x18, the context name centred in white.
class ContextButton final : public Widget {
public:
    std::function<std::string()>              label;
    std::function<std::vector<TooltipLine>()> tip;
    std::function<void()>                     onClick;

    void render(Painter& p, float mx, float my, double) override {
        sb::buttonBackground(p, rect, hovered(mx, my));
        auto text = label ? label() : std::string{};
        p.text(text, rect.x + rect.w / 2, rect.y - 4 + rect.h / 2, Color::rgb(0xFFFFFF), true, 1, TextAlign::Center);
    }
    bool mouseDown(MouseButton, float x, float y) override {
        if (!hovered(x, y)) return false;
        if (onClick) onClick();
        return true;
    }
    void tooltip(float x, float y, std::vector<TooltipLine>& out) const override {
        if (!hovered(x, y) || !tip) return;
        auto lines = tip();
        out.insert(out.end(), lines.begin(), lines.end());
    }
};

std::unique_ptr<Button> iconButton(Rect r, sb::Icon icon, std::vector<TooltipLine> tip, std::function<void(int)> onClick) {
    auto b          = std::make_unique<Button>();
    b->size         = Button::Size::Normal;
    b->rect         = r;
    b->icon         = [icon](Painter& p, float x, float y) { sb::icon(p, icon, x + 1, y + 1); };
    b->tooltipLines = std::move(tip);
    b->onClick      = std::move(onClick);
    return b;
}

void setting(nlohmann::json body) {
    body["action"] = "setting";
    net::sendScreenAction(std::move(body));
}
} // namespace

void BackpackScreenLayer::buildSettingsTab(cs::BackpackScreenModel const& m, TabState& tab) {
    tab.widgets.widgets.clear();
    float x = tab.rect.x, y = tab.rect.y;
    auto  values = settingsValues(m);
    if (tab.slot == kMainTab) {
        auto ctxBtn   = std::make_unique<ContextButton>();
        ctxBtn->rect  = {x + 3, y + 24, 62, 18};
        ctxBtn->label = [this] {
            auto const* cur = net::screenModel();
            bool        pl  = cur && settingsValues(*cur).value("context", std::string("backpack")) == "player";
            return pl ? ctr("abp.companion.settings.context_player", "Player") : ctr("abp.companion.settings.context_backpack", "Backpack");
        };
        ctxBtn->tip = [this]() -> std::vector<TooltipLine> {
            auto const* cur = net::screenModel();
            bool        pl  = cur && settingsValues(*cur).value("context", std::string("backpack")) == "player";
            if (pl) {
                return {{ctr("abp.companion.settings.context_player.tooltip", "Player level settings"), 0xFFFFFF},
                        {ctr("abp.companion.settings.context_player.detail", "Apply to all backpacks/storages unless overriden"), 0xAAAAAA}};
            }
            return {{ctr("abp.companion.settings.context_backpack.tooltip", "This Backpack's settings"), 0xFFFFFF},
                    {ctr("abp.companion.settings.context_backpack.detail", "Inherited from player or overriden for this backpack"), 0xAAAAAA}};
        };
        ctxBtn->onClick = [] { setting({{"what", "context"}}); };
        tab.widgets.add(std::move(ctxBtn));
        struct Opt {
            char const* name;
            sb::Icon    on, off;
            char const* onKey;
            char const* onText;
            char const* offKey;
            char const* offText;
            char const* onTipKey;
            char const* onTip;
            char const* offTipKey;
            char const* offTip;
            bool        def;
        };
        static Opt const opts[] = {
            {"shiftClickIntoOpenTab", sb::Icon::ShiftClickTabOn, sb::Icon::ShiftClickTabOff,
             "abp.companion.settings.shift_click_open_tab.on", "Shift Click Into Open Tab First",
             "abp.companion.settings.shift_click_open_tab.off", "Shift Click Into Inventory First",
             "abp.companion.settings.shift_click_open_tab.on.tooltip", "Shift click from storage/inventory will first try to put the stack in an open tab and only then into inventory/storage.",
             "abp.companion.settings.shift_click_open_tab.off.tooltip", "Shift click from storage/inventory will first try to put the stack in inventory/storage and only then into an open tab.",
             true},
            {"keepTabOpen", sb::Icon::KeepTabOpenOn, sb::Icon::KeepTabOpenOff,
             "abp.companion.settings.keep_tab_open.on", "Keep Tab Open: ON",
             "abp.companion.settings.keep_tab_open.off", "Keep Tab Open: OFF",
             "abp.companion.settings.keep_tab_open.on.tooltip", "On close of its gui the backpack/storage records which upgrade tab was last open and opens it when the gui is open next time",
             "abp.companion.settings.keep_tab_open.off.tooltip", "Open upgrade tab gets closed when the backpack/storage gui is closed and when the gui is next open all of the tabs are closed",
             true},
            {"keepSearchPhrase", sb::Icon::KeepSearchOn, sb::Icon::KeepSearchOff,
             "abp.companion.settings.keep_search_phrase.on", "Keep Search Phrase: ON",
             "abp.companion.settings.keep_search_phrase.off", "Keep Search Phrase: OFF",
             "abp.companion.settings.keep_search_phrase.on.tooltip", "Backpack / Storage gui keeps the search phrase and prefills it / filters by it when open again",
             "abp.companion.settings.keep_search_phrase.off.tooltip", "Backpack / Storage gui clears the search phrase when closed and shows all unfiltered item when open again",
             false},
        };
        float bx = x + 3;
        for (auto const& o : opts) {
            auto t     = std::make_unique<ToggleButton>();
            t->size    = Button::Size::Normal;
            t->rect    = {bx, y + 46, 18, 18};
            t->states  = {
                {[o](Painter& p, float ix, float iy) { sb::icon(p, o.off, ix + 1, iy + 1); },
                 {{ctr(o.offKey, o.offText), 0xFFFFFF}, {ctr(o.offTipKey, o.offTip), 0xAAAAAA}}},
                {[o](Painter& p, float ix, float iy) { sb::icon(p, o.on, ix + 1, iy + 1); },
                 {{ctr(o.onKey, o.onText), 0xFFFFFF}, {ctr(o.onTipKey, o.onTip), 0xAAAAAA}}},
            };
            t->current = [this, o] {
                auto const* cur = net::screenModel();
                return cur && settingsValues(*cur).value(o.name, o.def) ? 1 : 0;
            };
            t->onClick = [o](int) { setting({{"what", "option"}, {"name", o.name}}); };
            tab.widgets.add(std::move(t));
            bx += 18;
        }
        tab.openSize = {std::max(65.f + 6, 20 + 6 + 60.f), 46 + 18 + 6};
        return;
    }
    bool memory = tab.slot == kMemoryTab;
    char const* what = memory ? "memory" : "noSort";
    tab.widgets.add(iconButton({x + 3, y + 24, 18, 18}, sb::Icon::SelectAll,
                               {{ctr("abp.companion.settings.select_all", "Select All Slots"), 0xFFFFFF}},
                               [what](int b) { if (b == 0) setting({{"what", what}, {"op", "selectAll"}}); }));
    tab.widgets.add(iconButton({x + 21, y + 24, 18, 18}, sb::Icon::UnselectAll,
                               {{ctr("abp.companion.settings.unselect_all", "Unselect All Slots"), 0xFFFFFF}},
                               [what](int b) { if (b == 0) setting({{"what", what}, {"op", "unselectAll"}}); }));
    if (memory) {
        auto t     = std::make_unique<ToggleButton>();
        t->size    = Button::Size::Normal;
        t->rect    = {x + 39, y + 24, 18, 18};
        t->states  = {
            {[](Painter& p, float ix, float iy) { sb::icon(p, sb::Icon::IgnoreNbt, ix + 1, iy + 1); }, {{ctr("abp.companion.settings.ignore_nbt", "Ignore NBT"), 0xFFFFFF}}},
            {[](Painter& p, float ix, float iy) { sb::icon(p, sb::Icon::MatchNbt, ix + 1, iy + 1); }, {{ctr("abp.companion.settings.match_nbt", "Match NBT"), 0xFFFFFF}}},
        };
        t->current = [this] {
            auto const* cur = net::screenModel();
            return cur && settingsValues(*cur).value("memoryMatchNbt", false) ? 1 : 0;
        };
        t->onClick = [](int) { setting({{"what", "memoryMatchNbt"}}); };
        tab.widgets.add(std::move(t));
    } else {
        auto b          = std::make_unique<Button>();
        b->size         = Button::Size::Normal;
        b->rect         = {x + 39, y + 24, 18, 18};
        b->icon         = [](Painter& p, float ix, float iy) {
            auto const* cur = net::screenModel();
            sb::colorSquare(p, ix, iy, cur ? cur->noSortColor : 0x80C71F);
        };
        b->tooltipLines = {{ctr("abp.companion.settings.toggle_color", "Toggle Color"), 0xFFFFFF},
                           {ctr("abp.companion.settings.toggle_color.detail1", "Next = Left Click"), 0xAAAAAA},
                           {ctr("abp.companion.settings.toggle_color.detail2", "Previous = Right Click"), 0xAAAAAA}};
        b->onClick      = [](int button) { setting({{"what", "noSortColor"}, {"dir", button == 1 ? -1 : 1}}); };
        tab.widgets.add(std::move(b));
    }
    tab.openSize = {std::max(39.f + 18 + 6, 20 + 6 + 60.f), 24 + 18 + 6};
}

void BackpackScreenLayer::settingsSlotClick(int index, int button) {
    if (mSettingsOpen < 0 || mSettingsOpen >= static_cast<int>(mSettingsTabs.size())) return;
    int kind = mSettingsTabs[static_cast<std::size_t>(mSettingsOpen)].slot;
    if (kind != kMemoryTab && kind != kNoSortTab) return;
    if (button != 0 && button != 1) return;
    setting({{"what", kind == kMemoryTab ? "memory" : "noSort"}, {"op", button == 0 ? "select" : "unselect"}, {"index", index}});
}

void BackpackScreenLayer::renderSettings(Painter& p, cs::BackpackScreenModel const& m, FrameInfo const& f) {
    cs::LayoutInput in;
    in.storageSlots = m.storageSlots;
    in.upgradeSlots = 0;
    in.screenHeight = static_cast<int>(f.screen.h);
    in.sortButtons  = cs::SortButtonsPosition::Hidden;
    mLayout         = cs::computeLayout(in);
    mLeft           = std::floor((f.screen.w - static_cast<float>(mLayout.imageWidth)) / 2);
    mTop            = std::floor((f.screen.h - static_cast<float>(mLayout.imageHeight)) / 2);
    cs::scrollBy(mScroll, mLayout, m.storageSlots, 0);
    mPlaced = cs::placeStorageSlots(mLayout, m.storageSlots, {}, mScroll, &mVisibleCount);
    float  mx = f.mouseX, my = f.mouseY;
    double t  = now();

    if (mSettingsTabs.empty()) {
        for (int kind : {kBackTab, kMainTab, kNoSortTab, kMemoryTab}) {
            TabState tab;
            tab.slot = kind;
            mSettingsTabs.push_back(std::move(tab));
        }
    }
    float tx = mLeft + static_cast<float>(mLayout.tabsOrigin.x), ty = mTop + static_cast<float>(mLayout.tabsOrigin.y);
    for (std::size_t i = 0; i < mSettingsTabs.size(); ++i) {
        auto& tab = mSettingsTabs[i];
        Rect  r{tx, ty + static_cast<float>(i) * 25, cs::geometry::kTabWidth, cs::geometry::kTabHeight};
        if (r.x != tab.rect.x || r.y != tab.rect.y) {
            tab.rect = r;
            if (static_cast<int>(i) == mSettingsOpen) buildSettingsTab(m, tab);
        }
        tab.rect.x = r.x;
        tab.rect.y = r.y;
        if (static_cast<int>(i) == mSettingsOpen) {
            tab.rect.w = std::max<float>(tab.openSize.w, cs::geometry::kTabWidth);
            tab.rect.h = tab.openSize.h;
        } else {
            tab.rect.w = r.w;
            tab.rect.h = r.h;
        }
    }

    mHover = {};
    bool overTab = mSettingsOpen >= 0 && mSettingsTabs[static_cast<std::size_t>(mSettingsOpen)].rect.contains(mx, my);
    if (!overTab) {
        for (auto const& ps : mPlaced) {
            Rect r{mLeft + static_cast<float>(ps.pos.x) - 1, mTop + static_cast<float>(ps.pos.y) - 1, 18, 18};
            if (r.contains(mx, my)) mHover = {SlotRef::Area::Storage, ps.index};
        }
    }

    p.gradientRect(f.screen, Color::argb(0xC0101010), Color::argb(0xD0101010));
    p.flush();
    // Tabs (behind the background), the open one last.
    auto const* open = mSettingsOpen >= 0 ? &mSettingsTabs[static_cast<std::size_t>(mSettingsOpen)] : nullptr;
    auto drawTab     = [&](TabState& tab, bool isOpen) {
        sb::tabBackground(p, tab.rect);
        sb::Icon icon = tab.slot == kBackTab ? sb::Icon::BackToBackpack
                      : tab.slot == kMainTab ? sb::Icon::BackpackSettings
                      : tab.slot == kNoSortTab ? sb::Icon::NoSortTab
                                               : sb::Icon::MemoryTab;
        sb::icon(p, icon, tab.rect.x + 1, tab.rect.y + 4);
        if (!isOpen) return;
        auto title = tab.slot == kMainTab ? ctr("abp.companion.settings.backpack", "Backpack")
                   : tab.slot == kNoSortTab ? ctr("abp.companion.settings.no_sort", "No Sort")
                                            : ctr("abp.companion.settings.memory", "Memory");
        p.text(truncated(p, title, std::max(0.f, tab.rect.w - 26)), tab.rect.x + 20, tab.rect.y + 8, Color::rgb(kTextColor));
        tab.widgets.render(p, mx, my, t);
    };
    for (std::size_t i = 0; i < mSettingsTabs.size(); ++i) {
        auto& tab = mSettingsTabs[i];
        if (static_cast<int>(i) == mSettingsOpen) continue;
        if (open && !(open->rect.bottom() < tab.rect.y || open->rect.y > tab.rect.y)) continue;
        drawTab(tab, false);
    }
    if (open) drawTab(mSettingsTabs[static_cast<std::size_t>(mSettingsOpen)], true);
    p.flush();

    sb::storageBackground(p, {mLeft, mTop, static_cast<float>(mLayout.imageWidth), static_cast<float>(mLayout.imageHeight)});
    int cols = mLayout.storageColumns;
    sb::slotsBackground(p, mLeft + 7, mTop + 17, cols, mVisibleCount / cols, mVisibleCount % cols);
    for (auto const& ps : mPlaced) {
        auto  i = static_cast<std::size_t>(ps.index);
        float x = mLeft + static_cast<float>(ps.pos.x), y = mTop + static_cast<float>(ps.pos.y);
        if (i < m.noSort.size() && m.noSort[i]) p.fillRect({x, y, 16, 16}, Color::rgb(m.noSortColor, 0x50 / 255.f));
        bool remembered = i < mMemoryStacks.size() && !mMemoryStacks[i].isNull();
        if (!m.slots[i].empty()) p.item(mSlotStacks[i], x, y);
        else if (remembered) p.item(mMemoryStacks[i], x, y);
        if (remembered) sb::icon(p, sb::Icon::Memory, x, y);
        if (mHover.area == SlotRef::Area::Storage && mHover.index == ps.index) p.fillRect({x, y, 16, 16}, sb::color::HOVER, Painter::Layer::Foreground);
    }
    auto title = ctr("abp.companion.settings.title", "Backpack Settings");
    p.text(truncated(p, title, static_cast<float>(mLayout.imageWidth - 15)), mLeft + 8, mTop + 6, Color::rgb(kTextColor));
    p.text(ctr("abp.companion.screen.inventory", "Inventory"), mLeft + static_cast<float>(mLayout.inventoryLabelX), mTop + static_cast<float>(mLayout.inventoryLabelY), Color::rgb(kTextColor));
    if (auto* player = currentPlayer()) {
        auto& inv = player->getInventory();
        for (int i = 0; i < 36; ++i) {
            auto const& pos = mLayout.playerSlots[static_cast<std::size_t>(i)];
            auto const& s   = inv.getItem(i);
            sb::slotFrame(p, mLeft + static_cast<float>(pos.x), mTop + static_cast<float>(pos.y));
            if (!s.isNull()) p.item(s, mLeft + static_cast<float>(pos.x), mTop + static_cast<float>(pos.y));
        }
    }
    if (mLayout.scroll) {
        auto bar   = mLayout.scrollBar();
        auto thumb = cs::scrollThumb(mLayout, m.storageSlots, mScroll);
        sb::scrollBar(p, {mLeft + bar.x, mTop + bar.y, static_cast<float>(bar.w), static_cast<float>(bar.h)},
                      {mLeft + thumb.x, mTop + thumb.y, static_cast<float>(thumb.w), static_cast<float>(thumb.h)});
    }
    p.flush();

    // Tooltips: widgets, tab tooltips, hovered slot items.
    std::vector<TooltipLine> lines;
    if (open) open->widgets.tooltip(mx, my, lines);
    if (lines.empty()) {
        for (std::size_t i = 0; i < mSettingsTabs.size() && lines.empty(); ++i) {
            auto const& tab = mSettingsTabs[i];
            bool        isOpen = static_cast<int>(i) == mSettingsOpen;
            Rect        iconRect{tab.rect.x + 1, tab.rect.y + 4, 16, 16};
            if (isOpen ? !iconRect.contains(mx, my) : !tab.rect.contains(mx, my)) continue;
            switch (tab.slot) {
            case kBackTab:
                lines.push_back({ctr("abp.companion.settings.back_to_backpack", "Back To Backpack"), 0xFFFFFF});
                break;
            case kMainTab:
                if (!isOpen) lines.push_back({ctr("abp.companion.settings.backpack.tooltip", "Backpack Settings"), 0xFFFFFF});
                break;
            case kNoSortTab:
                lines.push_back({ctr("abp.companion.settings.no_sort.tooltip", "No Sort Slot Settings"), 0xFFFFFF});
                if (isOpen) {
                    lines.push_back({ctr("abp.companion.settings.no_sort.open1", "Allows selecting slots that are ignored by sorting"), 0xAAAAAA});
                    lines.push_back({ctr("abp.companion.settings.select_all_hint", "Select all / Unselect all = buttons"), 0xAAAAAA});
                    lines.push_back({ctr("abp.companion.settings.select_hint", "Select slot = left click/drag"), 0xAAAAAA});
                    lines.push_back({ctr("abp.companion.settings.unselect_hint", "Unselect slot = right click/drag"), 0xAAAAAA});
                } else {
                    lines.push_back({ctr("abp.companion.settings.no_sort.detail1", "Allows selecting slots that are ignored by sorting"), 0xAAAAAA});
                    lines.push_back({ctr("abp.companion.settings.open_tab_hint", "Open tab to modify slot settings"), 0xAAAAAA});
                }
                break;
            case kMemoryTab:
                lines.push_back({ctr("abp.companion.settings.memory.tooltip", "Slot Memory Settings"), 0xFFFFFF});
                lines.push_back({ctr("abp.companion.settings.memory.detail1", "Allows selecting slots that remember their contents and only allow matching stacks in them"), 0xAAAAAA});
                if (isOpen) {
                    lines.push_back({ctr("abp.companion.settings.select_all_hint", "Select all / Unselect all = buttons"), 0xAAAAAA});
                    lines.push_back({ctr("abp.companion.settings.select_hint", "Select slot = left click/drag"), 0xAAAAAA});
                    lines.push_back({ctr("abp.companion.settings.unselect_hint", "Unselect slot = right click/drag"), 0xAAAAAA});
                } else {
                    lines.push_back({ctr("abp.companion.settings.open_tab_hint", "Open tab to modify slot settings"), 0xAAAAAA});
                }
                break;
            default:
                break;
            }
        }
    }
    if (!lines.empty()) {
        drawTooltip(p, lines, mx, my);
        return;
    }
    if (mHover.area == SlotRef::Area::Storage) {
        auto i = static_cast<std::size_t>(mHover.index);
        ItemStack const* s = !m.slots[i].empty() ? &mSlotStacks[i] : (i < mMemoryStacks.size() && !mMemoryStacks[i].isNull() ? &mMemoryStacks[i] : nullptr);
        if (s) drawTooltip(p, itemTooltipLines(*s), mx, my);
    }
}

bool BackpackScreenLayer::settingsMouse(cs::BackpackScreenModel const& m, MouseButton button, bool down, float x, float y) {
    if (!down) {
        mDragButton = -1;
        mDraggingBar = false;
        return true;
    }
    if (mSettingsOpen >= 0 && mSettingsTabs[static_cast<std::size_t>(mSettingsOpen)].widgets.mouseDown(button, x, y)) return true;
    for (std::size_t i = 0; i < mSettingsTabs.size(); ++i) {
        auto& tab = mSettingsTabs[i];
        Rect  iconRect{tab.rect.x + 1, tab.rect.y + 4, 16, 16};
        bool  isOpen = static_cast<int>(i) == mSettingsOpen;
        if (!iconRect.contains(x, y)) {
            if (isOpen && tab.rect.contains(x, y)) return true;
            continue;
        }
        if (button != MouseButton::Left) return true;
        if (tab.slot == kBackTab) {
            showSettings(false);
            return true;
        }
        if (isOpen) {
            mSettingsOpen = -1;
            tab.widgets.widgets.clear();
        } else {
            if (mSettingsOpen >= 0) mSettingsTabs[static_cast<std::size_t>(mSettingsOpen)].widgets.widgets.clear();
            mSettingsOpen = static_cast<int>(i);
            buildSettingsTab(m, tab);
        }
        return true;
    }
    if (mLayout.scroll && button == MouseButton::Left) {
        auto bar = mLayout.scrollBar();
        Rect r{mLeft + static_cast<float>(bar.x), mTop + static_cast<float>(bar.y), static_cast<float>(bar.w), static_cast<float>(bar.h)};
        if (r.contains(x, y)) {
            mDraggingBar = true;
            mDragY       = y;
            return true;
        }
    }
    if (mHover.area == SlotRef::Area::Storage) {
        int b         = button == MouseButton::Right ? 1 : (button == MouseButton::Left ? 0 : -1);
        mDragButton   = b;
        mDragLastSlot = mHover.index;
        settingsSlotClick(mHover.index, b);
    }
    return true;
}
// ---- screen host ----------------------------------------------------------------------------------------------
// The native screen lives in a scene of its own (a common dialog scene that is never drawn
// natively; BEI's recipe screen does the same): it gives the companion focus and a free cursor,
// keeps the world rendered behind it, and no vanilla container screen exists at any time. The
// server keeps the menu session (MenuManager "virtual" presentation) and is told when the player
// closes the screen.

using Clock = std::chrono::steady_clock;

struct SceneState {
    std::shared_ptr<AbstractScene> scene;
    std::shared_ptr<AbstractScene> retired;
    Clock::time_point              openedAt;
    bool                           seen         = false;
    bool                           closing      = false; ///< we asked the game to pop it
    bool                           popScheduled = false;
    std::uint32_t                  closedSession = 0; ///< session the player closed (don't reopen)
};
SceneState gScene;

ScreenView* ownView() {
    if (!gScene.scene) return nullptr;
    auto* scene = static_cast<UIScene*>(gScene.scene.get()); // the dialog factory creates UIScenes
    std::unique_ptr<ScreenView> const& view = scene->mScreenView;
    return view.get();
}

bool owns(UIScene const* scene) { return gScene.scene && gScene.scene.get() == scene; }

ISceneStack* sceneStack() {
    auto* client = currentClient();
    if (!client) return nullptr;
    return client->getSceneFactory().getCurrentSceneStack().get();
}

void dropScene(bool byPlayer) {
    auto const* m = net::screenModel();
    if (byPlayer && m) {
        // ESC (handled natively by the dialog) or the game dropped the scene: close the menu.
        gScene.closedSession = m->session;
        net::sendScreenAction({{"action", "close"}});
    }
    if (gScene.scene) gScene.retired = std::move(gScene.scene);
    gScene.scene.reset();
    gScene.seen         = false;
    gScene.closing      = false;
    gScene.popScheduled = false;
    gLayer.resetFocus();
}

void pushScene() {
    auto* client = currentClient();
    auto  scene  = client ? client->getSceneFactory().createCommonDialogInfoScreen("AlleviatedBackpacks", "") : nullptr;
    if (!scene) {
        log().error("native screen: could not create the screen scene");
        return;
    }
    auto retired    = std::move(gScene.retired);
    auto closed     = gScene.closedSession;
    gScene          = {};
    gScene.retired  = std::move(retired);
    gScene.closedSession = closed;
    gScene.scene    = std::move(scene);
    gScene.openedAt = Clock::now();
    client->getSceneFactory().getCurrentSceneStack()->pushScreen(gScene.scene, false);
}

/// Per-frame: push the scene for a new screen model, pop it when the model is gone.
void watch() {
    gScene.retired.reset();
    auto const* shown = net::displayModel();
    bool        want  = shown && shown->session != gScene.closedSession && inLevel();
    if (want && !gScene.scene) pushScene();
    if (!gScene.scene) return;
    auto* stack = sceneStack();
    if (!stack) return;
    bool top = stack->getTopScene() == gScene.scene.get();
    if (top) gScene.seen = true;
    if (!want && !gScene.closing) {
        gScene.closing = true;
    }
    if (gScene.closing && top && !gScene.popScheduled) {
        stack->schedulePopScreen(1);
        gScene.popScheduled = true;
    }
    // Pushed but never made it to the top (dropped during a transition): give up.
    if (!top && !gScene.seen && Clock::now() - gScene.openedAt > std::chrono::seconds(3)) dropScene(true);
}

bool gLoggedFirst = false;

void requestClose() {
    auto const* m = net::screenModel();
    if (!m) return;
    gScene.closedSession = m->session; // never reopen this session's scene
    net::sendScreenAction({{"action", "close"}});
    gScene.closing = true;
    gLayer.resetFocus();
}

// Our scene's view is drawn by the companion only. Highest priority: outermost hook, so
// LeviLamina's Before/AfterUIRenderEvent never fire for it and UiRoot renders exactly once.
LL_TYPE_INSTANCE_HOOK(AbpNativeScreenViewHook, ll::memory::HookPriority::Highest, ScreenView, &ScreenView::render, void, ::UIRenderContext& uiRenderContext) {
    if (!gScene.scene || this != ownView()) return origin(uiRenderContext);
    try {
        watch();
        if (!gLoggedFirst && net::displayModel()) {
            gLoggedFirst = true;
            log().info("companion: drawing the native backpack screen (session {})", net::displayModel()->session);
        }
        gLayer.rendering = !gScene.closing;
        UiRoot::get().renderFrame(*this, reinterpret_cast<MinecraftUIRenderContext&>(uiRenderContext));
    } catch (std::exception const& e) {
        log().error("native backpack screen failed: {}", e.what());
    }
    gLayer.rendering = false;
}

LL_TYPE_INSTANCE_HOOK(
    AbpNativeSceneExitHook,
    ll::memory::HookPriority::Normal,
    UIScene,
    &UIScene::$onScreenExit,
    void,
    bool                           isPopping,
    bool                           doScreenTransitions,
    std::shared_ptr<AbstractScene> pushedScene
) {
    bool mine = owns(this);
    origin(isPopping, mine ? false : doScreenTransitions, std::move(pushedScene));
    if (mine && isPopping && owns(this)) dropScene(!gScene.closing);
}

LL_TYPE_INSTANCE_HOOK(
    AbpNativeSceneEntranceHook,
    ll::memory::HookPriority::Normal,
    UIScene,
    &UIScene::$onScreenEntrance,
    void,
    bool isRevisiting,
    bool doScreenTransitions
) {
    origin(isRevisiting, owns(this) ? false : doScreenTransitions);
}

LL_TYPE_INSTANCE_HOOK(AbpNativeSceneBehindHook, ll::memory::HookPriority::Normal, UIScene, &UIScene::$renderGameBehind, bool) {
    if (owns(this)) return true;
    return origin();
}

LL_TYPE_INSTANCE_HOOK(AbpNativeSceneHurtHook, ll::memory::HookPriority::Normal, UIScene, &UIScene::$closeOnPlayerHurt, bool) {
    if (owns(this)) return false;
    return origin();
}

class NativeScreenHost {
public:
    void install() {
        TextInput::install();
        AbpNativeScreenViewHook::hook();
        AbpNativeSceneExitHook::hook();
        AbpNativeSceneEntranceHook::hook();
        AbpNativeSceneBehindHook::hook();
        AbpNativeSceneHurtHook::hook();
        auto& bus = ll::event::EventBus::getInstance();
        // Pushes the scene when a screen opens while another view is on top (e.g. the HUD).
        mRender = bus.emplaceListener<ll::event::AfterUIRenderEvent>([](auto&) {
            try {
                watch();
            } catch (std::exception const& e) {
                log().error("native screen host failed: {}", e.what());
            }
        });
        mExit = bus.emplaceListener<ll::event::ClientExitLevelEvent>([](auto&) {
            if (gScene.scene) dropScene(false); // the game flushes its scene stack itself
            gScene.closedSession = 0;
        });
        net::onScreenChanged([] {
            if (!net::screenModel()) gLoggedFirst = false;
        });
    }
    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        for (auto* l : {&mRender, &mExit}) {
            if (*l) bus.removeListener(*l);
            l->reset();
        }
        if (gScene.scene) {
            if (auto* stack = sceneStack(); stack && stack->getTopScene() == gScene.scene.get()) stack->schedulePopScreen(1);
            dropScene(false);
        }
        gScene = {};
        AbpNativeSceneHurtHook::unhook();
        AbpNativeSceneBehindHook::unhook();
        AbpNativeSceneEntranceHook::unhook();
        AbpNativeSceneExitHook::unhook();
        AbpNativeScreenViewHook::unhook();
        TextInput::uninstall();
    }

private:
    ll::event::ListenerPtr mRender;
    ll::event::ListenerPtr mExit;
};

} // namespace
bool nativeScreenActive() { return gLayer.rendering; }

} // namespace abp::client::ui

ABP_REGISTER_MODULE_ORDERED(abp::client::ui::NativeScreenHost, abp::ModuleOrder::Feature + 102);

#endif // ABP_CLIENT
