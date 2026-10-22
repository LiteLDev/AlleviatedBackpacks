#pragma once

// Input routing for the client companion (ported from BEI's InputRouter).
//
// - Mouse/keyboard events arrive on the window thread. The router decides synchronously whether
//   to swallow an event (from the last rendered frame's hit tests) and runs the real handling on
//   the client thread (once per frame, from ClientInstance::update).
// - Mouse buttons and the wheel go to the UI layers (UiRoot::dispatch*); they are swallowed only
//   when the cursor is over a layer (a release is swallowed iff its press was). Mouse moves are
//   forwarded too (drags, hover).
// - Keys: while a layer has text focus every key goes to it; a layer may also capture specific
//   keys (`IUiLayer::capturesKey`, e.g. the native screen's B / [ / ] / Q / 1-9); everything else
//   goes to the key bindings (Hotkeys.cpp) and to vanilla.
// - Tracks the hovered vanilla container slot (SlotHover.cpp).

#include <functional>
#include <optional>
#include <string>

class ItemStack;

namespace abp::client::input {

/// Queue `fn` to run on the client thread at the next ClientInstance::update. Any thread.
void post(std::function<void()> fn);

/// The vanilla container slot under the cursor (client thread): collection name (e.g.
/// "inventory_items", "hotbar_items", "armor_items", "offhand_items", "container_items") and
/// index, plus its item. nullopt when none.
struct HoveredSlot {
    std::string collection;
    int         index = -1;
};
[[nodiscard]] std::optional<HoveredSlot> hoveredVanillaSlot();
/// The item in the hovered vanilla slot (empty stack when none). Valid until the next frame.
[[nodiscard]] ItemStack const& hoveredVanillaItem();

/// Physical modifier state (window thread view, readable anywhere).
[[nodiscard]] bool shiftDown();
[[nodiscard]] bool ctrlDown();
[[nodiscard]] bool altDown();

} // namespace abp::client::input
