// Infinity Upgrade (Admin) / Survival Infinity Upgrade: see Infinity.h.

#include "upgrade/infinity/Infinity.h"

#include <algorithm>
#include <climits>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerDestroyBlockEvent.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"

#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"

#include "feature/placed/PlacedBackpacks.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "net/NativeScreens.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/deposit/ContainerTransfer.h"

namespace abp::upgrade::infinity {

bool hasPermission(Player const& player, int level) {
    if (level <= 0) return true;
    // Java permission level 2 (cheats / commands) maps to Bedrock operators.
    return player.getCommandPermissionLevel() >= CommandPermissionLevel::GameDirectors;
}

namespace {
std::string message(Player const* player, std::string_view key, std::string_view fallback) {
    return player ? trFor(*player, key, fallback) : tr(key, fallback);
}

InfinityUpgrade const* asInfinity(IUpgrade const* type) {
    if (!type) return nullptr;
    auto const& id = type->info().id;
    return id == kAdminId || id == kSurvivalId ? static_cast<InfinityUpgrade const*>(type) : nullptr;
}
} // namespace

UpgradeInfo const& InfinityUpgrade::info() const {
    static UpgradeInfo const admin{
        .id           = std::string(kAdminId),
        .itemId       = "alleviatedbackpacks:infinity_upgrade",
        .nameKey      = "abp.upgrade.infinity.name",
        .nameFallback = "Infinity Upgrade (Admin)",
        .hasSettings  = false,
        .toggleable   = false,
    };
    static UpgradeInfo const survival{
        .id           = std::string(kSurvivalId),
        .itemId       = "alleviatedbackpacks:survival_infinity_upgrade",
        .nameKey      = "abp.upgrade.survival_infinity.name",
        .nameFallback = "Survival Infinity Upgrade",
        .hasSettings  = false,
        .toggleable   = false,
    };
    return mAdmin ? admin : survival;
}

bool InfinityUpgrade::canInstall(UpgradeContext const& ctx, std::string& reason) const {
    auto const& slots = ctx.backpack().data().upgrades;
    for (std::size_t i = 0; i < slots.size(); ++i) {
        if (static_cast<int>(i) == ctx.slot() || slots[i].empty()) continue;
        reason = message(ctx.player(), "abp.infinity.error.any_upgrade_exists", "All upgrade slots need to be empty");
        return false;
    }
    return true;
}

bool InfinityUpgrade::allowsInstallOf(UpgradeContext const& self, IUpgrade const&, std::string& reason) const {
    reason = message(self.player(), "abp.infinity.error.no_upgrade_can_be_added", "No upgrade can be added");
    return false;
}

bool InfinityUpgrade::canRemove(UpgradeContext const& ctx, std::string& reason) const {
    if (!ctx.player() || hasPermission(*ctx.player(), permissionLevel())) return true;
    reason = message(ctx.player(), "abp.infinity.error.only_admin_remove", "Only admins can remove Infinity Upgrade");
    return false;
}

void InfinityUpgrade::attach(UpgradeContext const& ctx, core::BackpackInventory& inventory) {
    auto const owner = ctx.hookOwner();
    auto*      inv   = &inventory; // the hooks belong to this inventory
    int        level = permissionLevel();
    // SC InfinityInventoryPart: non-empty slots are infinite, every slot holds Integer.MAX_VALUE.
    inventory.addInfiniteSlots(owner, [inv](int slot) { return !inv->at(slot).empty(); });
    inventory.addLimitModifier(owner, 1'000'000, [](int, core::ItemStackData const&, int) { return INT_MAX; });
    // Only empty slots take items, and only from a player with the permission level (the mod
    // itself may still restore contents, e.g. on a backpack tier change).
    inventory.addInsertFilter(owner, -1'000'000, [level](core::InsertQuery const& q) {
        if (q.slot >= 0 && !q.inventory.at(q.slot).empty()) return core::InsertVerdict::Deny;
        if (q.ctx.source == core::Source::Internal) return core::InsertVerdict::Allow;
        if (q.ctx.source != core::Source::Player || !q.ctx.user) return core::InsertVerdict::Deny;
        return hasPermission(*static_cast<Player const*>(q.ctx.user), level) ? core::InsertVerdict::Allow
                                                                             : core::InsertVerdict::Deny;
    });
}

int requiredLevel(game::Backpack const& backpack) {
    int level = -1;
    for (auto const& s : backpack.data().upgrades) {
        if (auto const* type = asInfinity(upgradeFor(s.item))) level = std::max(level, type->permissionLevel());
    }
    return level;
}

bool mayTakePlaced(Player const& player, game::Backpack const& backpack) {
    int level = requiredLevel(backpack);
    return level < 0 || hasPermission(player, level);
}

namespace {

/// Placed backpacks with an admin Infinity upgrade: only admins break or pick them up.
class PlacedGuard {
public:
    void install() {
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerDestroyBlockEvent>(
            [](ll::event::PlayerDestroyBlockEvent& ev) {
                if (refused(ev.self(), ev.pos())) {
                    ev.cancel();
                    transfer::sendTip(
                        ev.self(),
                        trFor(ev.self(), "abp.infinity.status.only_admin_break", "§cOnly Admins can break backpack with Infinity Upgrade")
                    );
                }
            },
            ll::event::EventPriority::Highest
        ));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerInteractBlockEvent>(
            [](ll::event::PlayerInteractBlockEvent& ev) {
                auto& player = ev.self();
                if (!player.isSneaking() || !player.getSelectedItem().isNull()) return; // only the pick-up gesture
                if (refused(player, ev.blockPos())) {
                    ev.cancel();
                    transfer::sendTip(
                        player,
                        trFor(player, "abp.infinity.status.only_admin_pickup", "§cOnly Admins can pickup backpack with Infinity Upgrade")
                    );
                }
            },
            ll::event::EventPriority::Highest // before the placed-backpack unit's own handler
        ));
    }
    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
    }

    static bool refused(Player& player, BlockPos const& pos) {
        if (player.isClientSide()) return false; // the integrated server decides (client build)
        auto& region = player.getDimensionBlockSource();
        if (!placed::PlacedBackpacks::isBackpackBlock(region.getBlock(pos))) return false;
        auto bp = placed::PlacedBackpacks::get().backpackAt(region, pos);
        return bp && !mayTakePlaced(player, *bp);
    }

private:
    std::vector<ll::event::ListenerPtr> mListeners;
};

} // namespace

} // namespace abp::upgrade::infinity

ABP_REGISTER_UPGRADE(abp::upgrade::infinity::InfinityUpgrade);
ABP_REGISTER_UPGRADE(abp::upgrade::infinity::SurvivalInfinityUpgrade);
ABP_REGISTER_MODULE(abp::upgrade::infinity::PlacedGuard);

// The native screen shows "∞" instead of the count of every non-empty slot (client: an upgrade
// whose tab data has "infinite": true).
ABP_REGISTER_TAB_DATA(abp::net::TabDataProvider{
    .upgrade = std::string(abp::upgrade::infinity::kAdminId),
    .data    = [](Player&, abp::upgrade::UpgradeContext const&) { return nlohmann::json{{"infinite", true}}; },
});
ABP_REGISTER_TAB_DATA(abp::net::TabDataProvider{
    .upgrade = std::string(abp::upgrade::infinity::kSurvivalId),
    .data    = [](Player&, abp::upgrade::UpgradeContext const&) { return nlohmann::json{{"infinite", true}}; },
});
