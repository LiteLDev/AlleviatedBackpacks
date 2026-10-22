#pragma once

// Storage overlays of the native backpack screen: Sophisticated Core's `UpgradeInventoryControlBase`
// (upgrade-owned controls drawn over the storage slots, e.g. the Mob Catcher's captured mobs).
//
// An overlay may take over storage slots (`replacesSlot`: the host then draws neither their item
// nor their hover highlight, like SC's `replacesSlotRender`), draws above the slots (`render`),
// gets mouse clicks before the slots do (`mouseDown`), may show its own tooltip (`tooltip`) and
// draws the red error overlay of the slots it replaced (`errorOverlay`).
//
//     class MyOverlay : public abp::client::ui::StorageOverlay { ... };
//     ABP_REGISTER_STORAGE_OVERLAY([] { return std::make_unique<MyOverlay>(); });

#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "nlohmann/json.hpp"

#include "client/ui/Geometry.h"
#include "client/ui/UiLayer.h"
#include "core/screen/ScreenModel.h"
#include "mod/Registry.h"

namespace abp::client::ui {

class Painter;

struct StorageOverlayContext {
    core::screen::BackpackScreenModel const& model;
    /// Item position (16x16 icon top-left, absolute GUI units) of a storage slot that is shown;
    /// nullopt when the slot is scrolled out or hidden by the search filter.
    std::function<std::optional<Rect>(int index)> slotItemRect;
    /// Union of the shown storage slots' item areas (absolute GUI units; empty when none).
    Rect visibleStorage;
    /// Sends `{"action":"tab","slot":upgradeSlot,"data":data}` (an upgrade's tab action handler).
    std::function<void(int upgradeSlot, nlohmann::json data)> sendTabAction;
};

class StorageOverlay {
public:
    virtual ~StorageOverlay() = default;
    /// The host does not draw storage slot `index` (item, memory ghost, hover highlight).
    [[nodiscard]] virtual bool replacesSlot(StorageOverlayContext const& /*ctx*/, int /*index*/) const { return false; }
    /// Drawn above the storage slots (after a flush), below the cursor item and tooltips.
    virtual void render(Painter& /*p*/, StorageOverlayContext const& /*ctx*/, float /*mx*/, float /*my*/) {}
    /// A mouse button went down over the screen; return true to consume it (before slot clicks).
    virtual bool mouseDown(StorageOverlayContext const& /*ctx*/, MouseButton /*button*/, float /*x*/, float /*y*/) { return false; }
    /// Draws the overlay's tooltip at the mouse; return true when one was drawn.
    virtual bool tooltip(Painter& /*p*/, StorageOverlayContext const& /*ctx*/, float /*mx*/, float /*my*/) { return false; }
    /// Red error overlay for `errorSlots` (the host skips the slots this overlay replaces).
    virtual void errorOverlay(Painter& /*p*/, StorageOverlayContext const& /*ctx*/, std::vector<int> const& /*errorSlots*/) {}
    /// The screen closed (or another backpack opened): drop per-screen caches.
    virtual void reset() {}
};

using StorageOverlayFactory = std::function<std::unique_ptr<StorageOverlay>()>;
using StorageOverlayRegistry = StaticRegistry<StorageOverlayFactory>;

/// The overlay instances (created on first use, one per registered factory). Client thread.
[[nodiscard]] std::vector<std::unique_ptr<StorageOverlay>>& storageOverlays();

namespace detail {
struct StorageOverlayRegistrar {
    explicit StorageOverlayRegistrar(StorageOverlayFactory f) { StorageOverlayRegistry::add(std::move(f)); }
};
} // namespace detail

} // namespace abp::client::ui

/// Registers a storage overlay factory (see `abp::client::ui::StorageOverlay`).
#define ABP_REGISTER_STORAGE_OVERLAY(...)                                                                              \
    static ::abp::client::ui::detail::StorageOverlayRegistrar ABP_UNIQUE(abp_storage_overlay_)(__VA_ARGS__)
