#include "upgrade/anvil/StationNative.h"

#include <algorithm>
#include <optional>

#include "mc/world/PlayerUIContainer.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

#include "game/Backpack.h"
#include "game/ItemConv.h"
#include "ui/menu/MenuManager.h"
#include "ui/menu/Slot.h"

namespace abp::upgrade::anvil {

namespace {
using ui::MenuManager;
using Where      = MenuManager::SlotAddress::Where;
using ActionType = MenuManager::ActionType;

constexpr int kInventorySlots = 36;

MenuManager::SimAction act(ActionType type, MenuManager::SlotAddress src, MenuManager::SlotAddress dst, int amount) {
    MenuManager::SimAction a;
    a.type   = type;
    a.src    = src;
    a.dst    = dst;
    a.amount = amount;
    return a;
}

std::optional<int> lockedSlot(Player& player) {
    auto menu = MenuManager::get().current(player);
    return menu ? menu->lockedInventorySlot : std::nullopt;
}

/// Puts `data` into the player's main inventory, then the hotbar (stacks of the same item first,
/// then empty slots). Returns how many items did not fit.
int giveToInventory(Player& player, core::ItemStackData const& data) {
    auto& inv    = player.getInventory();
    auto  locked = lockedSlot(player);
    int   left   = data.count;
    std::vector<int> order;
    for (int i = 9; i < kInventorySlots; ++i) order.push_back(i);
    for (int i = 0; i < 9; ++i) order.push_back(i);
    int max = std::clamp(data.maxStack, 1, 255);
    for (int pass = 0; pass < 2 && left > 0; ++pass) {
        for (int i : order) {
            if (left <= 0) break;
            if (locked && *locked == i) continue;
            auto const& cur = inv.getItem(i);
            if (pass == 0) {
                if (cur.isNull() || !game::ItemConv::toData(cur).sameItem(data)) continue;
                int room = std::max(0, std::min<int>(max, cur.getMaxStackSize()) - cur.mCount);
                int n    = std::min(room, left);
                if (n <= 0) continue;
                inv.setItem(i, game::ItemConv::toStack(data, cur.mCount + n));
                left -= n;
            } else {
                if (!cur.isNull()) continue;
                int n = std::min(max, left);
                inv.setItem(i, game::ItemConv::toStack(data, n));
                left -= n;
            }
        }
    }
    return left;
}
} // namespace

nlohmann::json stackJson(ItemStack const& stack) {
    if (stack.isNull() || stack.mCount <= 0) return nullptr;
    return game::ItemConv::toData(stack).toJson();
}

bool clickStationSlot(Player& player, std::shared_ptr<ui::Slot> const& slot, int button, bool shift, game::Backpack& backpack) {
    auto& mm = MenuManager::get();
    if (!slot) return false;
    ItemStack shown  = slot->display(player);
    ItemStack cursor = player.mPlayerUIContainer->getItem(0);
    MenuManager::SlotAddress here{Where::Menu, 0};
    MenuManager::SlotAddress cur{Where::Cursor, 0};
    if (shift) {
        if (shown.isNull()) return false;
        ItemStack taken = slot->take(player, shown.mCount);
        if (taken.isNull()) return false;
        auto data = game::ItemConv::toData(taken);
        auto r    = backpack.inventory().insert(data, core::AccessContext{core::Source::Player, &player});
        int  left = r.remainder.empty() ? 0 : r.remainder.count;
        if (left > 0) left = giveToInventory(player, data.withCount(left));
        if (left > 0) {
            // Didn't fit anywhere: back into the slot (an output slot undoes the craft).
            ItemStack back = game::ItemConv::toStack(data, left);
            int       acc  = slot->accept(player, back);
            if (acc < left) {
                ItemStack rest = game::ItemConv::toStack(data, left - acc);
                if (!player.add(rest)) player.drop(rest, false);
            }
            return left < data.count;
        }
        return true;
    }
    if (cursor.isNull()) {
        if (shown.isNull()) return false;
        int have = static_cast<int>(shown.mCount);
        return mm.simulateOn(player, act(ActionType::Take, here, cur, button == 1 ? (have + 1) / 2 : have), slot);
    }
    if (shown.isNull() || game::ItemConv::toData(shown).sameItem(game::ItemConv::toData(cursor))) {
        return mm.simulateOn(player, act(ActionType::Place, cur, here, button == 1 ? 1 : static_cast<int>(cursor.mCount)), slot);
    }
    return mm.simulateOn(player, act(ActionType::Swap, here, cur, 0), slot);
}

} // namespace abp::upgrade::anvil
