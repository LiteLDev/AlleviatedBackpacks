#include "upgrade/deposit/ContainerTransfer.h"

#include <algorithm>
#include <format>

#include "mc/network/packet/TextPacket.h"
#include "mc/network/packet/TextPacketType.h"
#include "mc/world/Container.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

#include "game/ItemConv.h"
#include "mod/I18n.h"
#include "ui/menu/FilterMenu.h"
#include "ui/menu/MenuManager.h"
#include "ui/menu/BackpackMenu.h"
#include "upgrade/UpgradeManager.h"

namespace abp::upgrade::transfer {

namespace {

/// The upgrade slot of `ctx` still holds an upgrade item `itemId` (another player may have removed
/// it while a settings menu was open).
bool holds(UpgradeContext const& ctx, std::string const& itemId) {
    auto const& slots = ctx.backpack().data().upgrades;
    if (ctx.slot() < 0 || ctx.slot() >= static_cast<int>(slots.size())) return false;
    auto const& item = slots[static_cast<std::size_t>(ctx.slot())].item;
    return !item.empty() && item.id == itemId;
}

void addDistinct(std::vector<core::ItemStackData>& out, core::ItemStackData const& item) {
    if (item.empty()) return;
    for (auto const& o : out) {
        if (o.sameItem(item)) return;
    }
    out.push_back(item.withCount(1));
}

/// FilterMenu plus the advanced "contents" mode toggle in the free control slot.
class TransferFilterMenu final : public ui::FilterMenu {
public:
    TransferFilterMenu(std::string title, Getter get, Setter set, Options options, UpgradeContext ctx, bool deposit)
    : FilterMenu(std::move(title), std::move(get), std::move(set), std::move(options)),
      mCtx(std::move(ctx)),
      mItemId(mCtx.state().item.id),
      mDeposit(deposit) {}

    void build(Player& player) override {
        FilterMenu::build(player);
        if (!stillInstalled()) return;
        bool on = mCtx.settings().value(std::string(kContentsKey), false);
        auto label =
            mDeposit ? trFor(player, "abp.deposit.contents", "Only items already in the container: {}", onOff(player, on))
                     : trFor(player, "abp.restock.contents", "Only items already in the backpack: {}", onOff(player, on));
        auto help = mDeposit ? trFor(player, "abp.deposit.contents.help", "§7When on, the filter list is ignored")
                             : trFor(player, "abp.restock.contents.help", "§7When on, the filter list is ignored");
        setSlot(
            size() - 9 + 5,
            std::make_shared<ui::ButtonSlot>(
                ui::icon(on ? "minecraft:chest" : "minecraft:barrel", label, {help, trFor(player, "abp.filter.click_to_toggle", "§7Click to toggle")}),
                [this](ui::ClickContext const& c) {
                    if (!stillInstalled()) {
                        ui::MenuManager::get().close(c.player);
                        return;
                    }
                    auto& s                      = mCtx.settings();
                    s[std::string(kContentsKey)] = !s.value(std::string(kContentsKey), false);
                    mCtx.saveSettings(false);
                    invalidate();
                }
            )
        );
    }

private:
    /// The upgrade this menu edits is still in its slot (another player may have removed it).
    [[nodiscard]] bool stillInstalled() const { return holds(mCtx, mItemId); }

    static std::string onOff(Player& player, bool v) {
        return v ? trFor(player, "abp.filter.on", "§aOn") : trFor(player, "abp.filter.off", "§cOff");
    }

    UpgradeContext mCtx;
    std::string    mItemId;
    bool           mDeposit;
};

} // namespace

bool contentsMode(UpgradeContext const& ctx, bool advanced) {
    return advanced && ctx.settings().value(std::string(kContentsKey), false);
}

core::Filter effectiveFilter(UpgradeContext const& ctx, bool advanced, std::vector<core::ItemStackData> const& reference) {
    int  slots = filterSlots(advanced);
    auto f     = core::Filter::fromJson(ctx.settings().value(std::string(kFilterKey), nlohmann::json::object()), slots);
    if (!advanced) {
        // Basic upgrades only have an allow/deny list matched by item.
        f.matchBy         = core::MatchBy::Item;
        f.matchNbt        = false;
        f.matchDurability = false;
        f.tags.clear();
        return f;
    }
    if (contentsMode(ctx, advanced)) {
        // Exactly the items present on the other side (NBT / durability options still apply).
        f.matchBy        = core::MatchBy::Item;
        f.mode           = core::FilterMode::Allow;
        f.emptyAllowsAll = false;
        f.tags.clear();
        f.ghosts = reference;
    }
    return f;
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
    int   slots = filterSlots(advanced);

    ui::FilterMenu::Options options;
    options.slots           = slots;
    options.allowMatchBy    = advanced;
    options.allowNbt        = advanced;
    options.allowDurability = advanced;
    options.onBack          = [bp = ctx.backpackPtr()](Player& p) {
        ui::BackpackMenu::openBackpack(p, bp, {}, ui::BackpackMenu::View::Upgrades);
    };
    auto key    = std::string(kFilterKey);
    auto itemId = ctx.state().item.id;
    auto get    = [ctx, key, slots, itemId] {
        if (!holds(ctx, itemId)) return core::Filter(slots); // removed while the menu was open
        return core::Filter::fromJson(ctx.settings().value(key, nlohmann::json::object()), slots);
    };
    auto set = [ctx, key, itemId](core::Filter const& f) {
        if (!holds(ctx, itemId)) return;
        ctx.settings()[key] = f.toJson();
        ctx.saveSettings(false); // the filter is read on use; no inventory hooks depend on it
    };
    if (!advanced) return std::make_shared<ui::FilterMenu>(std::move(title), std::move(get), std::move(set), std::move(options));
    return std::make_shared<TransferFilterMenu>(std::move(title), std::move(get), std::move(set), std::move(options), ctx, deposit);
}

std::vector<std::string> describe(Player& player, UpgradeContext const& ctx, bool advanced, bool deposit) {
    std::vector<std::string> lines;
    if (contentsMode(ctx, advanced)) {
        lines.push_back(
            deposit ? trFor(player, "abp.deposit.describe.contents", "§7Only items already in the container")
                    : trFor(player, "abp.restock.describe.contents", "§7Only items already in the backpack")
        );
        return lines;
    }
    auto f     = effectiveFilter(ctx, advanced, {});
    int  count = static_cast<int>(std::count_if(f.ghosts.begin(), f.ghosts.end(), [](auto const& g) { return !g.empty(); }));
    std::string_view unit = deposit ? "deposit" : "restock";
    if (f.isEmpty() && f.mode == core::FilterMode::Allow) {
        lines.push_back(trFor(player, std::format("abp.{}.describe.filter_all", unit), "§7Filter: everything"));
    } else if (f.mode == core::FilterMode::Allow) {
        lines.push_back(trFor(player, std::format("abp.{}.describe.filter_allow", unit), "§7Allow list: {} items", count));
    } else {
        lines.push_back(trFor(player, std::format("abp.{}.describe.filter_deny", unit), "§7Deny list: {} items", count));
    }
    return lines;
}

void sendTip(Player& player, std::string const& text) {
    auto        pkt = TextPacket{};
    TextPacketPayload::MessageOnly message;
    message.mType = TextPacketType::Tip;
    message.mMessage->assign(text);
    pkt.mBody = message;
    pkt.sendTo(player);
}

Interaction interact(std::shared_ptr<game::Backpack> const& backpack, Player& player, Container& container, int face) {
    Interaction out;
    if (!backpack) return out;
    std::vector<std::string> parts;
    forEachInstalled(backpack, true, [&](IUpgrade& type, UpgradeContext const& base) {
        auto const& info     = type.info();
        bool        advanced = info.tier == UpgradeTier::Advanced;
        auto        ctx      = base.withPlayer(&player);
        if (info.groupOrId() == "deposit") {
            out.handled = true;
            auto r      = deposit::run(ctx, container, face, advanced);
            out.deposited.stacks += r.stacks;
            out.deposited.items  += r.items;
            parts.push_back(
                r.stacks > 0 ? trFor(player, "abp.deposit.done", "Deposited {} stacks", r.stacks)
                             : trFor(player, "abp.deposit.nothing", "Nothing to deposit")
            );
        } else if (info.groupOrId() == "restock") {
            out.handled = true;
            auto r      = restock::run(ctx, container, face, advanced);
            out.restocked.stacks += r.stacks;
            out.restocked.items  += r.items;
            parts.push_back(
                r.stacks > 0 ? trFor(player, "abp.restock.done", "Restocked {} stacks", r.stacks)
                             : trFor(player, "abp.restock.nothing", "Nothing to restock")
            );
        }
    });
    for (auto const& p : parts) {
        if (!out.message.empty()) out.message += "§r, ";
        out.message += p;
    }
    return out;
}

} // namespace abp::upgrade::transfer
