#include "net/NativeScreens.h"

#include <algorithm>
#include <optional>
#include <unordered_map>

#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/Level.h"

#include "core/Sorting.h"
#include "feature/gui/Gui.h"
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

/// Sends the patch from the last sent model to the current one (if anything changed).
void pushUpdate(Player& player, ui::BackpackMenu& menu, NativeSession& s, bool force = false) {
    auto model        = buildScreenModel(player, menu);
    model.session     = s.id;
    model.containerId = s.containerId;
    applyError(model, s);
    auto patch = core::screen::makePatch(s.sent, model);
    s.revision   = menu.backpack()->revision();
    s.sinceBuild = 0;
    s.dirty      = false;
    if (patch.empty() && !force) return;
    s.sent = std::move(model);
    send(player, ScreenUpdatePacket{nlohmann::json{{"session", s.id}, {"patch", std::move(patch)}}});
}

void onBeforeShow(Player& player, ui::Menu& menu, int containerId) {
    auto* bm = backpackMenuOf(menu);
    if (!bm || uiModeFor(player) != UiMode::Native) return;
    NativeSession s;
    s.id          = gNextSession++;
    s.menu        = &menu;
    s.containerId = containerId;
    s.sent        = buildScreenModel(player, *bm);
    s.sent.session     = s.id;
    s.sent.containerId = containerId;
    s.revision         = bm->backpack()->revision();
    send(player, ScreenOpenPacket{s.sent.toJson()});
    gSessions[keyOf(player)] = std::move(s);
}

void onAfterTick(Player& player, ui::Menu& menu) {
    auto it = gSessions.find(keyOf(player));
    if (it == gSessions.end() || it->second.menu != &menu) return;
    auto* bm = backpackMenuOf(menu);
    if (!bm) return;
    auto& s = it->second;
    if (s.errorTicks > 0 && --s.errorTicks == 0) setError(s, {}, {});
    bool changed = bm->backpack()->revision() != s.revision || s.dirty;
    if (!changed && ++s.sinceBuild < kRebuildEvery) return;
    pushUpdate(player, *bm, s);
}

void onClosed(Player* player, ui::Menu& menu) {
    for (auto it = gSessions.begin(); it != gSessions.end(); ++it) {
        if (it->second.menu != &menu) continue;
        if (player) send(*player, ScreenClosePacket{nlohmann::json{{"session", it->second.id}}});
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
    } else {
        content = bp->data().upgrades[static_cast<std::size_t>(index)].item;
    }

    // Upgrade slot changes show SB's red error overlay instead of a chat message.
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
                setError(s, reason, {index});
                return false;
            }
        }
    }

    if (shift) {
        if (content.empty()) return false;
        int target = quickMoveTarget(p, content, menu.lockedInventorySlot);
        if (target < 0) return false;
        int amount = area == Area::Storage ? std::min(content.count, maxTake) : 1;
        bool ok    = mm.simulateOn(p, act(ActionType::Take, here, {Where::Inventory, target}, amount), slot);
        // Ctrl+Shift: SC's "transfer full slot" (all of an over-sized storage slot).
        for (int guard = 0; ok && ctrl && area == Area::Storage && guard < 64; ++guard) {
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
    return mm.simulateOn(p, act(ActionType::Swap, here, cur, 0), slot);
}

bool clickInventory(Player& p, ui::BackpackMenu& menu, int index, int button, bool shift) {
    if (index < 0 || index >= kInventory) return false;
    auto&     mm     = MenuManager::get();
    ItemStack item   = p.getInventory().getItem(index);
    ItemStack cursor = cursorOf(p);
    MenuManager::SlotAddress here{Where::Inventory, index};
    MenuManager::SlotAddress cur{Where::Cursor, 0};
    if (shift) {
        if (item.isNull()) return false;
        // Into the backpack (menu quick move: filters, memory slots, quick-move handlers).
        auto slot = menu.storageSlot(0);
        if (!slot) return false;
        return mm.simulateOn(p, act(ActionType::Place, here, {Where::Menu, 0}, item.mCount), slot);
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

/// Double click with an item on the cursor: gather the same item from storage and inventory.
bool collectToCursor(Player& p, ui::BackpackMenu& menu) {
    auto& mm = MenuManager::get();
    bool  any = false;
    for (int pass = 0; pass < 2; ++pass) {
        int limit = pass == 0 ? menu.backpack()->inventory().size() : kInventory;
        for (int i = 0; i < limit; ++i) {
            ItemStack cursor = cursorOf(p);
            if (cursor.isNull() || cursor.mCount >= cursor.getMaxStackSize()) return any;
            int room = cursor.getMaxStackSize() - cursor.mCount;
            if (pass == 0) {
                auto const& d = menu.backpack()->inventory().at(i);
                if (!sameItem(cursor, d)) continue;
                any |= mm.simulateOn(p, act(ActionType::Take, {Where::Menu, i}, {Where::Cursor, 0}, std::min(room, d.count)), menu.storageSlot(i));
            } else {
                auto const& it = p.getInventory().getItem(i);
                if (it.isNull() || !it.matchesItem(cursor)) continue;
                any |= mm.simulate(p, act(ActionType::Take, {Where::Inventory, i}, {Where::Cursor, 0}, std::min<int>(room, it.mCount)));
            }
        }
    }
    return any;
}

ui::BackpackMenu* currentMenu(Player& p, std::uint32_t session, NativeSession*& out) {
    auto it = gSessions.find(keyOf(p));
    if (it == gSessions.end() || it->second.id != session || session == 0) return nullptr;
    auto menu = MenuManager::get().current(p);
    if (!menu || menu.get() != it->second.menu) return nullptr;
    out = &it->second;
    return backpackMenuOf(*menu);
}

std::string translatedUpgradeName(Player& p, upgrade::IUpgrade const& type) { return upgrade::displayName(type, &p); }
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
    NativeSession* s    = nullptr;
    auto*          menu = currentMenu(player, body.value("session", 0u), s);
    if (!menu || !s) return false;
    auto action = body.value("action", std::string{});
    auto bp     = menu->backpack();
    bool ok     = false;
    bool items  = false; // the player's inventory/cursor may have changed

    if (action == "click" || action == "double" || action == "drop" || action == "hotbar") {
        items     = true;
        auto area = areaOf(body);
        int  idx  = body.value("index", -1);
        bool inRange = (area == Area::Storage && idx >= 0 && idx < bp->inventory().size())
                    || (area == Area::Inventory && idx >= 0 && idx < kInventory)
                    || (area == Area::Upgrade && idx >= 0 && idx < static_cast<int>(bp->data().upgrades.size()));
        if (!inRange) return false;
        if (!s->errorMessage.empty()) setError(*s, {}, {}); // SB clears the overlay on the next click
        auto& mm = MenuManager::get();
        if (action == "click") {
            int  button = body.value("button", 0);
            bool shift  = body.value("shift", false);
            bool ctrl   = body.value("ctrl", false);
            ok = area == Area::Inventory ? clickInventory(player, *menu, idx, button, shift)
                                         : clickMenuSlot(player, *menu, *s, area, idx, button, shift, ctrl);
        } else if (action == "double") {
            ok = collectToCursor(player, *menu);
        } else if (action == "drop") {
            bool all = body.value("all", false);
            if (area == Area::Storage) {
                int n = all ? std::clamp(bp->inventory().at(idx).maxStack, 1, 255) : 1;
                ok    = mm.simulateOn(player, act(ActionType::Drop, {Where::Menu, idx}, {}, n), menu->storageSlot(idx));
            } else if (area == Area::Inventory) {
                int n = all ? 255 : 1;
                ok    = mm.simulate(player, act(ActionType::Drop, {Where::Inventory, idx}, {}, n));
            }
        } else { // hotbar
            int hb = body.value("hotbar", -1);
            if (hb < 0 || hb > 8) return false;
            if (area == Area::Storage) {
                ok = mm.simulateOn(player, act(ActionType::Swap, {Where::Menu, idx}, {Where::Inventory, hb}, 0), menu->storageSlot(idx));
            } else if (area == Area::Inventory && idx != hb) {
                ok = mm.simulate(player, act(ActionType::Swap, {Where::Inventory, idx}, {Where::Inventory, hb}, 0));
            }
        }
    } else if (action == "sort") {
        ok = feature::gui::sortBackpack(*bp, feature::gui::effective(&player, *bp).sortBy);
    } else if (action == "sortBy") {
        auto next = core::nextSortBy(feature::gui::effective(&player, *bp).sortBy);
        feature::gui::setBackpackOption(*bp, feature::gui::kSortBy, std::string(core::toString(next)));
        ok = true;
    } else if (action == "transfer") {
        items = true;
        feature::gui::TransferOptions opt;
        opt.matchingOnly = body.value("matching", true);
        opt.keepHotbar   = feature::gui::effective(&player, *bp).keepHotbar;
        opt.lockedSlot   = menu->lockedInventorySlot;
        int moved        = body.value("to", std::string{}) == "inventory" ? feature::gui::transferToPlayer(player, *bp, opt)
                                                                          : feature::gui::transferToBackpack(player, *bp, opt);
        ok = moved > 0;
    } else if (action == "toggleUpgrade") {
        int slot = body.value("slot", -1);
        if (slot < 0 || slot >= static_cast<int>(bp->data().upgrades.size())) return false;
        auto const& st   = bp->data().upgrades[static_cast<std::size_t>(slot)];
        auto*       type = st.empty() ? nullptr : upgrade::upgradeFor(st.item);
        if (!type || !type->info().toggleable) return false;
        upgrade::setEnabled(bp, slot, !st.enabled);
        ok = true;
    } else if (action == "settings") {
        menu->showSettings(player);
        return true;
    } else if (action == "upgradeSettings") {
        int slot = body.value("slot", -1);
        if (slot < 0 || slot >= static_cast<int>(bp->data().upgrades.size())) return false;
        auto const& st   = bp->data().upgrades[static_cast<std::size_t>(slot)];
        auto*       type = st.empty() ? nullptr : upgrade::upgradeFor(st.item);
        if (!type || !type->info().hasSettings) return false;
        type->openSettings(player, upgrade::UpgradeContext(bp, slot, &player));
        return true;
    } else if (action == "tab") {
        int slot = body.value("slot", -1);
        if (slot < 0 || slot >= static_cast<int>(bp->data().upgrades.size())) return false;
        auto const& st   = bp->data().upgrades[static_cast<std::size_t>(slot)];
        auto*       type = st.empty() ? nullptr : upgrade::upgradeFor(st.item);
        if (!type) return false;
        auto data = body.value("data", nlohmann::json::object());
        for (auto const& h : TabActionRegistry::entries()) {
            if (h.handle && matchesUpgrade(h.upgrade, *type) && h.handle(player, upgrade::UpgradeContext(bp, slot, &player), data)) {
                ok = true;
                break;
            }
        }
    } else if (action == "openNested") {
        int idx = body.value("index", -1);
        if (idx < 0 || idx >= bp->inventory().size()) return false;
        if (bp->inventory().at(idx).count != 1) return false; // SB: only a single backpack item
        return upgrade::inception::openNested(player, menu, bp, idx);
    } else if (action == "back") {
        auto* nested = dynamic_cast<upgrade::inception::NestedBackpackMenu*>(menu);
        if (!nested) return false;
        nested->goBack(player);
        return true;
    } else if (action == "close") {
        MenuManager::get().close(player);
        return true;
    } else if (action == "resync") {
        s->sent = {}; // forces a full patch
        pushUpdate(player, *menu, *s, true);
        return true;
    } else {
        return false;
    }

    if (items) MenuManager::get().resyncPlayer(player);
    // The menu may have been replaced by a callback (e.g. Inception's storage click handler).
    NativeSession* again = nullptr;
    if (auto* still = currentMenu(player, body.value("session", 0u), again); still && again) pushUpdate(player, *still, *again);
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

class NativeScreens {
public:
    void install() {
        ui::MenuManager::Observer o;
        o.beforeShow = onBeforeShow;
        o.afterTick  = onAfterTick;
        o.closed     = onClosed;
        mObserver    = ui::MenuManager::get().addObserver(std::move(o));
    }
    void uninstall() {
        ui::MenuManager::get().removeObserver(mObserver);
        gSessions.clear();
    }

private:
    int mObserver = 0;
};

} // namespace abp::net

ABP_REGISTER_MODULE_ORDERED(abp::net::NativeScreens, abp::ModuleOrder::Menu + 6);
