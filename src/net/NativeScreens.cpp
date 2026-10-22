#include "net/NativeScreens.h"

#include <algorithm>
#include <chrono>
#include <optional>
#include <unordered_map>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerDisconnectEvent.h"

#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/Level.h"

#include "core/Appearance.h"
#include "core/Sorting.h"
#include "feature/gui/Gui.h"
#include "feature/gui/SbSettings.h"
#include "game/Backpack.h"
#include "game/ItemConv.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "net/Companion.h"
#include "net/Packets.h"
#include "ui/menu/BackpackMenu.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/inception/Inception.h"
#include "upgrade/stack/StackUpgrade.h"
#include "upgrade/tank/Columns.h"

namespace abp::net {

using core::screen::BackpackScreenModel;
using ui::MenuManager;
using Where      = MenuManager::SlotAddress::Where;
using ActionType = MenuManager::ActionType;

namespace {
constexpr int kRebuildEvery = 10;  ///< ticks between model rebuilds when the backpack did not change
constexpr int kErrorTicks   = 60;  ///< the red error overlay goes away after 3 s (or the next click)
constexpr int kInventory    = 36;

std::int64_t keyOf(Player const& p) { return p.getOrCreateUniqueID().rawID; }

struct NativeSession {
    std::uint32_t       id = 0;
    ui::Menu const*     menu = nullptr; ///< identity only (the menu is owned by MenuManager)
    int                 containerId = -1;
    BackpackScreenModel sent;
    std::uint64_t       revision    = ~std::uint64_t{0};
    int                 sinceBuild  = 0;
    std::string         errorMessage;
    std::vector<int>    errorUpgradeSlots;
    std::vector<int>    errorStorageSlots;
    int                 errorTicks = 0;
    bool                dirty      = false; ///< push an update on the next tick
    /// Vanilla `lastQuickMoved`: what the last shift-click moved (Shift + double-click moves the
    /// rest of that item from the same inventory).
    core::ItemStackData lastQuickMoved;
};

std::unordered_map<std::int64_t, NativeSession> gSessions; // server thread
std::uint32_t                                   gNextSession = 1;

ui::BackpackMenu* backpackMenuOf(ui::Menu& menu) { return dynamic_cast<ui::BackpackMenu*>(&menu); }

std::string const& groupOf(upgrade::IUpgrade const& type) { return type.info().groupOrId(); }

bool matchesUpgrade(std::string const& wanted, upgrade::IUpgrade const& type) {
    return wanted == type.info().id || wanted == groupOf(type);
}

void applyError(BackpackScreenModel& m, NativeSession const& s) {
    m.errorMessage      = s.errorMessage;
    m.errorUpgradeSlots = s.errorUpgradeSlots;
    m.errorStorageSlots = s.errorStorageSlots;
}

void setError(NativeSession& s, std::string message, std::vector<int> upgradeSlots, std::vector<int> storageSlots = {}) {
    s.errorMessage      = std::move(message);
    s.errorUpgradeSlots = std::move(upgradeSlots);
    s.errorStorageSlots = std::move(storageSlots);
    s.errorTicks        = s.errorMessage.empty() ? 0 : kErrorTicks;
    s.dirty             = true;
}

void forgetTabMenus(Player const& p);

/// A filler / decoration icon (gray glass pane named " "): the native screen draws nothing there.
bool isFiller(ItemStack const& s) {
    if (s.isNull() || s.getTypeName() != "minecraft:gray_stained_glass_pane") return false;
    auto name = s.getCustomName();
    return name.empty() || name == " " || name == "\xC2\xA7r " || name == "\xC2\xA7r";
}

/// Model of a menu without its own native screen ("menu" kind).
BackpackScreenModel buildMenuModel(Player& player, ui::Menu& menu) {
    BackpackScreenModel m;
    m.kind     = "menu";
    m.title    = menu.title();
    m.menuRows = static_cast<int>(menu.rows());
    menu.ensureBuilt(player);
    for (int i = 0; i < menu.size(); ++i) {
        auto* slot = menu.slot(i);
        auto  shown = slot ? slot->display(player) : ItemStack{};
        if (isFiller(shown)) m.menuSlots.push_back(core::ItemStackData::of(std::string(core::screen::kFillerId), 1));
        else m.menuSlots.push_back(game::ItemConv::toData(shown));
    }
    m.lockedSlot = menu.lockedInventorySlot.value_or(-1);
    return m;
}

BackpackScreenModel buildFor(Player& player, ui::Menu& menu) {
    if (auto* bm = backpackMenuOf(menu)) return buildScreenModel(player, *bm);
    return buildMenuModel(player, menu);
}

/// Sends the patch from the last sent model to the current one (if anything changed).
void pushUpdate(Player& player, ui::Menu& menu, NativeSession& s, bool force = false) {
    auto model        = buildFor(player, menu);
    model.session     = s.id;
    model.containerId = s.containerId;
    applyError(model, s);
    auto patch = core::screen::makePatch(s.sent, model);
    if (auto* bm = backpackMenuOf(menu)) s.revision = bm->backpack()->revision();
    s.sinceBuild = 0;
    s.dirty      = false;
    if (patch.empty() && !force) return;
    s.sent = std::move(model);
    send(player, ScreenUpdatePacket{nlohmann::json{{"session", s.id}, {"patch", std::move(patch)}}});
}

void onBeforeShow(Player& player, ui::Menu& menu, int containerId) {
    if (!MenuManager::get().isVirtual(player)) return;
    NativeSession s;
    s.id               = gNextSession++;
    s.menu             = &menu;
    s.containerId      = containerId;
    s.sent             = buildFor(player, menu);
    s.sent.session     = s.id;
    s.sent.containerId = containerId;
    if (auto* bm = backpackMenuOf(menu)) s.revision = bm->backpack()->revision();
    send(player, ScreenOpenPacket{s.sent.toJson()});
    gSessions[keyOf(player)] = std::move(s);
}

void onAfterTick(Player& player, ui::Menu& menu) {
    auto it = gSessions.find(keyOf(player));
    if (it == gSessions.end() || it->second.menu != &menu) return;
    auto& s = it->second;
    if (s.errorTicks > 0 && --s.errorTicks == 0) setError(s, {}, {});
    if (auto* bm = backpackMenuOf(menu)) {
        bool changed = bm->backpack()->revision() != s.revision || s.dirty;
        if (!changed && ++s.sinceBuild < kRebuildEvery) return;
    } else if (!s.dirty && !menu.needsBuild() && ++s.sinceBuild < 2) {
        return; // generic menus: diff every 2 ticks like the chest sync
    }
    pushUpdate(player, menu, s);
}

void onClosed(Player* player, ui::Menu& menu) {
    for (auto it = gSessions.begin(); it != gSessions.end(); ++it) {
        if (it->second.menu != &menu) continue;
        if (player) {
            send(*player, ScreenClosePacket{nlohmann::json{{"session", it->second.id}}});
            forgetTabMenus(*player);
        }
        gSessions.erase(it);
        return;
    }
}
// ---- clicks ---------------------------------------------------------------------------------------

enum class Area { Storage, Inventory, Upgrade, Invalid };

Area areaOf(nlohmann::json const& body) {
    auto a = body.value("area", std::string{});
    if (a == "storage") return Area::Storage;
    if (a == "inventory") return Area::Inventory;
    if (a == "upgrade") return Area::Upgrade;
    return Area::Invalid;
}

ItemStack cursorOf(Player& p) { return p.mPlayerUIContainer->getItem(0); }

bool sameItem(ItemStack const& a, core::ItemStackData const& b) {
    return !a.isNull() && !b.empty() && game::ItemConv::toData(a).sameItem(b);
}

/// An inventory slot for a quick move of `item`: a stack of the same item with room, else an
/// empty slot (main inventory first, like the chest screen), else -1.
int quickMoveTarget(Player& p, core::ItemStackData const& item, std::optional<int> locked) {
    auto& inv   = p.getInventory();
    auto  order = std::vector<int>{};
    for (int i = 9; i < kInventory; ++i) order.push_back(i);
    for (int i = 0; i < 9; ++i) order.push_back(i);
    for (int pass = 0; pass < 2; ++pass) {
        for (int i : order) {
            if (locked && *locked == i) continue;
            auto const& cur = inv.getItem(i);
            if (pass == 0 && sameItem(cur, item) && cur.mCount < cur.getMaxStackSize()) return i;
            if (pass == 1 && cur.isNull()) return i;
        }
    }
    return -1;
}

MenuManager::SimAction act(ActionType type, MenuManager::SlotAddress src, MenuManager::SlotAddress dst, int amount) {
    MenuManager::SimAction a;
    a.type   = type;
    a.src    = src;
    a.dst    = dst;
    a.amount = amount;
    return a;
}

struct SlotError {
    std::string      message;
    std::vector<int> upgradeSlots;
};

/// Sophisticated Core's `canAddUpgradeTo` result for putting `item` into upgrade slot `slot` that
/// `upgrade::canInstall` refused with `reason`: SC's message and the upgrade slots it highlights
/// (the slots already holding the limiting / conflicting upgrades). Empty message = no overlay
/// (SC: a non-upgrade item simply doesn't go in).
SlotError installError(Player& p, ui::BackpackMenu& menu, int slot, core::ItemStackData const& item, std::string const& reason) {
    auto* type = upgrade::upgradeFor(item);
    if (!type) return {};
    auto const& slots   = menu.backpack()->data().upgrades;
    auto        storage = menu.title();
    auto        name    = upgrade::displayName(*type, &p);
    if (!upgrade::typeAllowed(*type)) {
        return {trFor(p, "abp.mainscreen.error.add.upgrade_not_allowed", "{0} can't be used in {1}", name, storage), {}};
    }
    auto const&      info = type->info();
    std::vector<int> same, conflicting;
    for (int i = 0; i < static_cast<int>(slots.size()); ++i) {
        auto const& st = slots[static_cast<std::size_t>(i)];
        auto*       other = st.empty() || i == slot ? nullptr : upgrade::upgradeFor(st.item);
        if (!other) continue;
        if (other->info().groupOrId() == info.groupOrId()) same.push_back(i);
        auto hits = [](upgrade::IUpgrade const& a, upgrade::IUpgrade const& b) {
            return std::any_of(a.info().conflicts.begin(), a.info().conflicts.end(), [&](std::string const& c) {
                return c == b.info().id || c == b.info().groupOrId();
            });
        };
        if (hits(*type, *other) || hits(*other, *type)) conflicting.push_back(i);
    }
    if (!conflicting.empty()) return {reason, conflicting};
    if (static_cast<int>(same.size()) >= info.maxPerBackpack) {
        return {
            trFor(p, "abp.mainscreen.error.add.only_x_upgrades_allowed", "Only {0}x {1} can be used in {2}", info.maxPerBackpack, name, storage),
            same
        };
    }
    return {reason, {slot}};
}

/// One click on a storage or upgrade slot (through `simulateOn` with that slot).
bool clickMenuSlot(Player& p, ui::BackpackMenu& menu, NativeSession& s, Area area, int index, int button, bool shift, bool ctrl) {
    auto& mm     = MenuManager::get();
    auto  bp     = menu.backpack();
    auto  slot   = area == Area::Storage ? menu.storageSlot(index) : menu.upgradeSlot(index);
    if (!slot) return false;
    MenuManager::SlotAddress here{Where::Menu, index};
    MenuManager::SlotAddress cur{Where::Cursor, 0};
    ItemStack cursor = cursorOf(p);

    core::ItemStackData content;
    int                 maxTake = 1;
    if (area == Area::Storage) {
        content = bp->inventory().at(index);
        maxTake = std::clamp(content.maxStack, 1, 255);
        // SC Infinity: an infinite slot reports Integer.MAX_VALUE, so a click takes a full stack.
        if (!content.empty() && bp->inventory().isInfinite(index)) content.count = std::max(content.count, maxTake);
    } else {
        content = bp->data().upgrades[static_cast<std::size_t>(index)].item;
    }

    // Upgrade slot changes show SC's red error overlay instead of a chat message; a successful
    // upgrade slot change clears it (SC `updateSlotChangeError` with a successful result).
    if (area == Area::Upgrade) {
        std::string reason;
        bool        removing = !content.empty() && (cursor.isNull() || shift);
        bool        installing = content.empty() && !cursor.isNull();
        if (removing) {
            if (auto* type = upgrade::upgradeFor(content); type && !type->canRemove(upgrade::UpgradeContext(bp, index, &p), reason)) {
                setError(s, reason, {index});
                return false;
            }
        } else if (installing) {
            auto data = game::ItemConv::toData(cursor).withCount(1);
            if (!upgrade::canInstall(bp, index, data, &p, &reason)) {
                auto e = installError(p, menu, index, data, reason);
                if (!e.message.empty()) setError(s, std::move(e.message), std::move(e.upgradeSlots));
                return false;
            }
        } else if (!content.empty() && !cursor.isNull()) {
            // Swapping one upgrade for another: the installed one must allow it (conversion items
            // that aren't upgrades are left to IUpgrade::useItemOn).
            auto data = game::ItemConv::toData(cursor).withCount(1);
            auto* installed = upgrade::upgradeFor(content);
            if (installed && upgrade::upgradeFor(data) && !installed->canReplaceWith(upgrade::UpgradeContext(bp, index, &p), data, reason)) {
                setError(s, reason, {index});
                return false;
            }
        }
    }

    if (shift) {
        // Vanilla `lastQuickMoved` (Shift + double-click moves the rest of this item).
        if (!(ctrl && button == 0 && area == Area::Storage)) s.lastQuickMoved = content;
        if (content.empty()) return false;
        int target = quickMoveTarget(p, content, menu.lockedInventorySlot);
        if (target < 0) return false;
        int amount = area == Area::Storage ? std::min(content.count, maxTake) : 1;
        bool ok    = mm.simulateOn(p, act(ActionType::Take, here, {Where::Inventory, target}, amount), slot);
        // Ctrl+Shift+left click: SC's "transfer full slot" (all of an over-sized storage slot).
        for (int guard = 0; ok && ctrl && button == 0 && area == Area::Storage && guard < 64; ++guard) {
            auto const& left = bp->inventory().at(index);
            if (left.empty()) break;
            target = quickMoveTarget(p, left, menu.lockedInventorySlot);
            if (target < 0) break;
            ok = mm.simulateOn(p, act(ActionType::Take, here, {Where::Inventory, target}, std::min(left.count, maxTake)), slot);
        }
        return ok;
    }
    if (cursor.isNull()) {
        if (content.empty()) return false;
        int have   = area == Area::Storage ? std::min(content.count, maxTake) : 1;
        int amount = button == 1 ? (have + 1) / 2 : have;
        return mm.simulateOn(p, act(ActionType::Take, here, cur, amount), slot);
    }
    if (content.empty() || sameItem(cursor, content)) {
        int amount = button == 1 ? 1 : static_cast<int>(cursor.mCount);
        return mm.simulateOn(p, act(ActionType::Place, cur, here, amount), slot);
    }
    // SC: an item that is not an upgrade only acts on an installed upgrade with a right click
    // (stack upgrade conversion items); a left click does nothing.
    if (area == Area::Upgrade && button != 1 && !upgrade::upgradeFor(game::ItemConv::toData(cursor).withCount(1))) return false;
    return mm.simulateOn(p, act(ActionType::Swap, here, cur, 0), slot);
}

bool clickInventory(Player& p, ui::Menu& base, NativeSession& s, int index, int button, bool shift) {
    (void)base;
    if (index < 0 || index >= kInventory) return false;
    auto&     mm     = MenuManager::get();
    ItemStack item   = p.getInventory().getItem(index);
    ItemStack cursor = cursorOf(p);
    MenuManager::SlotAddress here{Where::Inventory, index};
    MenuManager::SlotAddress cur{Where::Cursor, 0};
    if (shift) {
        s.lastQuickMoved = item.isNull() ? core::ItemStackData{} : game::ItemConv::toData(item);
        if (item.isNull()) return false;
        // Into the menu (quick move: backpack filters, memory slots, quick-move handlers).
        return mm.simulate(p, act(ActionType::Place, here, {Where::Menu, 0}, item.mCount));
    }
    if (cursor.isNull()) {
        if (item.isNull()) return false;
        int amount = button == 1 ? (item.mCount + 1) / 2 : item.mCount;
        return mm.simulate(p, act(ActionType::Take, here, cur, amount));
    }
    if (item.isNull() || item.matchesItem(cursor)) {
        int room   = item.isNull() ? cursor.getMaxStackSize() : std::max(0, item.getMaxStackSize() - item.mCount);
        int amount = std::min<int>(button == 1 ? 1 : cursor.mCount, room);
        if (amount <= 0) return false;
        return mm.simulate(p, act(ActionType::Place, cur, here, amount));
    }
    return mm.simulate(p, act(ActionType::Swap, here, cur, 0));
}

/// Player inventory slots in the order of SC's menu slots: the main inventory rows, then the hotbar.
std::vector<int> menuOrderInventory() {
    std::vector<int> out;
    for (int i = 9; i < kInventory; ++i) out.push_back(i);
    for (int i = 0; i < 9; ++i) out.push_back(i);
    return out;
}

/// SC `doClick(PICKUP_ALL)` (double click with an item on the cursor) on the clicked slot: only when
/// that slot is empty, the same item is gathered from the storage and inventory slots (first pass
/// skips full stacks), then from the upgrade slots (not ones taking storage columns).
bool pickupAll(Player& p, ui::BackpackMenu& menu, Area area, int index) {
    auto&     mm  = MenuManager::get();
    auto      bp  = menu.backpack();
    auto&     inv = bp->inventory();
    ItemStack cursor = cursorOf(p);
    if (cursor.isNull()) return false;
    bool clickedEmpty = true;
    if (area == Area::Storage && index >= 0 && index < inv.size()) clickedEmpty = inv.at(index).empty();
    if (area == Area::Inventory && index >= 0 && index < kInventory) clickedEmpty = p.getInventory().getItem(index).isNull();
    if (area == Area::Upgrade && index >= 0 && index < static_cast<int>(bp->data().upgrades.size())) {
        clickedEmpty = bp->data().upgrades[static_cast<std::size_t>(index)].empty();
    }
    if (!clickedEmpty) return false;
    auto room = [&] {
        auto c = cursorOf(p);
        return c.isNull() ? 0 : std::max(0, static_cast<int>(c.getMaxStackSize()) - static_cast<int>(c.mCount));
    };
    auto const locked = menu.lockedInventorySlot;
    bool       any    = false;
    for (int pass = 0; pass < 2; ++pass) {
        for (int i = 0; i < inv.size() && room() > 0; ++i) {
            auto const d = inv.at(i);
            if (!sameItem(cursorOf(p), d)) continue;
            if (pass == 0 && d.count == d.maxStack) continue;
            any |= mm.simulateOn(p, act(ActionType::Take, {Where::Menu, i}, {Where::Cursor, 0}, std::min(room(), d.count)), menu.storageSlot(i));
        }
        for (int i : menuOrderInventory()) {
            if (room() <= 0) break;
            if (locked && *locked == i) continue;
            auto const& it = p.getInventory().getItem(i);
            if (it.isNull() || !it.matchesItem(cursorOf(p))) continue;
            if (pass == 0 && it.mCount == it.getMaxStackSize()) continue;
            any |= mm.simulate(p, act(ActionType::Take, {Where::Inventory, i}, {Where::Cursor, 0}, std::min<int>(room(), it.mCount)));
        }
    }
    for (int pass = 0; pass < 2; ++pass) {
        for (int i = 0; i < static_cast<int>(bp->data().upgrades.size()) && room() > 0; ++i) {
            auto const st = bp->data().upgrades[static_cast<std::size_t>(i)];
            if (st.empty() || !sameItem(cursorOf(p), st.item)) continue;
            auto* type = upgrade::upgradeFor(st.item);
            if (upgrade::columns::columnsOf(st.item) > 0) continue; // canTakeItemForPickAll
            if (pass == 0 && st.item.count == st.item.maxStack) continue;
            std::string reason;
            if (type && !type->canRemove(upgrade::UpgradeContext(bp, i, &p), reason)) continue;
            any |= mm.simulateOn(p, act(ActionType::Take, {Where::Menu, i}, {Where::Cursor, 0}, 1), menu.upgradeSlot(i));
        }
    }
    return any;
}

/// Vanilla Shift + double-click (`mouseReleased` with `lastQuickMoved`): quick-moves every other
/// stack of the last shift-clicked item from the same inventory (the storage, the player inventory
/// or the upgrade slots). Over-sized storage stacks move whole (SC `TransferFullSlotPayload`).
bool quickMoveAll(Player& p, ui::BackpackMenu& menu, NativeSession& s, Area area) {
    auto last = s.lastQuickMoved;
    if (last.empty()) return false;
    auto bp  = menu.backpack();
    bool any = false;
    if (area == Area::Storage) {
        for (int i = 0; i < bp->inventory().size(); ++i) {
            auto const& d = bp->inventory().at(i);
            if (d.empty() || !d.sameItem(last)) continue;
            any |= clickMenuSlot(p, menu, s, Area::Storage, i, 0, true, d.count > d.maxStack);
        }
    } else if (area == Area::Inventory) {
        for (int i : menuOrderInventory()) {
            auto const& it = p.getInventory().getItem(i);
            if (it.isNull() || !game::ItemConv::toData(it).sameItem(last)) continue;
            if (menu.lockedInventorySlot && *menu.lockedInventorySlot == i) continue;
            any |= clickInventory(p, menu, s, i, 0, true);
        }
    } else if (area == Area::Upgrade) {
        for (int i = 0; i < static_cast<int>(bp->data().upgrades.size()); ++i) {
            auto const& st = bp->data().upgrades[static_cast<std::size_t>(i)];
            if (st.empty() || !st.item.sameItem(last)) continue;
            any |= clickMenuSlot(p, menu, s, Area::Upgrade, i, 0, true, false);
        }
    }
    s.lastQuickMoved = last;
    return any;
}

ui::Menu* currentMenu(Player& p, std::uint32_t session, NativeSession*& out) {
    auto it = gSessions.find(keyOf(p));
    if (it == gSessions.end() || it->second.id != session || session == 0) return nullptr;
    auto menu = MenuManager::get().current(p);
    if (!menu || menu.get() != it->second.menu) return nullptr;
    out = &it->second;
    return menu.get();
}

/// A click on a slot of a generic menu (filter editors, crafting grids, buttons, ...).
bool clickGenericSlot(Player& p, ui::Menu& menu, int index, int button, bool shift) {
    auto& mm   = MenuManager::get();
    auto* slot = menu.slot(index);
    if (!slot) return false;
    ItemStack shown  = slot->display(p);
    ItemStack cursor = cursorOf(p);
    MenuManager::SlotAddress here{Where::Menu, index};
    MenuManager::SlotAddress cur{Where::Cursor, 0};
    if (shift) {
        auto data   = game::ItemConv::toData(shown);
        int  target = data.empty() ? 9 : quickMoveTarget(p, data, menu.lockedInventorySlot);
        if (target < 0) return false;
        return mm.simulate(p, act(ActionType::Take, here, {Where::Inventory, target}, std::max(1, static_cast<int>(shown.mCount))));
    }
    if (cursor.isNull()) {
        int have   = shown.isNull() ? 1 : static_cast<int>(shown.mCount);
        int amount = button == 1 ? (have + 1) / 2 : have;
        return mm.simulate(p, act(ActionType::Take, here, cur, amount));
    }
    if (shown.isNull() || shown.matchesItem(cursor)) {
        return mm.simulate(p, act(ActionType::Place, cur, here, button == 1 ? 1 : static_cast<int>(cursor.mCount)));
    }
    return mm.simulate(p, act(ActionType::Swap, here, cur, 0));
}

std::string translatedUpgradeName(Player& p, upgrade::IUpgrade const& type) { return upgrade::displayName(type, &p); }

// ---- native settings screen: feature/gui/SbSettings.h (P2-2) -------------------------------------------
// ---- open upgrade tab (SC setOpenTabId / getOpenTabId, reopened with "keep tab open") -------------------

constexpr char const* kOpenTabKey = "openTab";

int openTabOf(Player& p, game::Backpack const& bp) {
    if (!feature::gui::settingValue(p, bp, feature::gui::MainSetting::KeepTabOpen)) return -1; // removeOpenTabIfKeepOff
    auto it = bp.data().settings.find("mainscreen");
    if (it == bp.data().settings.end() || !it->is_object()) return -1;
    auto v = it->find(kOpenTabKey);
    return v != it->end() && v->is_number_integer() ? v->get<int>() : -1;
}

void setOpenTab(game::Backpack& bp, int slot) {
    auto& s = bp.data().settings["mainscreen"];
    if (!s.is_object()) s = nlohmann::json::object();
    if (s.value(kOpenTabKey, -2) == slot) return;
    s[kOpenTabKey] = slot;
    bp.markDirty();
}

// ---- upgrade tabs bridged from the upgrades' settings menus ------------------------------------------
// Until an upgrade has its own native tab, its settings menu (`IUpgrade::createSettingsMenu`, e.g.
// a filter editor) is shown inside its SB tab: the menu object lives here (never opened as a chest),
// its slots are sent as tab data and tab clicks run against them through `simulateOn`.

struct TabMenuKey {
    std::int64_t player;
    std::string  uuid;
    int          slot;
    bool         operator==(TabMenuKey const&) const = default;
};
struct TabMenuKeyHash {
    std::size_t operator()(TabMenuKey const& k) const noexcept {
        return std::hash<std::string>{}(k.uuid) ^ (static_cast<std::size_t>(k.player) * 31u) ^ static_cast<std::size_t>(k.slot);
    }
};
struct TabMenu {
    std::string               itemId;
    std::shared_ptr<ui::Menu> menu;
};
std::unordered_map<TabMenuKey, TabMenu, TabMenuKeyHash> gTabMenus;

std::shared_ptr<ui::Menu> tabMenuFor(Player& p, upgrade::IUpgrade& type, upgrade::UpgradeContext const& ctx) {
    TabMenuKey key{keyOf(p), ctx.backpack().uuid(), ctx.slot()};
    auto const& itemId = ctx.state().item.id;
    auto        it     = gTabMenus.find(key);
    if (it != gTabMenus.end() && it->second.itemId == itemId) return it->second.menu;
    auto menu = type.createSettingsMenu(p, ctx);
    if (!menu) return nullptr;
    gTabMenus[key] = TabMenu{itemId, menu};
    return menu;
}

void forgetTabMenus(Player const& p) {
    std::erase_if(gTabMenus, [k = keyOf(p)](auto const& e) { return e.first.player == k; });
}

nlohmann::json tabMenuJson(Player& p, ui::Menu& menu) {
    // Settings may have changed elsewhere (another player, a tab action handler): rebuild.
    menu.invalidate();
    menu.ensureBuilt(p);
    nlohmann::json slots = nlohmann::json::array();
    for (int i = 0; i < menu.size(); ++i) {
        auto* slot  = menu.slot(i);
        auto  shown = slot ? slot->display(p) : ItemStack{};
        if (isFiller(shown)) slots.push_back(core::ItemStackData::of(std::string(core::screen::kFillerId), 1).toJson());
        else if (shown.isNull()) slots.push_back(nullptr);
        else slots.push_back(game::ItemConv::toData(shown).toJson());
    }
    return nlohmann::json{{"rows", static_cast<int>(menu.rows())}, {"title", menu.title()}, {"slots", std::move(slots)}};
}

/// One click on a slot object that isn't part of the open menu (tab menus).
bool clickSlotObject(Player& p, std::shared_ptr<ui::Slot> slot, int button, bool shift, std::optional<int> locked) {
    auto& mm = MenuManager::get();
    if (!slot) return false;
    ItemStack shown  = slot->display(p);
    ItemStack cursor = cursorOf(p);
    MenuManager::SlotAddress here{Where::Menu, 0};
    MenuManager::SlotAddress cur{Where::Cursor, 0};
    if (shift) {
        auto data   = game::ItemConv::toData(shown);
        int  target = data.empty() ? 9 : quickMoveTarget(p, data, locked);
        if (target < 0) return false;
        return mm.simulateOn(p, act(ActionType::Take, here, {Where::Inventory, target}, std::max(1, static_cast<int>(shown.mCount))), slot);
    }
    if (cursor.isNull()) {
        int have = shown.isNull() ? 1 : static_cast<int>(shown.mCount);
        return mm.simulateOn(p, act(ActionType::Take, here, cur, button == 1 ? (have + 1) / 2 : have), slot);
    }
    if (shown.isNull() || shown.matchesItem(cursor)) {
        return mm.simulateOn(p, act(ActionType::Place, cur, here, button == 1 ? 1 : static_cast<int>(cursor.mCount)), slot);
    }
    return mm.simulateOn(p, act(ActionType::Swap, here, cur, 0), slot);
}

bool handleSettingsAction(Player& player, ui::BackpackMenu& menu, NativeSession& s, std::string const& action, nlohmann::json const& body) {
    auto bp = menu.backpack();
    if (action == "settings") {
        // The settings screen is a client view of this session; SB opens a new settings container.
        feature::gui::settingsScreenOpened(player, *bp);
        s.dirty = true;
        return true;
    }
    int slot = body.value("slot", -1);
    if (action == "upgradeSettings" || action == "tab") {
        if (slot < 0 || slot >= static_cast<int>(bp->data().upgrades.size())) return false;
        auto const& st   = bp->data().upgrades[static_cast<std::size_t>(slot)];
        auto*       type = st.empty() ? nullptr : upgrade::upgradeFor(st.item);
        if (!type) return false;
        upgrade::UpgradeContext ctx(bp, slot, &player);
        if (action == "upgradeSettings") {
            if (!type->info().hasSettings) return false;
            type->openSettings(player, ctx);
            return true;
        }
        auto data = body.value("data", nlohmann::json::object());
        if (data.contains("menuClick")) {
            auto tabMenu = tabMenuFor(player, *type, ctx);
            int  i       = data.value("menuClick", -1);
            if (!tabMenu || i < 0 || i >= tabMenu->size()) return false;
            tabMenu->ensureBuilt(player);
            std::shared_ptr<ui::Slot> target(tabMenu, tabMenu->slot(i)); // aliasing: keeps the menu alive
            // The menu lives in a tab, not as the open screen: its buttons must not close the
            // backpack screen (close()/closeThen() act on the open menu).
            bool ok;
            {
                MenuManager::NavigationGuard guard(player);
                ok = clickSlotObject(player, target, data.value("button", 0), data.value("shift", false), menu.lockedInventorySlot);
            }
            MenuManager::get().resyncPlayer(player);
            s.dirty = true;
            return ok;
        }
        for (auto const& h : TabActionRegistry::entries()) {
            if (h.handle && matchesUpgrade(h.upgrade, *type) && h.handle(player, ctx, data)) return true;
        }
        return false;
    }
    // "setting": the native settings screen (feature/gui/SettingsScreen.cpp).
    s.dirty = true;
    return feature::gui::handleSettingsScreenAction(player, *bp, body);
}
} // namespace

// ---- model ----------------------------------------------------------------------------------------

BackpackScreenModel buildScreenModel(Player& player, ui::BackpackMenu& menu) {
    auto&               bp  = *menu.backpack();
    auto&               inv = bp.inventory();
    BackpackScreenModel m;
    m.uuid         = bp.uuid();
    m.title        = menu.title();
    m.tier         = static_cast<int>(bp.tier());
    m.storageSlots = inv.size();
    m.slots.reserve(static_cast<std::size_t>(inv.size()));
    m.slotLimits.resize(static_cast<std::size_t>(inv.size()), 0);
    for (int i = 0; i < inv.size(); ++i) {
        auto const& d = inv.at(i);
        m.slots.push_back(d);
        if (!d.empty()) m.slotLimits[static_cast<std::size_t>(i)] = inv.slotLimit(i, d);
    }
    // SC InventoryHandler.getSlotLimit: the limit for 64-stackable items (64 x multiplier).
    m.baseSlotLimit = inv.size() > 0 ? inv.slotLimit(0, core::ItemStackData::of("minecraft:stone", 1, 64)) : 64;
    auto const& slots = bp.data().upgrades;
    for (int i = 0; i < static_cast<int>(slots.size()); ++i) {
        auto const&               s = slots[static_cast<std::size_t>(i)];
        core::screen::UpgradeView v;
        v.item    = s.item;
        v.enabled = s.enabled;
        if (auto* type = s.empty() ? nullptr : upgrade::upgradeFor(s.item)) {
            upgrade::UpgradeContext ctx(menu.backpack(), i, &player);
            v.id          = type->info().id;
            v.name        = translatedUpgradeName(player, *type);
            v.toggleable  = type->info().toggleable;
            v.runnable    = upgrade::typeAllowed(*type);
            v.hasSettings = type->info().hasSettings;
            v.lore        = type->describe(player, ctx);
            if (type->info().hasSettings) {
                if (auto tabMenu = tabMenuFor(player, *type, ctx)) v.tab["menu"] = tabMenuJson(player, *tabMenu);
            }
            for (auto const& p : TabDataRegistry::entries()) {
                if (p.data && matchesUpgrade(p.upgrade, *type)) {
                    auto data = p.data(player, ctx);
                    if (data.is_object()) v.tab.update(data);
                }
            }
        }
        m.upgrades.push_back(std::move(v));
    }
    m.memory = feature::gui::memory(bp);
    m.noSort = feature::gui::noSort(bp);
    m.sortBy = std::string(core::toString(feature::gui::effective(&player, bp).sortBy));
    m.nested = dynamic_cast<upgrade::inception::NestedBackpackMenu*>(&menu) != nullptr;
    m.lockedSlot      = menu.lockedInventorySlot.value_or(-1);
    m.stackMultiplier = upgrade::stack::totalMultiplier(bp);
    m.mainColor       = bp.data().mainColor;
    m.accentColor     = bp.data().accentColor;
    m.noSortColor     = core::kDyes[static_cast<std::size_t>(feature::gui::noSortColor(bp))].argb & 0xFFFFFF;
    m.extra["settings"] = feature::gui::settingsScreenJson(player, bp);
    m.extra["mainscreen"] = nlohmann::json{{"openTab", openTabOf(player, bp)}}; // kept open upgrade tab
    return m;
}

std::uint32_t nativeSession(Player const& player) {
    auto it = gSessions.find(keyOf(player));
    return it == gSessions.end() ? 0 : it->second.id;
}

BackpackScreenModel const* sentModel(Player const& player) {
    auto it = gSessions.find(keyOf(player));
    return it == gSessions.end() ? nullptr : &it->second.sent;
}

// ---- actions --------------------------------------------------------------------------------------

bool handleScreenAction(Player& player, nlohmann::json const& body) {
    if (!body.is_object()) return false;
    NativeSession* s       = nullptr;
    auto           session = body.value("session", 0u);
    auto*          base    = currentMenu(player, session, s);
    if (!base || !s) return false;
    auto* menu   = backpackMenuOf(*base);
    auto  action = body.value("action", std::string{});
    auto& mm     = MenuManager::get();
    bool  ok     = false;
    bool  items  = false; // the player's inventory/cursor may have changed
    bool  silent = action == "click" || action == "distribute" || action == "menuClick" || action == "double" || action == "drop" || action == "hotbar";

    if (action == "close") {
        mm.close(player);
        log().info("native screen action: close by {}", player.getRealName());
        return true;
    }
    if (action == "resync") {
        // The client's model is unreliable (a patch failed): send it whole again.
        auto model        = buildFor(player, *base);
        model.session     = s->id;
        model.containerId = s->containerId;
        applyError(model, *s);
        s->sent = model;
        send(player, ScreenOpenPacket{model.toJson()});
        return true;
    }

    auto area = areaOf(body);
    int  idx  = body.value("index", -1);
    bool intercepted = false;
    if (action == "click" && body.value("button", 0) == 1 && !body.value("shift", false) && !cursorOf(player).isNull()
        && (area == Area::Inventory || (area == Area::Storage && menu))) {
        for (auto const& h : ScreenClickInterceptRegistry::entries()) {
            if (h.handle && h.handle(player, menu, area == Area::Storage ? "storage" : "inventory", idx)) {
                intercepted = true;
                break;
            }
        }
    }
    if (intercepted) {
        items = true;
        ok    = true;
    } else if (action == "dropCursor") {
        items       = true;
        auto cursor = cursorOf(player);
        if (cursor.isNull()) return false;
        int n = body.value("all", true) ? static_cast<int>(cursor.mCount) : 1;
        ok    = mm.simulate(player, act(ActionType::Drop, {Where::Cursor, 0}, {}, n));
    } else if (action == "distribute") {
        // Dragging the cursor stack over slots (vanilla quick craft): left = split evenly,
        // right = one each. Only empty slots or slots with the same item take items.
        items      = true;
        auto slots = body.value("slots", nlohmann::json::array());
        auto first = cursorOf(player);
        if (first.isNull() || !slots.is_array() || slots.empty() || slots.size() > 200) return false;
        int button = body.value("button", 0);
        int per    = button == 1 ? 1 : std::max(1, static_cast<int>(first.mCount) / static_cast<int>(slots.size()));
        for (auto const& e : slots) {
            auto cursor = cursorOf(player);
            if (cursor.isNull()) break;
            auto a = e.value("area", std::string{});
            int  i = e.value("index", -1);
            int  n = std::min<int>(per, cursor.mCount);
            if (a == "storage" && menu && i >= 0 && i < menu->backpack()->inventory().size()) {
                auto const& d = menu->backpack()->inventory().at(i);
                if (!d.empty() && !sameItem(cursor, d)) continue;
                ok |= mm.simulateOn(player, act(ActionType::Place, {Where::Cursor, 0}, {Where::Menu, i}, n), menu->storageSlot(i));
            } else if (a == "inventory" && i >= 0 && i < kInventory) {
                auto const& it = player.getInventory().getItem(i);
                if (!it.isNull() && !it.matchesItem(cursor)) continue;
                int room = it.isNull() ? cursor.getMaxStackSize() : std::max(0, it.getMaxStackSize() - it.mCount);
                if (std::min(n, room) <= 0) continue;
                ok |= mm.simulate(player, act(ActionType::Place, {Where::Cursor, 0}, {Where::Inventory, i}, std::min(n, room)));
            }
        }
    } else if (action == "menuClick") {
        // A slot of a menu without its own native screen: exactly like a click in its chest.
        items = true;
        if (menu || idx < 0 || idx >= base->size()) return false;
        ok = clickGenericSlot(player, *base, idx, body.value("button", 0), body.value("shift", false));
    } else if ((action == "click" || action == "drop" || action == "hotbar") && area == Area::Inventory) {
        items = true;
        if (idx < 0 || idx >= kInventory) return false;
        if (action == "click") {
            ok = clickInventory(player, *base, *s, idx, body.value("button", 0), body.value("shift", false));
        } else if (action == "drop") {
            ok = mm.simulate(player, act(ActionType::Drop, {Where::Inventory, idx}, {}, body.value("all", false) ? 255 : 1));
        } else {
            int hb = body.value("hotbar", -1);
            if (hb < 0 || hb > 8 || idx == hb) return false;
            if (base->lockedInventorySlot && (*base->lockedInventorySlot == hb || *base->lockedInventorySlot == idx)) return false;
            ok = mm.simulate(player, act(ActionType::Swap, {Where::Inventory, idx}, {Where::Inventory, hb}, 0));
        }
    } else if (!menu) {
        return false; // the remaining actions belong to the backpack screen
    } else if (action == "double" || action == "quickMoveAll") {
        items = true;
        if (area == Area::Invalid) return false;
        ok = action == "double" ? pickupAll(player, *menu, area, idx) : quickMoveAll(player, *menu, *s, area);
    } else if (action == "click" || action == "drop" || action == "hotbar") {
        auto bp = menu->backpack();
        items   = true;
        bool inRange = (area == Area::Storage && idx >= 0 && idx < bp->inventory().size())
                    || (area == Area::Upgrade && idx >= 0 && idx < static_cast<int>(bp->data().upgrades.size()));
        if (!inRange) return false;
        if (area == Area::Storage && idx < static_cast<int>(s->sent.inaccessible.size()) && s->sent.inaccessible[static_cast<std::size_t>(idx)]) {
            return false; // SC: inaccessible slots can't be clicked
        }
        if (action == "click") {
            int  button = body.value("button", 0);
            bool shift  = body.value("shift", false);
            bool ctrl   = body.value("ctrl", false);
            ok          = clickMenuSlot(player, *menu, *s, area, idx, button, shift, ctrl);
            // A successful upgrade slot change replaces SC's error result (no overlay).
            if (ok && area == Area::Upgrade && !s->errorMessage.empty()) setError(*s, {}, {});
        } else if (action == "drop") {
            if (area != Area::Storage) return false;
            bool all = body.value("all", false);
            int  n   = all ? std::clamp(bp->inventory().at(idx).maxStack, 1, 255) : 1;
            ok       = mm.simulateOn(player, act(ActionType::Drop, {Where::Menu, idx}, {}, n), menu->storageSlot(idx));
        } else { // hotbar
            int hb = body.value("hotbar", -1);
            if (hb < 0 || hb > 8 || area != Area::Storage) return false;
            if (menu->lockedInventorySlot && *menu->lockedInventorySlot == hb) return false; // the open backpack stays put
            ok = mm.simulateOn(player, act(ActionType::Swap, {Where::Menu, idx}, {Where::Inventory, hb}, 0), menu->storageSlot(idx));
        }
    } else {
        auto bp = menu->backpack();
        if (action == "sort") {
            ok = feature::gui::sortBackpack(*bp, feature::gui::effective(&player, *bp).sortBy);
        } else if (action == "sortBy") {
            auto next = core::nextSortBy(feature::gui::effective(&player, *bp).sortBy);
            feature::gui::setBackpackOption(*bp, feature::gui::kSortBy, std::string(core::toString(next)));
            ok = true;
        } else if (action == "transfer") {
            // SC TransferItemsPayload (hotbar rules and exact matching, see Gui.h).
            items         = true;
            bool matching = body.value("matching", true);
            int  moved    = body.value("to", std::string{}) == "inventory"
                              ? feature::gui::transferToPlayerSB(player, *bp, matching, menu->lockedInventorySlot)
                              : feature::gui::transferToBackpackSB(player, *bp, matching, menu->lockedInventorySlot);
            ok = moved > 0;
        } else if (action == "openTab") {
            // SC setOpenTabId / removeOpenTabId: the open upgrade tab (reopened with "keep tab open").
            int slot = body.value("slot", -1);
            if (slot < -1 || slot >= static_cast<int>(bp->data().upgrades.size())) return false;
            setOpenTab(*bp, slot);
            s->dirty = true;
            ok       = true;
        } else if (action == "toggleUpgrade") {
            int slot = body.value("slot", -1);
            if (slot < 0 || slot >= static_cast<int>(bp->data().upgrades.size())) return false;
            auto const& st   = bp->data().upgrades[static_cast<std::size_t>(slot)];
            auto*       type = st.empty() ? nullptr : upgrade::upgradeFor(st.item);
            if (!type || !type->info().toggleable) return false;
            upgrade::setEnabled(bp, slot, !st.enabled);
            ok = true;
        } else if (action == "settings" || action == "upgradeSettings" || action == "tab" || action == "setting") {
            ok = handleSettingsAction(player, *menu, *s, action, body);
        } else if (action == "openNested") {
            if (idx < 0 || idx >= bp->inventory().size()) return false;
            if (bp->inventory().at(idx).count != 1) return false; // SB: only a single backpack item
            log().info("native screen action: openNested by {}", player.getRealName());
            return upgrade::inception::openNested(player, menu, bp, idx);
        } else if (action == "back") {
            auto* nested = dynamic_cast<upgrade::inception::NestedBackpackMenu*>(menu);
            if (!nested) return false;
            log().info("native screen action: back by {}", player.getRealName());
            nested->goBack(player);
            return true;
        } else {
            return false;
        }
    }

    if (!silent) log().info("native screen action: {} by {} ({})", action, player.getRealName(), ok ? "ok" : "nothing to do");
    if (items) mm.resyncPlayer(player);
    // The menu may have been replaced by a callback (e.g. Inception's storage click handler).
    NativeSession* again = nullptr;
    if (auto* still = currentMenu(player, session, again); still && again) pushUpdate(player, *still, *again);
    return ok;
}
// ---- packet handler + module ------------------------------------------------------------------------

class ScreenActionHandler : public ll::network::PacketHandlerBase<ScreenActionHandler, ScreenActionPacket> {
public:
    void handlePacket(NetworkIdentifier const& source, NetEventCallback& callback, ScreenActionPacket const& packet) const {
        auto* player = senderOf(source, callback, packet.mSenderSubId);
        if (!player || !packet.valid() || !hasCompanion(*player)) return;
        try {
            handleScreenAction(*player, packet.body);
        } catch (std::exception const& e) {
            log().error("native screen action failed: {}", e.what());
        }
    }
};

std::unordered_map<std::int64_t, std::chrono::steady_clock::time_point> gLastTold;

/// Link shown to players without the client mod (also in README.md).
constexpr std::string_view kClientModUrl = "https://github.com/LiteLDev/AlleviatedBackpacks/releases";

/// Backpack screens need the client mod: companions get native screens, simulated players
/// (selftests) the chest, everybody else a message.
ui::MenuManager::Presentation presentationFor(Player& player, ui::Menu&) {
    if (hasCompanion(player)) return ui::MenuManager::Presentation::Virtual;
    if (player.isSimulatedPlayer()) return ui::MenuManager::Presentation::Chest;
    auto  now  = std::chrono::steady_clock::now();
    if (gLastTold.size() > 512) gLastTold.clear();
    auto& lastTold = gLastTold;
    auto& last = lastTold[keyOf(player)];
    if (now - last > std::chrono::seconds(3)) {
        last = now;
        player.sendMessage(trFor(
            player,
            "abp.companion.required",
            "§cAlleviatedBackpacks needs its client mod to show backpack screens. §7Download it here: §b{}",
            kClientModUrl
        ));
        log().info("companion: {} has no client mod; backpack screen refused", player.getRealName());
    }
    return ui::MenuManager::Presentation::Refuse;
}

void forgetPlayer(Player const& p) {
    forgetTabMenus(p);
    gSessions.erase(keyOf(p));
    detail::forgetContentsRates(p);
}

class NativeScreens {
public:
    void install() {
        mDisconnect = ll::event::EventBus::getInstance().emplaceListener<ll::event::PlayerDisconnectEvent>(
            [](ll::event::PlayerDisconnectEvent& ev) { forgetPlayer(ev.self()); }
        );
        ui::MenuManager::get().setPresentationGate(presentationFor);
        ui::MenuManager::Observer o;
        o.beforeShow = onBeforeShow;
        o.afterTick  = onAfterTick;
        o.closed     = onClosed;
        mObserver    = ui::MenuManager::get().addObserver(std::move(o));
    }
    void uninstall() {
        ui::MenuManager::get().removeObserver(mObserver);
        ui::MenuManager::get().setPresentationGate(nullptr);
        ll::event::EventBus::getInstance().removeListener(mDisconnect);
        gSessions.clear();
        gTabMenus.clear();
    }

private:
    int                    mObserver = 0;
    ll::event::ListenerPtr mDisconnect;
};

} // namespace abp::net

ABP_REGISTER_MODULE_ORDERED(abp::net::NativeScreens, abp::ModuleOrder::Menu + 6);
