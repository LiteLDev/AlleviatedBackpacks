#include <algorithm>

#include "mc/world/actor/player/Player.h"

#include "core/Format.h"
#include "game/Backpack.h"
#include "game/ItemConv.h"
#include "mod/I18n.h"
#include "ui/menu/Slot.h"

namespace abp::ui {

ItemStack icon(std::string_view itemId, std::string const& name, std::vector<std::string> const& lore, int count) {
    auto s = game::ItemConv::make(itemId, count, 0);
    if (s.isNull()) return s;
    // A leading reset keeps the name white instead of the default italic custom-name style.
    game::ItemConv::setDisplay(s, name.empty() ? std::string{} : "§r" + name, lore);
    return s;
}

ItemStack GhostSlot::display(Player&) const {
    auto g = mGet ? mGet() : core::ItemStackData{};
    if (g.empty()) return {};
    return game::ItemConv::toStack(g, 1);
}

bool GhostSlot::click(ClickContext const& ctx) {
    switch (ctx.type) {
    case ClickType::Place:
    case ClickType::Swap:
    case ClickType::QuickMove:
        if (!ctx.offered.isNull() && mSet) {
            auto d = game::ItemConv::toData(ctx.offered);
            mSet(d.withCount(1));
        }
        break;
    case ClickType::Take:
    case ClickType::Drop:
        if (mSet) mSet({});
        break;
    }
    return true;
}

ItemStack StorageSlot::display(Player& player) const {
    auto const& inv = mBackpack->inventory();
    if (mIndex < 0 || mIndex >= inv.size()) return {};
    auto const& d = inv.at(mIndex);
    if (d.empty()) return {};
    int  shown = std::min({d.count, std::max(d.maxStack, 1), 255});
    auto s     = game::ItemConv::toStack(d, shown);
    if (d.count > shown) {
        game::ItemConv::setDisplay(
            s,
            {},
            {trFor(player, "abp.menu.stored_count", "§7Stored: §f{}", core::compactCount(d.count) + (d.count >= 1000 ? " (" + std::to_string(d.count) + ")" : ""))}
        );
    }
    return s;
}

bool StorageSlot::click(ClickContext const& ctx) {
    for (auto const& handler : StorageClickRegistry::entries()) {
        if (handler && handler(ctx, mBackpack, mIndex)) return true;
    }
    return false;
}

ItemStack StorageSlot::take(Player& player, int amount) {
    auto& inv = mBackpack->inventory();
    if (mIndex < 0 || mIndex >= inv.size()) return {};
    auto const& d = inv.at(mIndex);
    if (d.empty()) return {};
    amount   = std::min({amount, std::max(d.maxStack, 1), 255});
    auto got = inv.extract(mIndex, amount, core::AccessContext{core::Source::Player, &player});
    if (got.empty()) return {};
    return game::ItemConv::toStack(got, got.count);
}

int StorageSlot::accept(Player& player, ItemStack const& stack) {
    auto& inv = mBackpack->inventory();
    if (stack.isNull() || mIndex < 0 || mIndex >= inv.size()) return 0;
    auto r = inv.insertInto(mIndex, game::ItemConv::toData(stack), core::AccessContext{core::Source::Player, &player});
    return r.inserted + r.voided;
}

} // namespace abp::ui
