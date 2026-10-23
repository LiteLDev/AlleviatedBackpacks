#ifdef ABP_CLIENT

// Tracks the vanilla container slot under the cursor (ported from BEI). Client thread.

#include <cstdint>
#include <optional>
#include <string>

#include "ll/api/memory/Hook.h"

#include "mc/client/gui/ViewRequest.h"
#include "mc/client/gui/screens/controllers/ContainerScreenController.h"
#include "mc/client/gui/screens/controllers/CraftingScreenController.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/ItemStackBase.h"

#include "client/input/InputRouter.h"
#include "client/input/InputState.h"
#include "client/net/ClientNet.h"
#include "game/BackpackItem.h"

namespace abp::client::input {

namespace detail {

namespace {
struct Hovered {
    std::uintptr_t owner = 0; ///< identity of the controller (never dereferenced)
    std::string    collection;
    int            index = -1;
    ItemStack      item;
};
Hovered gHovered;

bool ownSlot(std::string const& c, int i) {
    // inventory_items counts the main inventory from 0 (player slot 9).
    return (c == "hotbar_items" && i >= 0 && i < 9) || (c == "inventory_items" && i >= 0 && i < 27)
        || (c == "combined_hotbar_and_inventory_items" && i >= 0 && i < 36)
        || (c == "armor_items" && i == 1) || c == "offhand_items";
}

void read(ContainerScreenController const& controller) {
    auto const& base                                = controller._getVisualItemStack(gHovered.collection, gHovered.index);
    static_cast<ItemStackBase&>(gHovered.item) = base;
    snapshot().hoverBackpack = net::serverReady() && !gHovered.item.isNull() && game::isBackpack(gHovered.item) && gHovered.item.mCount == 1
                            && ownSlot(gHovered.collection, gHovered.index);
}

void hovered(ContainerScreenController const& controller, std::string const& name, int index) {
    gHovered.owner      = reinterpret_cast<std::uintptr_t>(&controller);
    gHovered.collection = name;
    gHovered.index      = index;
    read(controller);
}

void unhovered(ContainerScreenController const& controller, std::string const& name, int index) {
    if (gHovered.owner != reinterpret_cast<std::uintptr_t>(&controller) || gHovered.index != index || gHovered.collection != name) {
        return;
    }
    clearSlotHover();
}

ContainerScreenController const* gActiveController = nullptr; // the container screen ticking now

void ticked(ContainerScreenController const& controller) {
    gActiveController = &controller;
    if (gHovered.owner != reinterpret_cast<std::uintptr_t>(&controller) || gHovered.index < 0) return;
    read(controller);
}

void left(ContainerScreenController const& controller) {
    if (gActiveController == &controller) gActiveController = nullptr;
    if (gHovered.owner == reinterpret_cast<std::uintptr_t>(&controller)) clearSlotHover();
}
} // namespace

LL_TYPE_INSTANCE_HOOK(
    AbpContainerSlotHoveredHook,
    ll::memory::HookPriority::Normal,
    ContainerScreenController,
    &ContainerScreenController::$_onContainerSlotHovered,
    ::ui::ViewRequest,
    std::string const& collectionName,
    int                index
) {
    auto result = origin(collectionName, index);
    hovered(*this, collectionName, index);
    return result;
}

LL_TYPE_INSTANCE_HOOK(
    AbpCraftingSlotHoveredHook,
    ll::memory::HookPriority::Normal,
    CraftingScreenController,
    &CraftingScreenController::$_onContainerSlotHovered,
    ::ui::ViewRequest,
    std::string const& collectionName,
    int                index
) {
    auto result = origin(collectionName, index);
    hovered(*this, collectionName, index);
    return result;
}

LL_TYPE_INSTANCE_HOOK(
    AbpContainerSlotUnhoveredHook,
    ll::memory::HookPriority::Normal,
    ContainerScreenController,
    &ContainerScreenController::$_onContainerSlotUnhovered,
    ::ui::ViewRequest,
    std::string const& collectionName,
    int                index
) {
    auto result = origin(collectionName, index);
    unhovered(*this, collectionName, index);
    return result;
}

LL_TYPE_INSTANCE_HOOK(AbpContainerTickHook, ll::memory::HookPriority::Normal, ContainerScreenController, &ContainerScreenController::$tick, ::ui::DirtyFlag) {
    auto result = origin();
    ticked(*this);
    return result;
}

LL_TYPE_INSTANCE_HOOK(AbpCraftingTickHook, ll::memory::HookPriority::Normal, CraftingScreenController, &CraftingScreenController::$tick, ::ui::DirtyFlag) {
    auto result = origin();
    ticked(*this);
    return result;
}

LL_TYPE_INSTANCE_HOOK(AbpContainerLeaveHook, ll::memory::HookPriority::Normal, ContainerScreenController, &ContainerScreenController::$onLeave, void) {
    left(*this);
    origin();
}

// Never keep a pointer to a destroyed controller.
LL_TYPE_INSTANCE_HOOK(AbpContainerDtorHook, ll::memory::HookPriority::Normal, ContainerScreenController, &ContainerScreenController::$dtor, void) {
    left(*this);
    origin();
}

void clearSlotHover() {
    gHovered                 = {};
    snapshot().hoverBackpack = false;
}

ContainerScreenController const* activeContainerController() { return gActiveController; }
void                             forgetContainerController() { gActiveController = nullptr; }

void installSlotHover() {
    AbpContainerSlotHoveredHook::hook();
    AbpCraftingSlotHoveredHook::hook();
    AbpContainerSlotUnhoveredHook::hook();
    AbpContainerTickHook::hook();
    AbpCraftingTickHook::hook();
    AbpContainerLeaveHook::hook();
    AbpContainerDtorHook::hook();
}

void uninstallSlotHover() {
    AbpContainerDtorHook::unhook();
    AbpContainerLeaveHook::unhook();
    gActiveController = nullptr;
    AbpCraftingTickHook::unhook();
    AbpContainerTickHook::unhook();
    AbpContainerSlotUnhoveredHook::unhook();
    AbpCraftingSlotHoveredHook::unhook();
    AbpContainerSlotHoveredHook::unhook();
    clearSlotHover();
}

} // namespace detail

std::optional<HoveredSlot> hoveredVanillaSlot() {
    if (detail::gHovered.index < 0) return std::nullopt;
    return HoveredSlot{detail::gHovered.collection, detail::gHovered.index};
}

ItemStack const& hoveredVanillaItem() { return detail::gHovered.item; }

} // namespace abp::client::input

#endif // ABP_CLIENT
