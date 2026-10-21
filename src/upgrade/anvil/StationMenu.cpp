#include "upgrade/anvil/StationMenu.h"

#include <algorithm>

#include "ll/api/chrono/GameChrono.h"
#include "ll/api/coro/CoroTask.h"
#include "ll/api/service/Bedrock.h"
#include "ll/api/thread/ServerThreadExecutor.h"

#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/Level.h"

#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/Storage.h"
#include "mod/I18n.h"
#include "ui/menu/BackpackMenu.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"

namespace abp::upgrade::anvil {

// ---- StationRef ---------------------------------------------------------------------------------

bool StationRef::valid() const {
    auto const& slots = mCtx.backpack().data().upgrades;
    if (mCtx.slot() < 0 || mCtx.slot() >= static_cast<int>(slots.size())) return false;
    auto* type = upgradeFor(slots[static_cast<std::size_t>(mCtx.slot())].item);
    return type && type->info().id == mId;
}

core::ItemStackData StationRef::item(std::string const& key) const {
    if (!valid()) return {};
    auto const& s = mCtx.settings();
    if (!s.is_object() || !s.contains(key)) return {};
    return core::ItemStackData::fromJson(s[key]);
}

ItemStack StationRef::stack(std::string const& key) const {
    auto d = item(key);
    if (d.empty()) return {};
    return game::ItemConv::toStack(d, d.count);
}

void StationRef::setItem(std::string const& key, core::ItemStackData const& data) const {
    if (!valid()) return;
    auto& s = mCtx.settings();
    if (!s.is_object()) s = nlohmann::json::object();
    if (data.empty()) s.erase(key);
    else s[key] = data.toJson();
    mCtx.saveSettings();
}

void StationRef::setStack(std::string const& key, ItemStack const& stack) const {
    setItem(key, stack.isNull() || stack.mCount == 0 ? core::ItemStackData{} : game::ItemConv::toData(stack));
}

void StationRef::consume(std::string const& key, int n) const {
    auto d = item(key);
    if (!d.empty() && n > 0) setItem(key, d.withCount(d.count - n));
}

std::optional<std::string> StationRef::text(std::string const& key) const {
    if (!valid()) return std::nullopt;
    auto const& s = mCtx.settings();
    if (!s.is_object() || !s.contains(key) || !s[key].is_string()) return std::nullopt;
    return s[key].get<std::string>();
}

void StationRef::setText(std::string const& key, std::optional<std::string> const& value) const {
    if (!valid()) return;
    auto& s = mCtx.settings();
    if (!s.is_object()) s = nlohmann::json::object();
    if (value) s[key] = *value;
    else if (s.contains(key)) s.erase(key);
    else return;
    mCtx.saveSettings();
}

// ---- InputSlot ----------------------------------------------------------------------------------

ItemStack InputSlot::display(Player&) const { return mRef.stack(mKey); }

ItemStack InputSlot::take(Player& player, int amount) {
    auto d = mRef.item(mKey);
    if (d.empty() || amount <= 0) return {};
    int  n   = std::min({amount, d.count, 255});
    auto out = game::ItemConv::toStack(d, n);
    mRef.setItem(mKey, d.withCount(d.count - n));
    if (mChanged) mChanged(player);
    return out;
}

int InputSlot::accept(Player& player, ItemStack const& stack) {
    if (stack.isNull() || stack.mCount <= 0 || !mRef.valid()) return 0;
    auto cur      = mRef.item(mKey);
    auto incoming = game::ItemConv::toData(stack);
    int  max      = std::max<int>(1, stack.getMaxStackSize());
    if (cur.empty()) {
        int n = std::min<int>(incoming.count, max);
        mRef.setItem(mKey, incoming.withCount(n));
        if (mChanged) mChanged(player);
        return n;
    }
    if (!cur.sameItem(incoming)) return 0;
    int n = std::min(incoming.count, std::max(0, max - cur.count));
    if (n <= 0) return 0;
    mRef.setItem(mKey, cur.withCount(cur.count + n));
    return n;
}

// ---- OutputSlot ---------------------------------------------------------------------------------

ItemStack OutputSlot::take(Player& player, int) {
    if (!mMenu.ref().valid() || !mCraft) return {};
    auto crafted = mCraft(player);
    if (!crafted || crafted->result.isNull()) return {};
    mMenu.pendingUndo = StationMenu::PendingUndo{crafted->result, std::move(crafted->undo)};
    return crafted->result;
}

int OutputSlot::accept(Player& player, ItemStack const& stack) {
    auto& pending = mMenu.pendingUndo;
    if (!pending || stack.isNull() || !stack.matchesItem(pending->result)) return 0;
    if (stack.mCount >= pending->result.mCount) {
        if (pending->undo) pending->undo(player);
    } else {
        // Partly handed back: give the rest to the player (never lose items).
        ItemStack rest = stack;
        if (!player.add(rest)) player.drop(rest, false);
    }
    int n = stack.mCount;
    pending.reset();
    return n;
}

// ---- StationMenu --------------------------------------------------------------------------------

StationMenu::StationMenu(std::string title, StationRef ref) : Menu(std::move(title), Rows::Six), mRef(std::move(ref)) {}

std::shared_ptr<ui::Slot> StationMenu::filler(std::string const& name) {
    return std::make_shared<ui::StaticSlot>(ui::icon("minecraft:gray_stained_glass_pane", name));
}

int StationMenu::pageCount() const {
    return std::max(1, (mRef.ctx().backpack().inventory().size() + kStorageSlots - 1) / kStorageSlots);
}

void StationMenu::build(Player& player) {
    for (int i = 0; i < kStorageStart; ++i) setSlot(i, filler());
    auto bp = mRef.ctx().backpackPtr();
    setSlot(
        0,
        std::make_shared<ui::ButtonSlot>(
            ui::icon("minecraft:chest", trFor(player, "abp.station.back", "Back to backpack")),
            [bp](ui::ClickContext const& c) {
                switchMenu(c.player, [bp](Player& p) { ui::BackpackMenu::openBackpack(p, bp); });
            }
        )
    );
    int pages = pageCount();
    mPage     = std::clamp(mPage, 0, pages - 1);
    if (mPage > 0) {
        setSlot(9, std::make_shared<ui::ButtonSlot>(ui::icon("minecraft:arrow", trFor(player, "abp.menu.prev", "Previous page")), [this](ui::ClickContext const&) {
            --mPage;
            invalidate();
        }));
    }
    if (mPage + 1 < pages) {
        setSlot(17, std::make_shared<ui::ButtonSlot>(ui::icon("minecraft:arrow", trFor(player, "abp.menu.next", "Next page")), [this](ui::ClickContext const&) {
            ++mPage;
            invalidate();
        }));
    }
    int size = bp->inventory().size();
    for (int i = 0; i < kStorageSlots; ++i) {
        int index = mPage * kStorageSlots + i;
        if (index < size) setSlot(kStorageStart + i, std::make_shared<ui::StorageSlot>(bp, index));
        else setSlot(kStorageStart + i, filler());
    }
    buildStation(player);
}

void StationMenu::onOpen(Player& player) {
    auto const& bp = mRef.ctx().backpack();
    for (auto const& c : game::scanBackpacks(player)) {
        if (c.uuid && *c.uuid == bp.uuid()) {
            mSource = c.where;
            if (c.where.kind == game::ItemLocation::Kind::Inventory) lockedInventorySlot = c.where.slot;
            break;
        }
    }
}

void StationMenu::onTick(Player& player) {
    pendingUndo.reset(); // a give-back only happens within the request that took the output
    bool ok = mRef.valid();
    if (ok && mSource) {
        auto const& item = game::itemAt(player, *mSource);
        auto        uuid = game::backpackUuid(item);
        ok               = game::isBackpack(item) && uuid && *uuid == mRef.ctx().backpack().uuid();
    }
    if (!ok) {
        ui::MenuManager::get().close(player);
    }
}

void StationMenu::onDestroyed() { game::Storage::get().save(mRef.ctx().backpack()); }

int StationMenu::quickMoveIn(Player& player, ItemStack const& stack) {
    if (stack.isNull()) return 0;
    int taken = quickMoveStation(player, stack);
    if (taken >= stack.mCount) return taken;
    auto rest = game::ItemConv::toData(stack).withCount(stack.mCount - taken);
    auto r    = mRef.ctx().backpack().inventory().insert(rest, core::AccessContext{core::Source::Player, &player});
    return taken + r.inserted + r.voided;
}

// ---- helpers ------------------------------------------------------------------------------------

std::optional<UpgradeContext> findInstalled(std::shared_ptr<game::Backpack> const& backpack, std::string_view upgradeId) {
    std::optional<UpgradeContext> found;
    forEachInstalled(backpack, true, [&](IUpgrade& u, UpgradeContext const& ctx) {
        if (!found && u.info().id == upgradeId) found = ctx;
    });
    return found;
}

std::vector<std::string> describeContents(Player& player, StationRef const& ref, std::vector<std::string> const& keys) {
    std::vector<std::string> out;
    for (auto const& key : keys) {
        auto s = ref.stack(key);
        if (s.isNull()) continue;
        auto name = s.getCustomName();
        if (name.empty()) name = s.getName();
        out.push_back(trFor(player, "abp.station.contains", "§7Holds: §f{} x{}", name, static_cast<int>(s.mCount)));
    }
    return out;
}

} // namespace abp::upgrade::anvil

namespace abp::upgrade::anvil {

void switchMenu(Player& player, std::function<void(Player&)> openNext) {
    auto& mm = ui::MenuManager::get();
    if (!mm.current(player)) {
        openNext(player);
        return;
    }
    mm.closeThen(player, [openNext = std::move(openNext)](Player& p) { openLater(p, std::move(openNext)); });
}

void openLater(Player& player, std::function<void(Player&)> openNext) {
    auto key = player.getOrCreateUniqueID();
    ll::coro::keepThis([key, openNext = std::move(openNext)]() -> ll::coro::CoroTask<> {
        co_await ll::chrono::ticks(kSwitchExtraTicks);
        auto level = ll::service::getLevel();
        if (!level) co_return;
        if (auto* pl = level->getPlayer(key); pl && !ui::MenuManager::get().current(*pl)) openNext(*pl);
    }).launch(ll::thread::ServerThreadExecutor::getDefault());
}

bool showMenu(Player& player, std::shared_ptr<ui::Menu> menu) {
    switchMenu(player, [menu = std::move(menu)](Player& p) { ui::MenuManager::get().open(p, menu); });
    return true;
}

} // namespace abp::upgrade::anvil
