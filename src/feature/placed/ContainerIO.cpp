#include "feature/placed/ContainerIO.h"

#include <algorithm>

#include "mc/world/Container.h"
#include "mc/world/Facing.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"

#include "game/ItemConv.h"

namespace abp::placed {

namespace {
/// Puts `items` back into `slot` (where they were just extracted from) without any filter.
/// Falls back to a normal internal insert if the slot changed meanwhile.
void restore(core::BackpackInventory& inv, int slot, core::ItemStackData const& items) {
    if (items.empty()) return;
    auto const& cur = inv.at(slot);
    if (cur.empty()) {
        inv.set(slot, items);
    } else if (cur.sameItem(items)) {
        inv.set(slot, cur.withCount(cur.count + items.count));
    } else {
        auto r = inv.insert(items, core::AccessContext{core::Source::Internal});
        // Last resort: any empty slot, unchecked.
        for (int i = 0; i < inv.size() && !r.remainder.empty(); ++i) {
            if (inv.at(i).empty()) {
                inv.set(i, r.remainder);
                r.remainder.clear();
            }
        }
    }
}
} // namespace

BlockPos neighbor(BlockPos const& pos, int face) {
    if (face < 0 || face > 5) return pos;
    return pos + Facing::DIRECTION()[static_cast<std::size_t>(face)];
}

int addToContainer(Container& c, ItemStack const& item, int count, int face, bool simulate) {
    if (item.isNull() || count <= 0) return 0;
    int size  = c.getContainerSize();
    int added = 0;
    int limit = std::max(1, std::min<int>(c.getMaxStackSize(), item.getMaxStackSize()));
    // Pass 1: top up matching stacks. Pass 2: empty slots.
    for (int pass = 0; pass < 2 && added < count; ++pass) {
        for (int slot = 0; slot < size && added < count; ++slot) {
            if (c.isSlotDisabled(slot)) continue;
            auto const& cur = c.getItem(slot);
            if (pass == 0) {
                if (cur.isNull() || !cur.isStackable(item)) continue;
                int room = limit - static_cast<int>(cur.mCount);
                if (room <= 0 || !c.canPushInItem(slot, face, item)) continue;
                int n = std::min(room, count - added);
                if (!simulate) {
                    ItemStack next = cur;
                    next.set(static_cast<int>(cur.mCount) + n);
                    c.setItem(slot, next);
                }
                added += n;
            } else {
                if (!cur.isNull() || !c.canPushInItem(slot, face, item)) continue;
                int n = std::min(limit, count - added);
                if (!simulate) {
                    ItemStack next = item;
                    next.set(n);
                    c.setItem(slot, next);
                }
                added += n;
            }
        }
    }
    return added;
}

int pullFromContainer(
    Container&                 from,
    core::BackpackInventory&   inv,
    int                        max,
    int                        face,
    core::AccessContext const& ctx,
    ItemPredicate const&       accept
) {
    int moved = 0;
    int size  = from.getContainerSize();
    for (int slot = 0; slot < size && moved < max; ++slot) {
        auto const& stack = from.getItem(slot);
        if (stack.isNull() || !from.canPullOutItem(slot, face, stack)) continue;
        auto data = game::ItemConv::toData(stack);
        if (accept && !accept(data)) continue;
        int  want = std::min(max - moved, data.count);
        auto r    = inv.insert(data.withCount(want), ctx, true);
        int  n    = r.inserted + r.voided;
        if (n <= 0) continue;
        auto real = inv.insert(data.withCount(n), ctx);
        n         = real.inserted + real.voided;
        if (n <= 0) continue;
        from.removeItem(slot, n);
        moved += n;
    }
    return moved;
}

int pushToContainer(
    core::BackpackInventory&   inv,
    Container&                 to,
    int                        max,
    int                        face,
    core::AccessContext const& ctx,
    ItemPredicate const&       accept
) {
    int moved = 0;
    for (int slot = 0; slot < inv.size() && moved < max; ++slot) {
        auto const& data = inv.at(slot);
        if (data.empty() || (accept && !accept(data))) continue;
        int want = std::min({max - moved, data.count, std::max(1, data.maxStack)});
        if (!inv.canExtract(slot, want, ctx)) continue;
        auto stack = game::ItemConv::toStack(data, want);
        if (stack.isNull()) continue;
        int n = addToContainer(to, stack, want, face, true);
        if (n <= 0) continue;
        auto taken = inv.extract(slot, n, ctx);
        if (taken.empty()) continue;
        int put = addToContainer(to, stack, taken.count, face);
        if (put < taken.count) restore(inv, slot, taken.withCount(taken.count - put)); // never lose items
        moved += put;
    }
    return moved;
}

int moveBetween(
    core::BackpackInventory&   from,
    core::BackpackInventory&   to,
    int                        max,
    core::AccessContext const& fromCtx,
    core::AccessContext const& toCtx,
    ItemPredicate const&       accept
) {
    if (&from == &to) return 0;
    int moved = 0;
    for (int slot = 0; slot < from.size() && moved < max; ++slot) {
        auto data = from.at(slot);
        if (data.empty() || (accept && !accept(data))) continue;
        int want = std::min(max - moved, data.count);
        if (!from.canExtract(slot, want, fromCtx)) continue;
        auto r = to.insert(data.withCount(want), toCtx, true);
        int  n = r.inserted + r.voided;
        if (n <= 0) continue;
        auto taken = from.extract(slot, n, fromCtx);
        if (taken.empty()) continue;
        auto real = to.insert(taken, toCtx);
        restore(from, slot, real.remainder); // never lose items
        moved += real.inserted + real.voided;
    }
    return moved;
}

Container* containerAt(BlockSource& region, BlockPos const& pos) { return region.tryGetContainer(pos).as_ptr(); }

} // namespace abp::placed
