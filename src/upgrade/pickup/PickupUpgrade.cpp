// Pickup upgrade (basic / advanced): items a carrier picks up from the ground go into the backpack
// first when they pass the upgrade's filter. Whatever doesn't fit (or doesn't pass) goes to the
// player inventory as usual. The foundation dispatches pickups to every carried backpack in order
// (`upgrade::dispatchPickup`, fed by `PlayerPickUpItemEvent`).
//
// Settings: {"filter": core::Filter}. An empty allow list picks up everything.

#include "upgrade/pickup/PickupUpgrade.h"

#include "mc/world/actor/player/Player.h"

#include "game/ItemConv.h"
#include "upgrade/UpgradeManager.h"
#include "upgrade/filter/FilterCommon.h"

namespace abp::upgrade::pickup {

namespace {

class PickupUpgradeBase : public IUpgrade {
public:
    explicit PickupUpgradeBase(UpgradeInfo info) : mInfo(std::move(info)) {}

    [[nodiscard]] UpgradeInfo const& info() const override { return mInfo; }

    /// Parsed filter, refreshed on every hook rebuild (settings saves rebuild the hooks).
    struct CachedFilter {
        bool         valid = false;
        core::Filter filter;
    };

    void attach(UpgradeContext const& ctx, core::BackpackInventory& /*inventory*/) override {
        auto& cache  = ctx.transient<CachedFilter>();
        cache.filter = filtering::load(ctx, mInfo.tier);
        cache.valid  = true;
    }

    void onPickup(UpgradeContext const& ctx, core::ItemStackData& item) override {
        if (item.empty()) return;
        auto& cache = ctx.transient<CachedFilter>();
        if (!cache.valid) {
            cache.filter = filtering::load(ctx, mInfo.tier);
            cache.valid  = true;
        }
        if (!cache.filter.passes(item, &game::ItemConv::tags())) return;
        auto r = ctx.backpack().inventory().insert(item, core::AccessContext{core::Source::Pickup, ctx.player()});
        item.count -= r.inserted + r.voided;
    }

    [[nodiscard]] std::shared_ptr<ui::Menu> createSettingsMenu(Player& player, UpgradeContext const& ctx) override {
        return std::make_shared<filtering::UpgradeFilterMenu>(player, ctx, mInfo.tier);
    }

    [[nodiscard]] std::vector<std::string> describe(Player& player, UpgradeContext const& ctx) const override {
        return filtering::describe(player, filtering::load(ctx, mInfo.tier), mInfo.tier);
    }

private:
    UpgradeInfo mInfo;
};

class PickupUpgrade final : public PickupUpgradeBase {
public:
    PickupUpgrade()
    : PickupUpgradeBase(UpgradeInfo{
          .id             = std::string(kBasicId),
          .itemId         = "alleviatedbackpacks:pickup_upgrade",
          .nameKey        = "abp.upgrade.pickup.name",
          .nameFallback   = "Pickup Upgrade",
          .tier           = UpgradeTier::Basic,
          .group          = "pickup",
          .maxPerBackpack = kMaxPerBackpack,
          .hasSettings    = true,
          .toggleable     = true,
      }) {}
};

class AdvancedPickupUpgrade final : public PickupUpgradeBase {
public:
    AdvancedPickupUpgrade()
    : PickupUpgradeBase(UpgradeInfo{
          .id             = std::string(kAdvancedId),
          .itemId         = "alleviatedbackpacks:advanced_pickup_upgrade",
          .nameKey        = "abp.upgrade.advanced_pickup.name",
          .nameFallback   = "Advanced Pickup Upgrade",
          .tier           = UpgradeTier::Advanced,
          .group          = "pickup",
          .maxPerBackpack = kMaxPerBackpack,
          .hasSettings    = true,
          .toggleable     = true,
      }) {}
};

} // namespace
} // namespace abp::upgrade::pickup

ABP_REGISTER_UPGRADE(abp::upgrade::pickup::PickupUpgrade);
ABP_REGISTER_UPGRADE(abp::upgrade::pickup::AdvancedPickupUpgrade);
