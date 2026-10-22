#ifdef ABP_CLIENT

// Right-click stashing, client side (see Stash.h): server queries, the vanilla screen hooks
// (right click, slot signs, tooltip) and the drawing helpers the native screen uses.

#include "client/input/Stash.h"

#include <array>
#include <cmath>
#include <format>
#include <chrono>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/client/ClientExitLevelEvent.h"
#include "ll/api/event/render/UIRenderEvent.h"
#include "ll/api/event/world/ClientLevelTickEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/gui/controls/UIControl.h"
#include "mc/client/gui/controls/renderers/InventoryItemRenderOwnerData.h"
#include "mc/client/gui/controls/renderers/InventoryItemRenderer.h"
#include "mc/client/gui/screens/ScreenController.h"
#include "mc/client/gui/screens/ScreenView.h"
#include "mc/client/gui/screens/controllers/ContainerScreenController.h"
#include "mc/client/gui/screens/controllers/CraftingScreenController.h"
#include "mc/client/gui/screens/controllers/FurnaceScreenController.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/renderer/screen/MinecraftUIRenderContext.h"
#include "mc/network/NetEventCallback.h"
#include "mc/network/ServerNetworkHandler.h"
#include "mc/world/PlayerUIContainer.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/ItemStackBase.h"

#include "client/Client.h"
#include "client/input/InputRouter.h"
#include "client/input/InputState.h"
#include "client/net/ClientNet.h"
#include "client/tooltip/ContentsTooltip.h"
#include "client/ui/UiRoot.h"
#include "client/ui/gfx/Painter.h"
#include "client/ui/screen/BackpackScreen.h"
#include "game/BackpackItem.h"
#include "mod/Log.h"
#include "net/Packets.h"

namespace abp::client::input::stash {

namespace {
using Clock = std::chrono::steady_clock;

constexpr double        kRefreshEvery = 0.5;  ///< 10 ticks: slot contents / filters may change
constexpr double        kMinGap       = 0.1;  ///< at most 10 queries per second
constexpr std::uint32_t kGreen        = 0x55FF55; // ChatFormatting.GREEN
constexpr std::uint32_t kYellow       = 0xFFFF00;

double now() {
    static auto const start = Clock::now();
    return std::chrono::duration<double>(Clock::now() - start).count();
}

std::string keyOf(std::string_view area, int slot) { return std::string(area) + ":" + std::to_string(slot); }

/// Identity of a slot's content for the cache: item, aux, count and backpack uuid.
std::string fingerprint(ItemStack const& s) {
    if (s.isNull()) return {};
    auto fp = s.getTypeName() + "#" + std::to_string(s.getAuxValue()) + "x" + std::to_string(s.mCount);
    if (game::isBackpack(s)) fp += "@" + game::backpackUuid(s).value_or(std::string{});
    return fp;
}

ItemStack const* cursorItem() {
    auto* player = currentPlayer();
    if (!player) return nullptr;
    auto const& c = player->mPlayerUIContainer->getItem(0);
    return c.isNull() ? nullptr : &c;
}

struct Entry {
    std::string fp;
    Result      result = Result::None;
};

struct State {
    std::string                                  resultsCursor; ///< cursor the results are for
    std::unordered_map<std::string, Entry>       results;
    std::unordered_map<std::string, std::string> wanted; ///< slots seen since the last query
    std::unordered_map<std::string, std::string> sent;   ///< slots of the pending query
    std::string                                  sentCursor;
    int                                          q        = 0;
    double                                       lastSent = -10;
    std::size_t                                  loggedCount = ~std::size_t{0};
};
State gState; // client thread

void reset() { gState = State{}; }

/// Sends a query when the cursor or a shown slot changed, or every 10 ticks.
void pump() {
    auto const* cursor = cursorItem();
    if (!cursor || !net::serverReady()) {
        if (!gState.results.empty() || !gState.wanted.empty()) reset();
        return;
    }
    if (gState.wanted.empty()) return;
    double t   = now();
    auto   cfp = fingerprint(*cursor);
    bool   need = t - gState.lastSent >= kRefreshEvery || cfp != gState.sentCursor;
    if (!need) {
        for (auto const& [key, fp] : gState.wanted) {
            auto it = gState.sent.find(key);
            if (it == gState.sent.end() || it->second != fp) {
                need = true;
                break;
            }
        }
    }
    if (!need || t - gState.lastSent < kMinGap) return;
    nlohmann::json slots = nlohmann::json::array();
    for (auto const& [key, fp] : gState.wanted) {
        auto colon = key.find(':');
        slots.push_back({{"area", key.substr(0, colon)}, {"slot", std::stoi(key.substr(colon + 1))}});
        if (slots.size() >= 256) break;
    }
    gState.sent       = std::move(gState.wanted);
    gState.wanted     = {};
    gState.sentCursor = cfp;
    gState.lastSent   = t;
    net::sendKeyAction(nlohmann::json{{"action", "stashQuery"}, {"q", ++gState.q}, {"slots", std::move(slots)}});
}

void onAnswer(nlohmann::json const& body) {
    if (body.value("q", -1) != gState.q) return; // an older query
    auto const& results = body.contains("results") && body["results"].is_object() ? body["results"] : nlohmann::json::object();
    gState.results.clear();
    std::size_t found = 0;
    for (auto const& [key, fp] : gState.sent) {
        Result r = Result::None;
        if (auto it = results.find(key); it != results.end() && it->is_number_integer()) {
            int v = it->get<int>();
            r     = v == 2 ? Result::Match : v == 1 ? Result::Space : Result::None;
        }
        if (r != Result::None) ++found;
        gState.results[key] = Entry{fp, r};
    }
    gState.resultsCursor = gState.sentCursor;
    if (found != gState.loggedCount) {
        gState.loggedCount = found;
        log().info("stash: {} of {} slots stashable for the cursor {}", found, gState.sent.size(), gState.sentCursor);
    }
}

bool creativeInventory() {
    auto* player = currentPlayer();
    auto* client = currentClient();
    if (!player || !client || !player->isCreative()) return false;
    return client->getScreenName().find("inventory_screen") != std::string::npos;
}

/// A vanilla collection slot -> the server location.
std::optional<std::pair<std::string_view, int>> locationOf(std::string const& c, int i) {
    if (i < 0) return std::nullopt;
    if ((c == "hotbar_items" || c == "inventory_items" || c == "combined_hotbar_and_inventory_items") && i < 36) {
        return std::pair<std::string_view, int>{"inventory", i};
    }
    if (c == "armor_items" && i < 4) return std::pair<std::string_view, int>{"armor", i};
    if (c == "offhand_items") return std::pair<std::string_view, int>{"offhand", 0};
    if (c == "container_items") return std::pair<std::string_view, int>{"container", i};
    return std::nullopt;
}

ItemStack copyOf(ItemStackBase const& base) {
    ItemStack s;
    static_cast<ItemStackBase&>(s) = base;
    return s;
}

/// The backpack whose contents the tooltip shows (SC getStashResultAndTooltip).
ItemStack const* tooltipBackpack(ItemStack const& inSlot) {
    if (inSlot.mCount == 1 && game::isBackpack(inSlot)) return &inSlot;
    auto const* held = cursorItem();
    if (held && game::isBackpack(*held)) return held;
    return nullptr;
}

/// SC renderStashSign: "+" at (x+10, y+8) on stash storages, "-" at (x+1, y) on items; green when
/// the storage already holds the item, yellow (0xFFFF00) for space only.
void signAt(ui::Painter& p, Result r, bool plus, float x, float y, float s) {
    if (r == Result::None) return;
    auto color = ui::Color::rgb(r == Result::Match ? kGreen : kYellow);
    if (plus) p.text("+", x + 10 * s, y + 8 * s, color, true, s);
    else p.text("-", x + 1 * s, y, color, true, s);
}

// ---- vanilla screens -----------------------------------------------------------------------------------

struct Mark {
    float  x, y, scale;
    Result result;
    bool   plus;
    double at;
};
std::unordered_map<std::string, Mark> gMarks; // drawn this frame (by slot)

/// Where the open container screen draws its slots (GUI units), logged once per screen for the
/// e2e harness ("stash: vanilla slots ...").
struct SlotRects {
    ContainerScreenController const*                          controller = nullptr;
    std::string                                               screen;
    double                                                    since    = 0;
    double                                                    lastNote = 0;
    double                                                    changed  = 0;
    int                                                       logs     = 0;
    bool                                                      dirty    = false;
    std::vector<std::pair<std::string, std::array<float, 3>>> rects;
};
SlotRects gRects;

/// Logs the slot positions of the open screen (again whenever they change, a few times per
/// screen): "stash: vanilla slots screen=<name> invGuiScale=<s> <area>:<slot>=<x>,<y>,<scale> ...".
void noteSlotRect(ContainerScreenController const* controller, std::string const& key, float x, float y, float s) {
    double t      = now();
    auto*  client = currentClient();
    auto   screen = client ? client->getScreenName() : std::string{};
    // A new screen (or the same controller object reused after a pause in drawing).
    if (gRects.controller != controller || gRects.screen != screen || t - gRects.lastNote > 0.5) {
        gRects = SlotRects{controller, screen, t, t};
    }
    gRects.lastNote = t;
    bool known      = false;
    for (auto& [k, r] : gRects.rects) {
        if (k != key) continue;
        known = true;
        if (std::abs(r[0] - x) > 0.5f || std::abs(r[1] - y) > 0.5f) r = {x, y, s}, gRects.dirty = true, gRects.changed = t;
    }
    if (!known) gRects.rects.emplace_back(key, std::array<float, 3>{x, y, s}), gRects.dirty = true, gRects.changed = t;
    // Screens slide in when they open: log once the positions stood still for a moment.
    if (!gRects.dirty || t - gRects.since < 0.8 || t - gRects.changed < 0.4 || gRects.logs >= 6) return;
    gRects.dirty = false;
    ++gRects.logs;
    std::string line;
    for (auto const& [k, r] : gRects.rects) line += std::format(" {}={:.1f},{:.1f},{:.2f}", k, r[0], r[1], r[2]);
    log().info("stash: vanilla slots screen={} invGuiScale={:.4f}{}", screen, detail::snapshot().invGuiScale.load(), line);
}
/// Right click with an item on the cursor on a vanilla slot (place one / take-place).
bool vanillaRightClick(ContainerScreenController const& controller, std::string const& collection, int index) {
    if (!active() || ui::nativeScreenActive()) return false;
    auto loc = locationOf(collection, index);
    if (!loc) return false;
    auto item = copyOf(controller._getItemStack(collection, index));
    if (item.isNull()) return false;
    if (!rightClick(loc->first, loc->second, item)) return false;
    log().info("stash: right click on {} #{} ({} {})", collection, index, loc->first, loc->second);
    return true;
}

std::int64_t gHandledPress = -1;

bool recentRightPress() {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
    auto at = detail::snapshot().rightPressMs.load();
    return at > 0 && ms - at < 400 && at != gHandledPress;
}

bool takeRightClick(ContainerScreenController const& controller, std::string const& collection, int index) {
    if (!recentRightPress()) return false;
    if (!vanillaRightClick(controller, collection, index)) return false;
    gHandledPress = detail::snapshot().rightPressMs.load();

    return true;
}
} // namespace

LL_TYPE_INSTANCE_HOOK(
    AbpStashPlaceOneHook,
    ll::memory::HookPriority::Normal,
    ContainerScreenController,
    &ContainerScreenController::$_handlePlaceOne,
    void,
    std::string const& collectionName,
    int                index
) {
    if (takeRightClick(*this, collectionName, index)) return;
    origin(collectionName, index);
}

LL_TYPE_INSTANCE_HOOK(
    AbpStashCraftingPlaceOneHook,
    ll::memory::HookPriority::Normal,
    CraftingScreenController,
    &CraftingScreenController::$_handlePlaceOne,
    void,
    std::string const& collectionName,
    int                index
) {
    if (takeRightClick(*this, collectionName, index)) return;
    origin(collectionName, index);
}

LL_TYPE_INSTANCE_HOOK(
    AbpStashFurnacePlaceOneHook,
    ll::memory::HookPriority::Normal,
    FurnaceScreenController,
    &FurnaceScreenController::$_handlePlaceOne,
    void,
    std::string const& collectionName,
    int                index
) {
    if (takeRightClick(*this, collectionName, index)) return;
    origin(collectionName, index);
}

// Some input paths swap / place through take-place; only a right click there is ours.
LL_TYPE_INSTANCE_HOOK(
    AbpStashTakePlaceHook,
    ll::memory::HookPriority::Normal,
    ContainerScreenController,
    &ContainerScreenController::$_handleTakePlace,
    int,
    std::string const& collectionName,
    int                collectionIndex,
    bool               allItems
) {
    if (!allItems && takeRightClick(*this, collectionName, collectionIndex)) return 0;
    return origin(collectionName, collectionIndex, allItems);
}

namespace {
/// The icon InventoryItemRenderer::render is drawing right now: `_render` gets its real screen
/// position (UIControl positions don't include scroll panel offsets).
struct PendingIcon {
    bool  active = false;
    bool  got    = false;
    float x = 0, y = 0;
};
PendingIcon gPendingIcon; // client (render) thread
/// The container screen controller of the view being rendered (BeforeUIRenderEvent).
ContainerScreenController const* gFrameController = nullptr;
} // namespace

LL_TYPE_INSTANCE_HOOK(
    AbpStashItemRenderInnerHook,
    ll::memory::HookPriority::Normal,
    InventoryItemRenderer,
    &InventoryItemRenderer::_render,
    void,
    ::IInventoryItemRendererRenderContext& renderContext,
    ::InventoryItemRenderOwnerData const&  owner,
    int                                    pass
) {
    if (gPendingIcon.active && !gPendingIcon.got) {
        gPendingIcon.x   = owner.mScreenAlignedX;
        gPendingIcon.y   = owner.mScreenAlignedY;
        gPendingIcon.got = true;
    }
    origin(renderContext, owner, pass);
}

// TEMP diagnostics: which vanilla handler a click with an item on the cursor runs.
namespace {
int gHandlerLogs = 0;
void logHandler(char const* what, std::string const& c, int i) {
    if (gHandlerLogs < 40 && cursorItem()) {
        ++gHandlerLogs;
        log().info("stash diag: {} {} #{}", what, c, i);
    }
}
} // namespace
LL_TYPE_INSTANCE_HOOK(AbpDiagPlaceAll, ll::memory::HookPriority::Normal, ContainerScreenController, &ContainerScreenController::$_handlePlaceAll, void, std::string const& c, int i) {
    logHandler("placeAll", c, i);
    origin(c, i);
}
LL_TYPE_INSTANCE_HOOK(AbpDiagAutoPlace, ll::memory::HookPriority::Normal, ContainerScreenController, &ContainerScreenController::_handleAutoPlace, void, int amount, std::string const& c, int i) {
    logHandler("autoPlace", c, i);
    origin(amount, c, i);
}
LL_TYPE_INSTANCE_HOOK(AbpDiagSelect, ll::memory::HookPriority::Normal, ContainerScreenController, &ContainerScreenController::$_handleSelectSlot, void, std::string const& c, int i) {
    logHandler("selectSlot", c, i);
    origin(c, i);
}
// Slot signs: every item icon of the open container screen reports its slot and position.
LL_TYPE_INSTANCE_HOOK(
    AbpStashItemRenderHook,
    ll::memory::HookPriority::Normal,
    InventoryItemRenderer,
    &InventoryItemRenderer::$render,
    void,
    ::MinecraftUIRenderContext& ctx,
    ::IClientInstance&          client,
    ::UIControl&                owner,
    int                         pass
) {
    auto const* controller = gFrameController; // only icons of the container screen's own view
    gPendingIcon           = PendingIcon{controller != nullptr && !ui::nativeScreenActive()};
    origin(ctx, client, owner, pass);
    auto drawn   = gPendingIcon;
    gPendingIcon = {};
    try {
        if (!drawn.active) return;
        if (!drawn.got) {
            // `_render` not reached (inlined path): the control's cached position.
            if (owner.mCachedPositionDirty) owner._setCachedPosition();
            drawn.x = owner.mCachedPosition.get().x;
            drawn.y = owner.mCachedPosition.get().y;
        }
        // The slot this icon belongs to (the callback's index is not the slot index; the
        // collection index is looked up by name).
        std::optional<std::pair<std::string, int>> slot;
        owner.findAllCollectionIndices([&](std::string const& name, int) {
            int index = owner.findCollectionIndex(name);
            if (!slot && locationOf(name, index)) slot = std::pair<std::string, int>{name, index};
        });
        if (!slot) return;
        auto  loc  = locationOf(slot->first, slot->second);
        auto  key  = keyOf(loc->first, loc->second);
        auto const& size = owner.mSize.get();
        float s    = size.x > 0 ? size.x / 16.f : 1.f;
        struct {
            float x, y;
        } pos{drawn.x, drawn.y}; // where the icon really is (scroll panels included)
        noteSlotRect(controller, key, pos.x, pos.y, s);
        if (!active()) return;
        auto item = copyOf(controller->_getVisualItemStack(slot->first, slot->second));
        if (item.isNull()) return;
        auto r = result(loc->first, loc->second, item);
        if (r == Result::None) return;
        if (auto hovered = hoveredVanillaSlot(); hovered && hovered->collection == slot->first && hovered->index == slot->second) return;
        gMarks[key] = Mark{pos.x, pos.y, s, r, game::isBackpack(item), now()};
    } catch (std::exception const& e) {
        log().error("stash sign failed: {}", e.what());
    }
}

namespace {
/// Draws the signs and the hovered slot's tooltip over vanilla container screens.
class StashOverlayLayer final : public ui::IUiLayer {
public:
    [[nodiscard]] char const* name() const override { return "abp.stash_overlay"; }
    [[nodiscard]] int         zOrder() const override { return 500; }
    [[nodiscard]] bool        visible(ui::FrameInfo const& f) const override {
        return f.isContainerScreen && !ui::nativeScreenActive() && active();
    }
    void render(ui::Painter& p, ui::FrameInfo const& f) override {
        double t = now();
        for (auto it = gMarks.begin(); it != gMarks.end();) {
            auto const& m = it->second;
            if (t - m.at > 0.1) {
                it = gMarks.erase(it);
                continue;
            }
            drawSign(p, m.result, m.plus, m.x, m.y, m.scale);
            ++it;
        }
        p.flush();
        auto hovered = hoveredVanillaSlot();
        if (!hovered) return;
        auto loc = locationOf(hovered->collection, hovered->index);
        if (!loc) return;
        drawTooltip(p, loc->first, loc->second, hoveredVanillaItem(), f.mouseX, f.mouseY);
    }

private:
    static void drawSign(ui::Painter& p, Result r, bool plus, float x, float y, float s) { signAt(p, r, plus, x, y, s); }
};
StashOverlayLayer gOverlay;
ABP_REGISTER_UI_LAYER(gOverlay);

/// True when a server-to-client packet really reached the client (not this process's server).
bool fromServer(NetEventCallback& callback) {
    auto handler = ll::service::getServerNetworkHandler();
    return !handler || static_cast<NetEventCallback*>(handler.as_ptr()) != &callback;
}

class StashInfoHandler : public ll::network::PacketHandlerBase<StashInfoHandler, abp::net::StashInfoPacket> {
public:
    void handlePacket(NetworkIdentifier const&, NetEventCallback& cb, abp::net::StashInfoPacket const& p) const {
        if (!fromServer(cb) || !p.valid()) return;
        onAnswer(p.body);
    }
};

std::vector<ll::event::ListenerPtr> gListeners;
} // namespace

// ---- API -----------------------------------------------------------------------------------------------

bool active() {
    if (!net::serverReady() || !cursorItem()) return false;
    return !creativeInventory();
}

Result result(std::string_view area, int slot, ItemStack const& item) {
    if (!active() || item.isNull()) return Result::None;
    auto key = keyOf(area, slot);
    auto fp  = fingerprint(item);
    if (gState.wanted.size() < 256 || gState.wanted.contains(key)) gState.wanted[key] = fp;
    if (gState.resultsCursor != fingerprint(*cursorItem())) return Result::None;
    auto it = gState.results.find(key);
    return it != gState.results.end() && it->second.fp == fp ? it->second.result : Result::None;
}

void drawSign(ui::Painter& p, Result r, ItemStack const& item, float x, float y, float scale) {
    if (r == Result::None || item.isNull()) return;
    signAt(p, r, game::isBackpack(item), x, y, scale);
}

bool drawTooltip(ui::Painter& p, std::string_view area, int slot, ItemStack const& item, float mouseX, float mouseY) {
    if (result(area, slot, item) == Result::None) return false;
    auto const* backpack = tooltipBackpack(item);
    if (!backpack) return false;
    tooltip::drawRightClickToAdd(p, *backpack, mouseX, mouseY);
    return true;
}

bool rightClick(std::string_view area, int slot, ItemStack const& item) {
    if (result(area, slot, item) == Result::None) return false;
    net::sendKeyAction(nlohmann::json{{"action", "stash"}, {"area", std::string(area)}, {"slot", slot}});
    // The server answers with the new inventory / cursor; ask again right away.
    gState.results.clear();
    gState.resultsCursor.clear();
    gState.sent.clear();
    gState.lastSent = -10;
    return true;
}

bool vanillaTooltipShown() {
    if (!active() || ui::nativeScreenActive()) return false;
    auto hovered = hoveredVanillaSlot();
    if (!hovered) return false;
    auto loc = locationOf(hovered->collection, hovered->index);
    if (!loc) return false;
    auto const& item = hoveredVanillaItem();
    return result(loc->first, loc->second, item) != Result::None && tooltipBackpack(item);
}

} // namespace abp::client::input::stash

namespace abp::client::input::detail {

void installStash() {
    using namespace stash;
    AbpStashPlaceOneHook::hook();
    AbpStashCraftingPlaceOneHook::hook();
    AbpStashFurnacePlaceOneHook::hook();
    AbpStashTakePlaceHook::hook();
    AbpStashItemRenderHook::hook();
    AbpDiagPlaceAll::hook();
    AbpDiagAutoPlace::hook();
    AbpDiagSelect::hook();
    AbpStashItemRenderInnerHook::hook();
    auto& bus = ll::event::EventBus::getInstance();
    gListeners.push_back(bus.emplaceListener<ll::event::ClientLevelTickEvent>([](auto&) { pump(); }));
    gListeners.push_back(bus.emplaceListener<ll::event::BeforeUIRenderEvent>([](ll::event::BeforeUIRenderEvent& ev) {
        auto const& c    = ev.screenView().mController;
        gFrameController = c && c->_isContainerScreen() ? static_cast<ContainerScreenController const*>(c.get()) : nullptr;
    }));
    gListeners.push_back(bus.emplaceListener<ll::event::AfterUIRenderEvent>([](auto&) { gFrameController = nullptr; }, ll::event::EventPriority::High));
    gListeners.push_back(bus.emplaceListener<ll::event::ClientExitLevelEvent>([](auto&) {
        reset();
        gMarks.clear();
    }));
}

void uninstallStash() {
    using namespace stash;
    auto& bus = ll::event::EventBus::getInstance();
    for (auto& l : gListeners) bus.removeListener(l);
    gListeners.clear();
    AbpStashItemRenderInnerHook::unhook();
    AbpDiagSelect::unhook();
    AbpDiagAutoPlace::unhook();
    AbpDiagPlaceAll::unhook();
    AbpStashItemRenderHook::unhook();
    AbpStashTakePlaceHook::unhook();
    AbpStashFurnacePlaceOneHook::unhook();
    AbpStashCraftingPlaceOneHook::unhook();
    AbpStashPlaceOneHook::unhook();
    reset();
    gMarks.clear();
}

} // namespace abp::client::input::detail

#endif // ABP_CLIENT
