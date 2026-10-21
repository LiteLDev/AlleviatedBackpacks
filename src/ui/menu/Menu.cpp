#include "ui/menu/Menu.h"

#include "mc/world/actor/player/Player.h"

namespace abp::ui {

Menu::Menu(std::string title, Rows rows) : mTitle(std::move(title)), mRows(rows), mSlots(static_cast<std::size_t>(size())) {}

void Menu::setSlot(int index, std::shared_ptr<Slot> slot) {
    if (index < 0 || index >= size()) return;
    mSlots[static_cast<std::size_t>(index)] = std::move(slot);
}

Slot* Menu::slot(int index) const {
    if (index < 0 || index >= size()) return nullptr;
    return mSlots[static_cast<std::size_t>(index)].get();
}

void Menu::clearSlots() {
    for (auto& s : mSlots) s.reset();
}

void Menu::ensureBuilt(Player& player) {
    if (!mNeedsBuild) return;
    mNeedsBuild = false;
    clearSlots();
    build(player);
}

int Menu::quickMoveIn(Player& player, ItemStack const& stack) {
    if (stack.isNull()) return 0;
    int  left  = stack.mCount;
    // Pass 1: occupied slots (merging), pass 2: empty slots. `accept()` refuses what doesn't fit.
    auto offer = [&](bool occupied) {
        for (int i = 0; i < size() && left > 0; ++i) {
            auto* s = slot(i);
            if (!s || s->display(player).isNull() == occupied) continue;
            ItemStack part = stack;
            part.set(left);
            left -= s->accept(player, part);
        }
    };
    offer(true);
    offer(false);
    return stack.mCount - left;
}

} // namespace abp::ui
