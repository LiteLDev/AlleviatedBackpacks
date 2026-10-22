#include "upgrade/deposit/ContainerTransfer.h"

#include <algorithm>
#include <format>

#include "mc/network/packet/TextPacket.h"
#include "mc/network/packet/TextPacketType.h"
#include "mc/world/Container.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"

#include "game/ItemConv.h"
#include "mod/I18n.h"
#include "ui/menu/BackpackMenu.h"
#include "ui/menu/FilterMenu.h"
#include "ui/menu/MenuManager.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/filter/FilterCommon.h"

namespace abp::upgrade::transfer {

namespace {

UpgradeTier tierFor(bool advanced) { return advanced ? UpgradeTier::Advanced : UpgradeTier::Basic; }

/// The upgrade slot of `ctx` still holds an upgrade item `itemId` (another player may have removed
/// it while a settings menu was open).
bool holds(UpgradeContext const& ctx, std::string const& itemId) { return filtering::stillInstalled(ctx, itemId); }

void addDistinct(std::vector<core::ItemStackData>& out, core::ItemStackData const& item) {
    if (item.empty()) return;
    for (auto const& o : out) {
        if (o.sameItem(item)) return;
    }
    out.push_back(item.withCount(1));
}

/// FilterMenu plus the third type state (INVENTORY / STORAGE) in a free control slot.
class TransferFilterMenu final : public ui::FilterMenu {
public:
    TransferFilterMenu(std::string title, Getter get, Setter set, Options options, UpgradeContext ctx, bool deposit)
    : FilterMenu(std::move(title), get, set, std::move(options)),
      mGetFilter(std::move(get)),
      mSetFilter(std::move(set)),
      mCtx(std::move(ctx)),
      mItemId(mCtx.state().item.id),
      mDeposit(deposit) {}

    void build(Player& player) override {
        FilterMenu::build(player);
        if (!holds(mCtx, mItemId)) return;
        auto f  = mGetFilter();
        bool on = mDeposit ? f.filterByInventory : f.filterByStorage;
        auto label = mDeposit ? trFor(player, "abp.deposit.contents", "Match Inventory Contents: {}", onOff(player, on))
                              : trFor(player, "abp.restock.contents", "Match Backpack Contents: {}", onOff(player, on));
        setSlot(
            size() - 9 + 5,
            std::make_shared<ui::ButtonSlot>(
                ui::icon(on ? "minecraft:chest" : "minecraft:barrel", label, {trFor(player, "abp.filter.click_to_toggle", "§7Click to toggle")}),
                [this](ui::ClickContext const& c) {
                    if (!holds(mCtx, mItemId)) {
                        ui::MenuManager::get().close(c.player);
                        return;
                    }
                    auto f = mGetFilter();
                    if (mDeposit) f.filterByInventory = !f.filterByInventory;
                    else f.filterByStorage = !f.filterByStorage;
                    mSetFilter(f);
                    invalidate();
                }
            )
        );
    }

private:
    static std::string onOff(Player& player, bool v) {
        return v ? trFor(player, "abp.filter.on", "§aOn") : trFor(player, "abp.filter.off", "§cOff");
    }

    Getter         mGetFilter;
    Setter         mSetFilter;
    UpgradeContext mCtx;
    std::string    mItemId;
    bool           mDeposit;
};

} // namespace

core::Filter loadFilter(UpgradeContext const& ctx, bool advanced) { return filtering::load(ctx, tierFor(advanced)); }

bool contentsMode(UpgradeContext const& ctx, bool advanced) {
    auto f = loadFilter(ctx, advanced);
    return f.filterByInventory || f.filterByStorage;
}

std::vector<core::ItemStackData> distinctItems(Container& container) {
    std::vector<core::ItemStackData> out;
    int                              n = container.getContainerSize();
    for (int i = 0; i < n; ++i) {
        auto const& stack = container.getItem(i);
        if (stack.isNull()) continue;
        addDistinct(out, game::ItemConv::toData(stack));
    }
    return out;
}

std::vector<core::ItemStackData> distinctItems(core::BackpackInventory const& inventory) {
    std::vector<core::ItemStackData> out;
    for (int i = 0; i < inventory.size(); ++i) addDistinct(out, inventory.at(i));
    return out;
}

int insertIntoContainer(Container& container, int face, core::ItemStackData const& item, int amount, bool simulate) {
    if (item.empty() || amount <= 0) return 0;
    ItemStack proto = game::ItemConv::toStack(item, 1);
    if (proto.isNull()) return 0;
    int limit = std::min({container.getMaxStackSize(), static_cast<int>(proto.getMaxStackSize()), 255});
    if (limit <= 0) return 0;
    int size = container.getContainerSize();
    int left = amount;
    // Merge into equal stacks first.
    for (int s = 0; s < size && left > 0; ++s) {
        auto const& cur = container.getItem(s);
        if (cur.isNull() || cur.mCount >= limit) continue;
        if (!cur.isStackable(proto)) continue; // the game's own merge rule
        if (container.isSlotDisabled(s) || !container.canPushInItem(s, face, proto)) continue;
        int n = std::min(limit - static_cast<int>(cur.mCount), left);
        if (!simulate) container.setItem(s, game::ItemConv::toStack(item, cur.mCount + n));
        left -= n;
    }
    // Then empty slots.
    for (int s = 0; s < size && left > 0; ++s) {
        if (!container.getItem(s).isNull()) continue;
        if (container.isSlotDisabled(s) || !container.canPushInItem(s, face, proto)) continue;
        int n = std::min(limit, left);
        if (!simulate) container.setItem(s, game::ItemConv::toStack(item, n));
        left -= n;
    }
    return amount - left;
}

std::shared_ptr<ui::Menu> settingsMenu(Player& player, UpgradeContext const& ctx, bool advanced, bool deposit) {
    auto* type  = upgradeFor(ctx.state().item);
    auto  title = type ? displayName(*type, &player) : trFor(player, "abp.filter.title", "Filter");
    auto  tier  = tierFor(advanced);
    int   slots = filtering::specFor(deposit ? "deposit" : "restock", tier).slots;

    ui::FilterMenu::Options options;
    options.slots           = slots;
    options.allowMatchBy    = advanced;
    options.allowNbt        = advanced;
    options.allowDurability = advanced;
    options.onBack          = [bp = ctx.backpackPtr()](Player& p) {
        ui::BackpackMenu::openBackpack(p, bp, {}, ui::BackpackMenu::View::Upgrades);
    };
    auto base   = ctx.withPlayer(nullptr);
    auto itemId = ctx.state().item.id;
    auto get    = [base, tier, slots, itemId] {
        if (!holds(base, itemId)) return core::Filter(slots, core::FilterDefaults{}); // removed while the menu was open
        return filtering::load(base, tier);
    };
    auto set = [base, tier, itemId](core::Filter const& f) {
        if (holds(base, itemId)) filtering::store(base, tier, f);
    };
    return std::make_shared<TransferFilterMenu>(std::move(title), std::move(get), std::move(set), std::move(options), base, deposit);
}

std::vector<std::string> describe(Player& player, UpgradeContext const& ctx, bool advanced, bool deposit) {
    auto f = loadFilter(ctx, advanced);
    if (deposit ? f.filterByInventory : f.filterByStorage) {
        return {deposit ? trFor(player, "abp.deposit.describe.contents", "§7Match Inventory Contents")
                        : trFor(player, "abp.restock.describe.contents", "§7Match Backpack Contents")};
    }
    auto copy            = f;
    copy.filterByStorage = false;
    return filtering::describe(player, copy, tierFor(advanced));
}

void sendTip(Player& player, std::string const& text) {
    auto                           pkt = TextPacket{};
    TextPacketPayload::MessageOnly message;
    message.mType = TextPacketType::Tip;
    message.mMessage->assign(text);
    pkt.mBody = message;
    pkt.sendTo(player);
}

Interaction interact(std::shared_ptr<game::Backpack> const& backpack, Player& player, Container& container, int face) {
    Interaction out;
    if (!backpack) return out;
    forEachInstalled(backpack, true, [&](IUpgrade& type, UpgradeContext const& base) {
        auto const& info     = type.info();
        bool        advanced = info.tier == UpgradeTier::Advanced;
        auto        ctx      = base.withPlayer(&player);
        if (info.groupOrId() == "deposit") {
            out.handled = true;
            auto r      = deposit::run(ctx, container, face, advanced);
            out.deposited.stacks += r.stacks;
            out.deposited.items  += r.items;
            // SB DepositUpgradeWrapper.onHandlerInteract
            out.message = r.stacks > 0
                            ? trFor(player, "abp.deposit.status.stacks_deposited", "{} stacks deposited to inventory", r.stacks)
                            : trFor(player, "abp.deposit.status.nothing_to_deposit", "No stacks could be deposited to inventory");
        } else if (info.groupOrId() == "restock") {
            out.handled = true;
            auto r      = restock::run(ctx, container, face, advanced);
            out.restocked.stacks += r.stacks;
            out.restocked.items  += r.items;
            // SB RestockUpgradeWrapper.onHandlerInteract
            out.message = r.stacks > 0
                            ? trFor(player, "abp.restock.status.stacks_restocked", "{} stacks restocked from inventory", r.stacks)
                            : trFor(player, "abp.restock.status.nothing_to_restock", "No stacks could be restocked from inventory");
        }
    });
    return out;
}

} // namespace abp::upgrade::transfer
