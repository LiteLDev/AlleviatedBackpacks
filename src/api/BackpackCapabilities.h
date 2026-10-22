#pragma once
// Internal integration points between upgrades and the capability API (not part of the public
// headers in src/api/abp/). Tank-like upgrades contribute fluid tanks, battery-like upgrades an
// energy storage; the API combines them into the backpack's handlers, which other mods see through
// `CapabilityService::backpackFluid/backpackEnergy`, backpack items (`itemFluid/itemEnergy`) and
// placed backpacks (`blockFluid/blockEnergy`).

#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "api/abp/Energy.h"
#include "api/abp/Fluid.h"
#include "mod/Registry.h"
#include "upgrade/IUpgrade.h"

namespace abp::game {
class Backpack;
}

namespace abp::api::backpack {

/// Makes the handler of one installed upgrade (slot `ctx.slot()`). Return nullptr to contribute
/// nothing. Handlers are short-lived views: read the upgrade's state on every call (don't cache).
using TankFactory   = std::function<std::shared_ptr<IFluidHandler>(upgrade::UpgradeContext const& ctx)>;
using EnergyFactory = std::function<std::shared_ptr<IEnergyStorage>(upgrade::UpgradeContext const& ctx)>;

/// Upgrades with id `upgradeId` (`UpgradeInfo::id`) contribute fluid tanks. Static init only
/// (use `ABP_REGISTER_BACKPACK_TANK`).
void registerTankUpgrade(std::string upgradeId, TankFactory factory);
/// Upgrades with id `upgradeId` contribute the backpack's energy storage (SB: the first one in
/// slot order wins). Static init only (use `ABP_REGISTER_BACKPACK_BATTERY`).
void registerEnergyUpgrade(std::string upgradeId, EnergyFactory factory);

/// Wrappers applied (in registration order) to the combined handlers, e.g. Inception adding the
/// tanks of nested backpacks or a Void upgrade swallowing fluids. `inner` may be nullptr (no
/// tanks / battery); return nullptr for "none". Static init only.
using FluidWrapper  = std::function<std::shared_ptr<IFluidHandler>(std::shared_ptr<game::Backpack> const&, std::shared_ptr<IFluidHandler> inner)>;
using EnergyWrapper = std::function<std::shared_ptr<IEnergyStorage>(std::shared_ptr<game::Backpack> const&, std::shared_ptr<IEnergyStorage> inner)>;
void addFluidWrapper(FluidWrapper wrapper);
void addEnergyWrapper(EnergyWrapper wrapper);

/// The backpack's combined fluid handler: every contributing upgrade's tanks in slot order
/// (`CombinedFluidHandler` rules), then the wrappers. nullptr without tanks.
[[nodiscard]] std::shared_ptr<IFluidHandler> fluidHandler(std::shared_ptr<game::Backpack> const& backpack);
/// The backpack's energy storage: the first contributing upgrade in slot order, then the
/// wrappers. nullptr without one.
[[nodiscard]] std::shared_ptr<IEnergyStorage> energyStorage(std::shared_ptr<game::Backpack> const& backpack);

/// The handler one installed upgrade contributes (its registered tank / energy factory applied to
/// `ctx`), or nullptr when its type registered none. Used e.g. by Stack upgrades to find out the
/// stack multiplier the upgrade's contents need (SC `IStackableContentsUpgrade`).
[[nodiscard]] std::shared_ptr<IFluidHandler>  tankOf(upgrade::UpgradeContext const& ctx);
[[nodiscard]] std::shared_ptr<IEnergyStorage> energyOf(upgrade::UpgradeContext const& ctx);

/// SB sizing inputs of a backpack: slot rows (`core::capacity::slotRows` of its storage size) and
/// stack multiplier (from its slot limit for 64-stack items; 1 without Stack upgrades).
[[nodiscard]] int    slotRows(game::Backpack& backpack);
[[nodiscard]] double stackMultiplier(game::Backpack& backpack);

namespace detail {
struct TankRegistrar {
    TankRegistrar(std::string id, TankFactory f) { registerTankUpgrade(std::move(id), std::move(f)); }
};
struct EnergyRegistrar {
    EnergyRegistrar(std::string id, EnergyFactory f) { registerEnergyUpgrade(std::move(id), std::move(f)); }
};
} // namespace detail

} // namespace abp::api::backpack

/// `ABP_REGISTER_BACKPACK_TANK("tank", [](abp::upgrade::UpgradeContext const& ctx) { ... });`
#define ABP_REGISTER_BACKPACK_TANK(id, ...)                                                                            \
    static ::abp::api::backpack::detail::TankRegistrar ABP_UNIQUE(abp_backpack_tank_)(id, __VA_ARGS__)
/// `ABP_REGISTER_BACKPACK_BATTERY("battery", [](abp::upgrade::UpgradeContext const& ctx) { ... });`
#define ABP_REGISTER_BACKPACK_BATTERY(id, ...)                                                                         \
    static ::abp::api::backpack::detail::EnergyRegistrar ABP_UNIQUE(abp_backpack_battery_)(id, __VA_ARGS__)
