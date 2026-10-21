// Sort and "move all" actions, the control-row buttons, shift-click behaviour, slot hints and
// sneak + use to open the worn backpack.

#include <algorithm>
#include <unordered_map>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/Level.h"

#include "feature/gui/Gui.h"
#include "game/Backpack.h"
#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/PlayerScan.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "mod/Module.h"
#include "ui/menu/BackpackMenu.h"
#include "ui/menu/MenuManager.h"

namespace abp::feature::gui {

namespace {
constexpr int kInventorySlots = 36;
constexpr int kHotbarSlots    = 9;

/// Player inventory slots in fill order: main inventory first, then the hotbar.
std::vector<int> playerSlots(TransferOptions const& opt) {
    std::vector<int> out;
    for (int i = kHotbarSlots; i < kInventorySlots; ++i) out.push_back(i);
    if (!opt.keepHotbar) {
        for (int i = 0; i < kHotbarSlots; ++i) out.push_back(i);
    }
    if (opt.lockedSlot) std::erase(out, *opt.lockedSlot);
    return out;
}

/// Puts up to `count` of `item` into the player's inventory slots. Returns how many fit.
int giveToPlayer(Player& player, core::ItemStackData const& item, int count, std::vector<int> const& slots, bool simulate) {
    auto& inv  = player.getInventory();
    int   left = count;
    int   size = inv.getContainerSize();
    // Pass 0: merge into stacks of the same item. Pass 1: empty slots.
    for (int pass = 0; pass < 2 && left > 0; ++pass) {
        for (int i : slots) {
            if (left <= 0) break;
            if (i < 0 || i >= size) continue;
            auto const& cur = inv.getItem(i);
            if (pass == 0) {
                if (cur.isNull() || !game::ItemConv::toData(cur).sameItem(item)) continue;
                int room = std::max(0, static_cast<int>(cur.getMaxStackSize()) - static_cast<int>(cur.mCount));
                int n    = std::min(room, left);
                if (n <= 0) continue;
                if (!simulate) {
                    ItemStack copy = cur;
                    copy.set(cur.mCount + n);
                    inv.setItem(i, copy);
                }
                left -= n;
            } else {
                if (!cur.isNull()) continue;
                int max = std::clamp(item.maxStack, 1, 255);
                int n   = std::min(max, left);
                if (!simulate) inv.setItem(i, game::ItemConv::toStack(item, n));
                left -= n;
            }
        }
    }
    return count - left;
}

bool playerHas(Player& player, core::ItemStackData const& item) {
    auto& inv = player.getInventory();
    for (int i = 0; i < kInventorySlots; ++i) {
        auto const& s = inv.getItem(i);
        if (!s.isNull() && core::memoryMatches(game::ItemConv::toData(s), item)) return true;
    }
    return false;
}

bool backpackHas(game::Backpack& bp, core::ItemStackData const& item) {
    auto& inv = bp.inventory();
    for (int i = 0; i < inv.size(); ++i) {
        if (core::memoryMatches(inv.at(i), item)) return true;
    }
    auto const& mem = memory(bp);
    return std::any_of(mem.begin(), mem.end(), [&](core::ItemStackData const& m) { return core::memoryMatches(m, item); });
}

TransferOptions transferOptions(ui::BackpackMenuContext const& c, bool matchingOnly) {
    TransferOptions o;
    o.matchingOnly = matchingOnly;
    o.keepHotbar   = effective(&c.player, *c.backpack).keepHotbar;
    o.lockedSlot   = c.menu.lockedInventorySlot;
    return o;
}
} // namespace

// ---- actions --------------------------------------------------------------------------------------

bool sortBackpack(game::Backpack& backpack, core::SortBy by) {
    core::SortLayout layout;
    layout.locked = noSort(backpack);
    layout.memory = memory(backpack);
    return core::sortInventory(backpack.inventory(), core::comparatorFor(by, &game::ItemConv::tags()), layout);
}

int transferToBackpack(Player& player, game::Backpack& backpack, TransferOptions const& opt) {
    auto& inv   = player.getInventory();
    int   moved = 0;
    for (int i : playerSlots(opt)) {
        auto const& st = inv.getItem(i);
        if (st.isNull()) continue;
        auto data = game::ItemConv::toData(st);
        if (opt.matchingOnly && !backpackHas(backpack, data)) continue;
        auto r    = backpack.inventory().insert(data, core::AccessContext{core::Source::Player, &player});
        int  used = std::min(r.inserted + r.voided, static_cast<int>(st.mCount));
        if (used <= 0) continue;
        if (used >= st.mCount) {
            inv.setItem(i, ItemStack::EMPTY_ITEM());
        } else {
            ItemStack copy = st;
            copy.set(st.mCount - used);
            inv.setItem(i, copy);
        }
        moved += used;
    }
    if (moved > 0) player.sendInventory(false);
    return moved;
}

int transferToPlayer(Player& player, game::Backpack& backpack, TransferOptions const& opt) {
    auto&       inv   = backpack.inventory();
    auto const  slots = playerSlots(opt);
    int         moved = 0;
    core::AccessContext ctx{core::Source::Player, &player};
    for (int s = 0; s < inv.size(); ++s) {
        while (!inv.at(s).empty()) {
            auto const cur = inv.at(s);
            if (opt.matchingOnly && !playerHas(player, cur)) break;
            int fits = giveToPlayer(player, cur, cur.count, slots, true);
            if (fits <= 0) break;
            auto got = inv.extract(s, fits, ctx);
            if (got.empty()) break;
            int given = giveToPlayer(player, got, got.count, slots, false);
            moved    += given;
            if (given < got.count) { // should not happen; put the rest back
                (void)inv.insertInto(s, got.withCount(got.count - given), core::AccessContext{core::Source::Internal, &player});
                break;
            }
        }
    }
    if (moved > 0) player.sendInventory(false);
    return moved;
}

void notify(Player& player, game::Backpack const& backpack, std::string const& message) {
    if (effective(&player, backpack).notifications) player.sendMessage(message);
}

} // namespace abp::feature::gui

// ---- buttons ------------------------------------------------------------------------------------

namespace {
using namespace abp;
using namespace abp::feature::gui;
using ui::BackpackMenuContext;
using ui::ClickContext;
using ui::ClickType;

ABP_REGISTER_BACKPACK_BUTTON(ui::BackpackButton{
    .id    = "gui.sort",
    .order = 10,
    .icon =
        [](BackpackMenuContext const& c) {
            auto by = effective(&c.player, *c.backpack).sortBy;
            return ui::icon(
                "minecraft:hopper",
                trFor(c.player, "abp.gui.sort", "Sort (by {})", sortByLabel(c.player, by)),
                {trFor(c.player, "abp.gui.sort.lore1", "§7Click: sort this backpack"),
                 trFor(c.player, "abp.gui.sort.lore2", "§7Shift-click: change the sort mode")}
            );
        },
    .onClick =
        [](BackpackMenuContext const& c, ClickContext const& click) {
            auto& bp = *c.backpack;
            auto  by = effective(&c.player, bp).sortBy;
            if (click.type == ClickType::QuickMove) {
                auto next = core::nextSortBy(by);
                setBackpackOption(bp, kSortBy, std::string(core::toString(next)));
                notify(c.player, bp, trFor(c.player, "abp.gui.sort.mode", "§7Sort mode: §f{}", sortByLabel(c.player, next)));
                c.menu.invalidate();
                return;
            }
            if (sortBackpack(bp, by)) {
                notify(c.player, bp, trFor(c.player, "abp.gui.sort.done", "§7Sorted by {}.", sortByLabel(c.player, by)));
            } else {
                c.player.sendMessage(trFor(c.player, "abp.gui.sort.no_room", "§cNot enough room to sort."));
            }
        },
    .label = [](BackpackMenuContext const& c) { return trFor(c.player, "abp.gui.sort.label", "Sort"); },
});

ABP_REGISTER_BACKPACK_BUTTON(ui::BackpackButton{
    .id    = "gui.to_backpack",
    .order = 20,
    .icon =
        [](BackpackMenuContext const& c) {
            return ui::icon(
                "minecraft:barrel",
                trFor(c.player, "abp.gui.to_backpack", "Move all into the backpack"),
                {trFor(c.player, "abp.gui.to_backpack.lore1", "§7Click: everything from your inventory"),
                 trFor(c.player, "abp.gui.transfer.lore_matching", "§7Shift-click: only items already there"),
                 effective(&c.player, *c.backpack).keepHotbar
                     ? trFor(c.player, "abp.gui.transfer.lore_hotbar", "§8The hotbar is kept (see settings)")
                     : trFor(c.player, "abp.gui.transfer.lore_with_hotbar", "§8Includes the hotbar (see settings)")}
            );
        },
    .onClick =
        [](BackpackMenuContext const& c, ClickContext const& click) {
            int n = transferToBackpack(c.player, *c.backpack, transferOptions(c, click.type == ClickType::QuickMove));
            notify(c.player, *c.backpack, trFor(c.player, "abp.gui.to_backpack.done", "§7Moved {} items into the backpack.", n));
        },
    .label = [](BackpackMenuContext const& c) { return trFor(c.player, "abp.gui.to_backpack.label", "Move all into backpack"); },
});

ABP_REGISTER_BACKPACK_BUTTON(ui::BackpackButton{
    .id    = "gui.to_player",
    .order = 30,
    .icon =
        [](BackpackMenuContext const& c) {
            return ui::icon(
                "minecraft:dropper",
                trFor(c.player, "abp.gui.to_player", "Move all to your inventory"),
                {trFor(c.player, "abp.gui.to_player.lore1", "§7Click: everything that fits"),
                 trFor(c.player, "abp.gui.transfer.lore_matching", "§7Shift-click: only items already there"),
                 effective(&c.player, *c.backpack).keepHotbar
                     ? trFor(c.player, "abp.gui.transfer.lore_hotbar", "§8The hotbar is kept (see settings)")
                     : trFor(c.player, "abp.gui.transfer.lore_with_hotbar", "§8Includes the hotbar (see settings)")}
            );
        },
    .onClick =
        [](BackpackMenuContext const& c, ClickContext const& click) {
            int n = transferToPlayer(c.player, *c.backpack, transferOptions(c, click.type == ClickType::QuickMove));
            notify(c.player, *c.backpack, trFor(c.player, "abp.gui.to_player.done", "§7Moved {} items to your inventory.", n));
        },
    .label = [](BackpackMenuContext const& c) { return trFor(c.player, "abp.gui.to_player.label", "Move all to inventory"); },
});

// Shift-click on the upgrades page: the open "tab" (upgrade slots) first, or storage first.
ABP_REGISTER_BACKPACK_QUICK_MOVE(ui::BackpackQuickMoveHandler{
    .id    = "gui.open_tab_first",
    .order = 100,
    .handle = [](BackpackMenuContext const& c, ItemStack const& stack) -> std::optional<int> {
        if (c.menu.view() != ui::BackpackMenu::View::Upgrades) return std::nullopt;
        if (effective(&c.player, *c.backpack).openTabFirst) {
            if (int n = c.menu.quickMoveToUpgrades(c.player, stack); n > 0) return n;
            return c.menu.quickMoveToStorage(c.player, stack);
        }
        if (int n = c.menu.quickMoveToStorage(c.player, stack); n > 0) return n;
        return c.menu.quickMoveToUpgrades(c.player, stack);
    },
});

// Memory slots show their item as a ghost when empty; slot hints in the lore.
ABP_REGISTER_BACKPACK_SLOT_DECORATOR(ui::BackpackSlotDecorator{
    .id    = "gui.slot_hints",
    .order = 100,
    .decorate =
        [](BackpackMenuContext const& c, int index, ItemStack& shown) {
            auto& bp  = *c.backpack;
            auto const& mem = memory(bp);
            auto const& ns  = noSort(bp);
            bool hasMem = index >= 0 && index < static_cast<int>(mem.size()) && !mem[static_cast<std::size_t>(index)].empty();
            bool isNs   = index >= 0 && index < static_cast<int>(ns.size()) && ns[static_cast<std::size_t>(index)];
            if (!hasMem && !isNs) return;
            if (shown.isNull()) {
                if (!hasMem) return; // empty no-sort slots stay empty (dropping items into them must work)
                shown = game::ItemConv::toStack(mem[static_cast<std::size_t>(index)], 1);
                game::ItemConv::setDisplay(
                    shown,
                    {},
                    {trFor(c.player, "abp.gui.memory.ghost", "§bMemory slot §7(empty)"),
                     trFor(c.player, "abp.gui.memory.ghost2", "§7Only this item goes here")}
                );
                return;
            }
            if (!effective(&c.player, bp).notifications) return;
            auto lore = shown.getCustomLore();
            if (hasMem) lore.push_back(trFor(c.player, "abp.gui.memory.hint", "§bMemory slot"));
            if (isNs) lore.push_back(trFor(c.player, "abp.gui.nosort.hint", "§eNo-sort slot"));
            shown.setCustomLore(lore);
        },
});

// Sneak + use with an empty hand on a block opens the backpack worn in the chest slot.
class SneakOpen {
public:
    void install() {
        mListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::PlayerInteractBlockEvent>(
            [this](ll::event::PlayerInteractBlockEvent& ev) {
                auto& player = ev.self();
                if (player.isClientSide() || !config::get<GuiConfig>().sneakUseOpensWornBackpack) return;
                if (!ev.item().isNull() || !player.isSneaking()) return;
                game::ItemLocation chest{
                    game::ItemLocation::Kind::Armor,
                    static_cast<int>(SharedTypes::Legacy::ArmorSlot::Torso)
                };
                if (!game::isBackpack(game::itemAt(player, chest))) return;
                ev.cancel();
                auto level = ll::service::getLevel();
                std::uint64_t now = level ? static_cast<std::uint64_t>(level->getCurrentServerTick().tickID) : 0;
                auto key   = player.getOrCreateUniqueID().rawID;
                if (auto it = mLast.find(key); it != mLast.end() && now - it->second < 10) return;
                mLast[key] = now;
                if (ui::MenuManager::get().current(player)) return;
                ui::BackpackMenu::openFor(player, chest);
            }
        );
    }
    void uninstall() {
        ll::event::EventBus::getInstance().removeListener(mListener);
        mLast.clear();
    }

private:
    ll::event::ListenerPtr                          mListener;
    std::unordered_map<std::int64_t, std::uint64_t> mLast;
};
} // namespace

ABP_REGISTER_MODULE(SneakOpen);
