#pragma once
// AlleviatedBackpacks public API: capability lookup (energy / fluids) for items, blocks and
// backpacks, plus the fluid registry. For LeviLamina mods; see docs/API.md.
//
// Discovery: AlleviatedBackpacks registers a `CapabilityService` with LeviLamina's
// `ll::service::ServiceManager` when it is enabled. Nothing has to be linked:
//
//     #include "abp/Capabilities.h"
//     if (auto caps = abp::api::capabilities()) caps->registerItemEnergy("mymod:battery", ...);
//
// (or `ServiceManager::getInstance().subscribeService<abp::api::CapabilityService>(fn)` to be told
// when it appears / goes away). Add AlleviatedBackpacks as a dependency in your manifest so it is
// enabled first.
//
// ABI: `CapabilityService` is a pure interface. New functions are only ever appended at the end
// and `apiVersion()` increases; existing ones never change. A breaking change would get a new
// service version (and id). All calls: server thread only.

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ll/api/service/Service.h"
#include "ll/api/service/ServiceManager.h"

#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"

#include "Energy.h"
#include "Fluid.h"
#include "Xp.h"

class BlockSource;

namespace abp::api {

/// The side of a block a capability is asked for (Bedrock FacingID order). `None`: no side
/// (internal / any).
enum class Side : int { None = -1, Down = 0, Up = 1, North = 2, South = 3, West = 4, East = 5 };

/// A fluid handler on an item. Filling or draining may change the item (a bucket becomes a water
/// bucket), so the handler works on its own copy: read the result with `container()` and put it
/// back where the item came from.
class IFluidHandlerItem : public IFluidHandler {
public:
    [[nodiscard]] virtual ItemStack const& container() const = 0;
};

/// What a filled swap container holds and what is left after emptying it.
struct ContainerContents {
    FluidStack fluid; ///< contents of one item
    ItemStack  empty; ///< the empty container (count 1)
};

/// An empty swap container filled with a fluid.
struct FilledContainer {
    ItemStack filled;     ///< the filled container (count 1)
    int       amount = 0; ///< mB it took
};

/// Provider signatures. Providers run on the server thread and must not keep the references they
/// get beyond the returned handler's immediate use.
/// - Item energy: the storage reads / writes `stack` in place (e.g. its user data), so `stack`
///   must outlive every use of the returned storage.
using ItemEnergyProvider  = std::function<std::shared_ptr<IEnergyStorage>(ItemStack& stack)>;
/// - Item fluids: the handler works on a copy of `stack` (see `IFluidHandlerItem`).
using ItemFluidProvider   = std::function<std::shared_ptr<IFluidHandlerItem>(ItemStack const& stack)>;
/// - Blocks: the block at `pos` has the id the provider was registered for.
using BlockEnergyProvider = std::function<std::shared_ptr<IEnergyStorage>(BlockSource& region, BlockPos const& pos, Side side)>;
using BlockFluidProvider  = std::function<std::shared_ptr<IFluidHandler>(BlockSource& region, BlockPos const& pos, Side side)>;

/// Registration handles: `register*` returns a non-zero token (0 = refused, e.g. that key is
/// already taken); pass it to `unregister` when your mod is disabled.
using Token = std::uint64_t;

class CapabilityService : public ll::service::Service {
public:
    static constexpr ll::service::ServiceIdView ServiceId{"AlleviatedBackpacks.CapabilityService", 1};
    [[nodiscard]] ll::service::ServiceId getServiceId() const noexcept override { return ServiceId; }

    /// Increases whenever functions are appended (1 = this header).
    [[nodiscard]] virtual int apiVersion() const noexcept = 0;
    /// Removes any registration made by a `register*` call.
    virtual bool unregister(Token token) = 0;

    // ---- energy -------------------------------------------------------------------------------
    /// Makes items with id `itemId` energy items. One provider per item id.
    virtual Token registerItemEnergy(std::string_view itemId, ItemEnergyProvider provider) = 0;
    /// Gives blocks with id `blockId` an energy storage. One provider per block id.
    virtual Token registerBlockEnergy(std::string_view blockId, BlockEnergyProvider provider) = 0;
    /// The energy storage of an item (nullptr if none). Backpacks expose their Battery upgrade.
    [[nodiscard]] virtual std::shared_ptr<IEnergyStorage> itemEnergy(ItemStack& stack) = 0;
    /// The energy storage of the block at `pos` (nullptr if none). Placed backpacks expose their
    /// Battery upgrade.
    [[nodiscard]] virtual std::shared_ptr<IEnergyStorage> blockEnergy(BlockSource& region, BlockPos const& pos, Side side = Side::None) = 0;
    /// The energy storage of the backpack with this uuid (its Battery upgrade; nullptr if none).
    [[nodiscard]] virtual std::shared_ptr<IEnergyStorage> backpackEnergy(std::string_view uuid) = 0;
    /// A ready-made item energy storage keeping its value as an int in `stack`'s user data under
    /// `key` (for `registerItemEnergy` providers).
    [[nodiscard]] virtual std::shared_ptr<IEnergyStorage>
    makeItemEnergyStorage(ItemStack& stack, int capacity, int maxReceive, int maxExtract, std::string_view key = "abp_energy") = 0;

    // ---- fluid registry -----------------------------------------------------------------------
    /// Registers a fluid ("namespace:path", unique).
    virtual Token registerFluid(FluidInfo info) = 0;
    [[nodiscard]] virtual std::optional<FluidInfo> fluid(std::string_view id) const = 0;
    [[nodiscard]] virtual std::vector<FluidInfo>   fluids() const = 0;
    [[nodiscard]] virtual bool                     fluidHasTag(std::string_view fluidId, std::string_view tag) const = 0;
    /// Registers a swap container (filled item + aux unique). Vanilla: buckets (1000 mB), water /
    /// potion / honey bottles (250 mB), bottle o' enchanting (160 mB experience).
    virtual Token registerFluidContainer(FluidContainer container) = 0;
    [[nodiscard]] virtual std::optional<ContainerContents> containerContents(ItemStack const& filled) const = 0;
    [[nodiscard]] virtual std::optional<FilledContainer>
    fillContainer(ItemStack const& empty, FluidStack const& fluid) const = 0;

    // ---- fluid capabilities -------------------------------------------------------------------
    /// Gives items with id `itemId` a fluid handler (beyond swap containers). One per item id.
    virtual Token registerItemFluid(std::string_view itemId, ItemFluidProvider provider) = 0;
    /// Gives blocks with id `blockId` a fluid handler. One per block id. Built in: cauldrons
    /// (one 1000 mB tank: water / lava / powder snow, whole buckets only, like NeoForge).
    virtual Token registerBlockFluid(std::string_view blockId, BlockFluidProvider provider) = 0;
    /// The fluid handler of an item: a registered provider, else a swap-container handler for
    /// registered containers (filled or empty). nullptr if none.
    [[nodiscard]] virtual std::shared_ptr<IFluidHandlerItem> itemFluid(ItemStack const& stack) = 0;
    [[nodiscard]] virtual std::shared_ptr<IFluidHandler> blockFluid(BlockSource& region, BlockPos const& pos, Side side = Side::None) = 0;
    /// The backpack's tanks (Tank upgrades, combined in upgrade slot order; nullptr if none).
    [[nodiscard]] virtual std::shared_ptr<IFluidHandler> backpackFluid(std::string_view uuid) = 0;

    // ---- world helpers (pumps) ----------------------------------------------------------------
    /// Takes the 1000 mB fluid source at `pos` (water / lava source, powder snow: any registered
    /// fluid's `sourceBlock`) and replaces it with air. Empty if there is none.
    virtual FluidStack pickupFluidSource(BlockSource& region, BlockPos const& pos, bool simulate) = 0;
    /// Places a source block of `fluid` (needs >= 1000 mB and a `sourceBlock`) at `pos`, which
    /// must be air or a flowing (non-source) liquid. Water is refused (0) in ultra-warm
    /// dimensions. Returns the mB used (1000) or 0. No permission checks: callers do those.
    virtual int placeFluidSource(BlockSource& region, BlockPos const& pos, FluidStack const& fluid, bool simulate) = 0;

    // New functions go here (append only).
};

/// The service, or nullptr while AlleviatedBackpacks is not enabled.
[[nodiscard]] inline std::shared_ptr<CapabilityService> capabilities() {
    auto res = ll::service::ServiceManager::getInstance().getService<CapabilityService>();
    return res ? *res : nullptr;
}

/// Fills `handler` with up to `amount` mB of a fluid tagged `tag`: the tagged fluid already in a
/// tank if any (first in tank order), else `fallback` (SC's tag fill, e.g. experience).
inline int fillTagged(
    CapabilityService const& caps,
    IFluidHandler&           handler,
    std::string_view         tag,
    int                      amount,
    FluidStack const&        fallback,
    bool                     simulate
) {
    for (int t = 0; t < handler.tanks(); ++t) {
        auto in = handler.fluidInTank(t);
        if (!in.empty() && caps.fluidHasTag(in.id, tag)) return handler.fill(in.copyWithAmount(amount), simulate);
    }
    return handler.fill(fallback.copyWithAmount(amount), simulate);
}

/// Drains up to `maxDrain` mB of the first fluid tagged `tag` found in `handler`'s tanks.
inline FluidStack
drainTagged(CapabilityService const& caps, IFluidHandler& handler, std::string_view tag, int maxDrain, bool simulate) {
    for (int t = 0; t < handler.tanks(); ++t) {
        auto in = handler.fluidInTank(t);
        if (!in.empty() && caps.fluidHasTag(in.id, tag)) return handler.drain(in.copyWithAmount(maxDrain), simulate);
    }
    return {};
}

} // namespace abp::api
