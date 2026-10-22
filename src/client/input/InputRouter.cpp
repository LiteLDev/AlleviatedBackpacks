#ifdef ABP_CLIENT

// Input routing: raw mouse/keyboard events (window thread) -> companion layers (client thread),
// key bindings (Hotkeys.cpp) and vanilla slot hover (SlotHover.cpp). Ported from BEI.

#include "client/input/InputRouter.h"

#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <mutex>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/client/ClientExitLevelEvent.h"
#include "ll/api/event/input/KeyInputEvent.h"
#include "ll/api/event/input/MouseInputEvent.h"
#include "ll/api/event/render/UIRenderEvent.h"
#include "ll/api/memory/Hook.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/game/MinecraftGame.h"
#include "mc/client/gui/GuiData.h"
#include "mc/deps/input/HIDController.h"
#include "mc/deps/input/MouseAction.h"

#include "client/input/InputState.h"
#include "client/ui/UiRoot.h"
#include "mod/Log.h"
#include "mod/Module.h"

extern "C" __declspec(dllimport) short __stdcall GetAsyncKeyState(int vKey);
#pragma comment(lib, "user32")

namespace abp::client::input {

namespace detail {

namespace {

constexpr std::size_t MAX_QUEUED = 1024;

std::mutex                         gQueueMutex;
std::vector<std::function<void()>> gQueue;

constexpr std::uint64_t    NO_MOUSE = ~std::uint64_t{0};
std::atomic<std::uint64_t> gPendingMouse{NO_MOUSE};
std::atomic<std::uint64_t> gLastMouse{NO_MOUSE};

std::uint64_t packMouse(float x, float y) {
    return (std::uint64_t{std::bit_cast<std::uint32_t>(x)} << 32) | std::bit_cast<std::uint32_t>(y);
}
float unpackX(std::uint64_t p) { return std::bit_cast<float>(static_cast<std::uint32_t>(p >> 32)); }
float unpackY(std::uint64_t p) { return std::bit_cast<float>(static_cast<std::uint32_t>(p)); }

constexpr std::size_t                      BUTTONS = 3;
std::array<std::atomic<bool>, BUTTONS>     gHeldButtons{};
std::array<std::atomic<bool>, 256>         gHeldKeys{};
std::array<std::atomic<bool>, 256>         gKeysDown{};
std::array<std::atomic<std::int64_t>, 256> gKeyDownTime{};
constexpr std::int64_t                     REPEAT_WINDOW_MS = 1100;

ll::event::ListenerPtr gMouseDedupListener;
ll::event::ListenerPtr gMouseListener;
ll::event::ListenerPtr gKeyListener;
ll::event::ListenerPtr gAfterRenderListener;
ll::event::ListenerPtr gExitLevelListener;

void runQueued() {
    std::vector<std::function<void()>> work;
    {
        std::lock_guard lock{gQueueMutex};
        work.swap(gQueue);
    }
    for (auto& fn : work) {
        try {
            fn();
        } catch (std::exception const& e) {
            log().error("companion input handler failed: {}", e.what());
        } catch (...) {
            log().error("companion input handler failed");
        }
    }
}

// --- hit testing for the window thread ----------------------------------------------------------

std::atomic<std::uint64_t> gHitMouse{NO_MOUSE};

constexpr int GRID_CELLS = 64;
struct HitGrid {
    std::atomic<float>                                                   originX{0}, originY{0}, cell{0};
    std::atomic<int>                                                     cols{0}, rows{0};
    std::array<std::atomic<std::uint64_t>, GRID_CELLS * GRID_CELLS / 64> bits{};
};
HitGrid gGrid;

void refreshHitGrid(bool live) {
    auto& root   = ui::UiRoot::get();
    auto& screen = root.lastFrame().screen;
    if (!live || screen.empty()) {
        gGrid.cols = 0;
        return;
    }
    static double builtFor = -1;
    if (gGrid.cols.load() > 0 && root.lastFrame().time == builtFor) return;
    builtFor   = root.lastFrame().time;
    float cell = std::max(screen.w, screen.h) / GRID_CELLS;
    int   cols = std::min(GRID_CELLS, static_cast<int>(std::ceil(screen.w / cell)));
    int   rows = std::min(GRID_CELLS, static_cast<int>(std::ceil(screen.h / cell)));
    std::array<std::uint64_t, GRID_CELLS * GRID_CELLS / 64> bits{};
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            float x = screen.x + (static_cast<float>(c) + 0.5f) * cell;
            float y = screen.y + (static_cast<float>(r) + 0.5f) * cell;
            if (root.contains(x, y)) {
                int i                                   = r * GRID_CELLS + c;
                bits[static_cast<std::size_t>(i / 64)] |= std::uint64_t{1} << (i % 64);
            }
        }
    }
    for (std::size_t i = 0; i < bits.size(); ++i) gGrid.bits[i] = bits[i];
    gGrid.originX = screen.x;
    gGrid.originY = screen.y;
    gGrid.cell    = cell;
    gGrid.rows    = rows;
    gGrid.cols    = cols;
}

bool overUiAt(float x, float y) {
    auto& snap = snapshot();
    if (auto packed = gHitMouse.load(); packed != NO_MOUSE) {
        if (std::abs(unpackX(packed) - x) <= 1.f && std::abs(unpackY(packed) - y) <= 1.f) return snap.overUi.load();
    }
    int   cols = gGrid.cols.load();
    float cell = gGrid.cell.load();
    if (cols <= 0 || cell <= 0) return snap.overUi.load();
    int c = static_cast<int>(std::floor((x - gGrid.originX.load()) / cell));
    int r = static_cast<int>(std::floor((y - gGrid.originY.load()) / cell));
    if (c < 0 || r < 0 || c >= cols || r >= gGrid.rows.load()) return false;
    int i = r * GRID_CELLS + c;
    return (gGrid.bits[static_cast<std::size_t>(i / 64)].load() >> (i % 64)) & 1u;
}

void refreshHitSnapshot() {
    auto& snap      = snapshot();
    auto& root      = ui::UiRoot::get();
    auto& frame     = root.lastFrame();
    bool  live      = snap.inLevel.load() && snap.cursorScreen.load();
    snap.overUi     = live && root.contains(frame.mouseX, frame.mouseY);
    snap.textFocus  = live && root.wantsTextInput();
    gHitMouse       = packMouse(frame.mouseX, frame.mouseY);
    std::array<std::uint64_t, 4> keys{};
    if (live) {
        for (int vk = 1; vk < 256; ++vk) {
            if (root.capturesKey(vk)) keys[static_cast<std::size_t>(vk / 64)] |= std::uint64_t{1} << (vk % 64);
        }
    }
    for (std::size_t i = 0; i < keys.size(); ++i) snap.capturedKeys[i] = keys[i];
}

// --- press/release de-duplication ------------------------------------------------------------------
// A mouse button must reach the game and our layers as exactly one press and one release. The game
// can feed the same transition to MouseDevice::feed more than once in a frame: it reads the mouse
// through GDK GameInput readings per device, so a button state reported by several devices arrives
// once per device (seen with LeviHeadlessInput's virtual GameInput mouse, which reports its buttons
// for every mouse device, on a PC with two HID mice: every click came as down, down, up, up). A press
// while the button is already down, or a release while it is up, is such a duplicate: it is cancelled
// before anyone sees it (highest priority; a cancelled event stops the listener chain and the game).
enum ButtonState : std::uint8_t { Unknown, Up, Down };
constexpr std::size_t                                MOUSE_ACTIONS = MouseAction::ActionX2 + 1;
std::array<std::atomic<std::uint8_t>, MOUSE_ACTIONS> gButtonState{};

bool isMouseButton(int action) {
    return action >= MouseAction::ActionLeft && action <= MouseAction::ActionX2 && action != MouseAction::ActionWheel;
}

void onMouseDedup(ll::event::MouseInputEvent& ev) {
    int action = static_cast<int>(ev.actionButtonId());
    if (!isMouseButton(action)) return;
    std::uint8_t state = ev.buttonData() == MouseAction::DataDown ? Down : Up;
    if (gButtonState[static_cast<std::size_t>(action)].exchange(state) == state) ev.cancel();
}

/// Forgets the button states (focus lost, level left): the next press or release always counts.
void resetButtonState() {
    for (auto& b : gButtonState) b = Unknown;
}

void releaseHeld(bool notifyLayers) {
    resetButtonState();
    for (std::size_t i = 0; i < BUTTONS; ++i) {
        if (!gHeldButtons[i].exchange(false) || !notifyLayers) continue;
        auto button = static_cast<ui::MouseButton>(i);
        post([button] {
            auto& frame = ui::UiRoot::get().lastFrame();
            ui::UiRoot::get().dispatchMouseButton(button, false, frame.mouseX, frame.mouseY);
        });
    }
    for (std::size_t vk = 0; vk < gHeldKeys.size(); ++vk) {
        if (!gHeldKeys[vk].exchange(false) || !notifyLayers) continue;
        post([vk] { ui::UiRoot::get().dispatchKey(static_cast<int>(vk), false); });
    }
    for (auto& key : gKeysDown) key = false;
}

void onClientUpdate(ClientInstance& client) {
    auto& snap        = snapshot();
    snap.invGuiScale  = client.getGuiData()->mInvGuiScale;
    bool inLevel      = client.getLevel() != nullptr;
    bool wasCursor    = snap.cursorScreen.load();
    bool cursor       = inLevel && !client.getMouseGrabbed() && client.getScreenName() != "hud_screen";
    snap.inLevel      = inLevel;
    snap.cursorScreen = cursor;
    if (wasCursor && !cursor) clearSlotHover();

    if (auto packed = gPendingMouse.exchange(NO_MOUSE); packed != NO_MOUSE) {
        ui::UiRoot::get().setMouse(unpackX(packed), unpackY(packed));
        if (cursor) ui::UiRoot::get().dispatchMouseMove(unpackX(packed), unpackY(packed));
    }
    runQueued();
    refreshHitSnapshot();
    refreshHitGrid(inLevel && cursor);
}

// --- event handlers (window thread) ----------------------------------------------------------------

void onMouse(ll::event::MouseInputEvent& ev) {
    auto& snap   = snapshot();
    float scale  = snap.invGuiScale.load();
    float x      = static_cast<float>(ev.x()) * scale;
    float y      = static_cast<float>(ev.y()) * scale;
    auto  action = static_cast<int>(ev.actionButtonId());

    switch (action) {
    case MouseAction::ActionMove:
        gLastMouse    = packMouse(x, y);
        gPendingMouse = packMouse(x, y);
        return;
    case MouseAction::ActionMoveRelative:
        return;
    case MouseAction::ActionWheel: {
        if (auto last = gLastMouse.load(); last != NO_MOUSE) x = unpackX(last), y = unpackY(last);
        if (!snap.cursorScreen.load() || !overUiAt(x, y)) return;
        float delta = static_cast<float>(ev.buttonData());
        post([x, y, delta] { ui::UiRoot::get().dispatchScroll(x, y, delta); });
        ev.cancel();
        return;
    }
    default:
        break;
    }

    ui::MouseButton button;
    switch (action) {
    case MouseAction::ActionLeft:
        button = ui::MouseButton::Left;
        break;
    case MouseAction::ActionRight:
        button = ui::MouseButton::Right;
        break;
    case MouseAction::ActionMiddle:
        button = ui::MouseButton::Middle;
        break;
    default:
        return;
    }
    auto& held = gHeldButtons[static_cast<std::size_t>(button)];
    bool  down = ev.buttonData() == MouseAction::DataDown;
    // (Repeated downs / ups of the same button never get here: onMouseDedup cancels them.)
    bool  live = snap.inLevel.load() && snap.cursorScreen.load();

    bool consume;
    if (down) {
        consume = live && overUiAt(x, y);
        held    = consume;
        if (!consume && snap.inLevel.load()) onHotkeyMouseDown(button);
        if (!live) return;
    } else {
        consume = held.exchange(false);
        if (!consume && !live) return;
    }
    gLastMouse    = packMouse(x, y);
    gPendingMouse = packMouse(x, y);
    post([button, down, x, y] { ui::UiRoot::get().dispatchMouseButton(button, down, x, y); });
    if (consume) ev.cancel();
}

void onKey(ll::event::KeyInputEvent& ev) {
    auto& snap            = snapshot();
    snap.vanillaTextFocus = static_cast<bool>(ev.controller().mTextboxIsFocused);
    int  vk               = ev.keyCode();
    bool down             = ev.isDown();
    if (vk < 0 || vk >= static_cast<int>(gHeldKeys.size())) return;
    auto& held = gHeldKeys[static_cast<std::size_t>(vk)];
    auto  idx  = static_cast<std::size_t>(vk);
    auto  now =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    bool repeat = gKeysDown[idx].exchange(down) && down && now - gKeyDownTime[idx].load() < REPEAT_WINDOW_MS;
    if (down) gKeyDownTime[idx] = now;

    bool consume;
    if (down) {
        bool live = snap.inLevel.load() && snap.cursorScreen.load();
        consume   = live && (snap.textFocus.load() || snap.captured(vk));
        if (consume) held = true;
        else if (!repeat) onHotkeyKeyDown(vk);
    } else {
        consume = held.exchange(false);
    }
    if (!consume) return;
    post([vk, down] { ui::UiRoot::get().dispatchKey(vk, down); });
    ev.cancel();
}

} // namespace

Snapshot& snapshot() {
    static Snapshot instance;
    return instance;
}

LL_TYPE_INSTANCE_HOOK(
    CompanionClientUpdateHook,
    ll::memory::HookPriority::Normal,
    ClientInstance,
    &ClientInstance::$update,
    bool,
    bool isInitFinished
) {
    auto result = origin(isInitFinished);
    if (!isInitFinished) return result;
    try {
        onClientUpdate(*this);
    } catch (std::exception const& e) {
        log().error("companion input update failed: {}", e.what());
    }
    return result;
}

LL_TYPE_INSTANCE_HOOK(CompanionFocusLostHook, ll::memory::HookPriority::Normal, MinecraftGame, &MinecraftGame::$onAppFocusLost, void) {
    origin();
    releaseHeld(true);
    post([] { clearSlotHover(); });
}

} // namespace detail

void post(std::function<void()> fn) {
    std::lock_guard lock{detail::gQueueMutex};
    if (detail::gQueue.size() >= detail::MAX_QUEUED) return;
    detail::gQueue.push_back(std::move(fn));
}

bool shiftDown() { return (GetAsyncKeyState(0x10) & 0x8000) != 0; }
bool ctrlDown() { return (GetAsyncKeyState(0x11) & 0x8000) != 0; }
bool altDown() { return (GetAsyncKeyState(0x12) & 0x8000) != 0; }

/// Input module: hooks, listeners, key bindings (load: KeyRegistry, before the game builds its
/// keyboard layout).
class InputRouterModule {
public:
    void load() { detail::registerHotkeys(); }
    void install() {
        using namespace detail;
        auto& bus      = ll::event::EventBus::getInstance();
        gMouseDedupListener =
            bus.emplaceListener<ll::event::MouseInputEvent>(onMouseDedup, ll::event::EventPriority::Highest);
        gMouseListener = bus.emplaceListener<ll::event::MouseInputEvent>(onMouse);
        gKeyListener   = bus.emplaceListener<ll::event::KeyInputEvent>(onKey, ll::event::EventPriority::Low);
        gAfterRenderListener = bus.emplaceListener<ll::event::AfterUIRenderEvent>(
            [](ll::event::AfterUIRenderEvent&) { refreshHitSnapshot(); },
            ll::event::EventPriority::Low
        );
        gExitLevelListener = bus.emplaceListener<ll::event::ClientExitLevelEvent>([](ll::event::ClientExitLevelEvent&) {
            auto& snap        = snapshot();
            snap.inLevel      = false;
            snap.cursorScreen = false;
            snap.overUi       = false;
            snap.textFocus    = false;
            releaseHeld(false);
            clearSlotHover();
        });
        CompanionClientUpdateHook::hook();
        CompanionFocusLostHook::hook();
        installSlotHover();
        installHotkeys();
    }
    void uninstall() {
        using namespace detail;
        uninstallHotkeys();
        uninstallSlotHover();
        CompanionFocusLostHook::unhook();
        CompanionClientUpdateHook::unhook();
        auto& bus = ll::event::EventBus::getInstance();
        for (auto* l : {&gMouseDedupListener, &gMouseListener, &gKeyListener, &gAfterRenderListener, &gExitLevelListener}) {
            if (*l) bus.removeListener(*l);
            l->reset();
        }
        releaseHeld(false);
        std::lock_guard lock{gQueueMutex};
        gQueue.clear();
    }
};

/// UiRoot as a module (installed before the input router).
class UiRootModule {
public:
    void install() { ui::UiRoot::get().install(); }
    void uninstall() { ui::UiRoot::get().uninstall(); }
};

} // namespace abp::client::input

ABP_REGISTER_MODULE_ORDERED(abp::client::input::UiRootModule, abp::ModuleOrder::Feature + 100);
ABP_REGISTER_MODULE_ORDERED(abp::client::input::InputRouterModule, abp::ModuleOrder::Feature + 101);

#endif // ABP_CLIENT
