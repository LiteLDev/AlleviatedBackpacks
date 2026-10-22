// The capability service: energy / fluid provider registries, the fluid registry, built-in
// providers (cauldrons, backpack items, placed backpacks) and the LeviLamina service registration.

#include "api/Service.h"

#include <map>
#include <unordered_map>

#include "ll/api/service/ServiceManager.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/CauldronLiquidType.h"
#include "mc/world/level/block/VanillaStates.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/BlockActorType.h"
#include "mc/world/level/block/actor/CauldronBlockActor.h"
#include "mc/world/level/block/states/BlockStateVariant.h"
#include "mc/world/level/dimension/Dimension.h"

#include "api/BackpackCapabilities.h"
#include "feature/admin/SbConfig.h"
#include "feature/placed/PlacedBackpacks.h"
#include "game/BackpackItem.h"
#include "game/ItemConv.h"
#include "game/Storage.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace abp::api {

namespace {

namespace ids = fluids;

// ---- blocks ---------------------------------------------------------------------------------------

Block const* blockByName(std::string_view name) {
    auto b = Block::tryGetFromRegistry(HashedString(name));
    return b ? b.as_ptr() : nullptr;
}

bool setBlock(BlockSource& region, BlockPos const& pos, Block const& block) {
    return region.setBlock(pos, block, 3, nullptr, BlockChangeContext{});
}

bool isCauldron(Block const& block) {
    auto const& n = block.getTypeName();
    return n == "minecraft:cauldron" || n == "minecraft:lava_cauldron";
}

/// Potion or dyed-water cauldrons keep their contents in the block actor: not a plain fluid.
bool cauldronHasExtras(BlockSource& region, BlockPos const& pos) {
    auto* be = region.getBlockEntity(pos);
    if (!be || be->getType() != BlockActorType::Cauldron) return false;
    auto const& cauldron = *static_cast<CauldronBlockActor const*>(be);
    return cauldron.mHasCustomColor || cauldron.mPotionId > 0;
}

/// Makes `pos` an empty cauldron (`fluid` empty) or a full one of water / lava / powder snow.
bool setCauldron(BlockSource& region, BlockPos const& pos, std::string_view fluid) {
    auto const* base = blockByName(fluid == ids::Lava ? "minecraft:lava_cauldron" : "minecraft:cauldron");
    if (!base && fluid == ids::Lava) base = blockByName("minecraft:cauldron");
    if (!base) return false;
    auto liquid = fluid == ids::Lava         ? CauldronLiquidType::Lava
                : fluid == ids::PowderSnow ? CauldronLiquidType::PowderSnow
                                           : CauldronLiquidType::Water;
    auto withLiquid = base->setState<int>(VanillaStates::CauldronLiquid(), static_cast<int>(liquid));
    if (!withLiquid) return false;
    auto full = withLiquid->setState<int>(VanillaStates::FillLevel(), fluid.empty() ? 0 : 6);
    if (!full) return false;
    return setBlock(region, pos, *full);
}

/// A cauldron as one 1000 mB tank of water / lava / powder snow. Like NeoForge's cauldron
/// handler only whole buckets move: an empty cauldron takes 1000 mB, a full one gives 1000 mB.
/// Partly filled cauldrons report their level (fill_level / 6 of a bucket) but can't be drained.
class CauldronHandler final : public IFluidHandler {
public:
    static constexpr int kMaxLevel = 6;

    CauldronHandler(BlockSource& region, BlockPos const& pos) : mRegion(region), mPos(pos) {}

    [[nodiscard]] int        tanks() const override { return 1; }
    [[nodiscard]] FluidStack fluidInTank(int tank) const override { return tank == 0 ? current() : FluidStack{}; }
    [[nodiscard]] int        tankCapacity(int tank) const override { return tank == 0 ? kBucketVolume : 0; }
    [[nodiscard]] bool       isFluidValid(int tank, FluidStack const& s) const override { return tank == 0 && placeable(s); }

    int fill(FluidStack const& resource, bool simulate) override {
        if (!placeable(resource) || resource.amount < kBucketVolume || !usable() || level() != 0) return 0;
        if (!simulate && !setCauldron(mRegion, mPos, resource.id)) return 0;
        return kBucketVolume;
    }

    FluidStack drain(FluidStack const& resource, bool simulate) override {
        auto c = current();
        if (!c.isSameFluid(resource)) return {};
        return drainAny(resource.amount, simulate);
    }

    FluidStack drainAny(int maxDrain, bool simulate) override {
        auto c = current();
        if (c.amount < kBucketVolume || maxDrain < kBucketVolume) return {};
        if (!simulate && !setCauldron(mRegion, mPos, {})) return {};
        return c;
    }

private:
    [[nodiscard]] static bool placeable(FluidStack const& s) {
        return !s.empty() && s.nbt.empty() && (s.id == ids::Water || s.id == ids::Lava || s.id == ids::PowderSnow);
    }
    [[nodiscard]] Block const& block() const { return mRegion.getBlock(mPos); }
    [[nodiscard]] bool usable() const { return isCauldron(block()) && !cauldronHasExtras(mRegion, mPos); }
    [[nodiscard]] int  level() const { return block().getState<int>(VanillaStates::FillLevel()).value_or(0); }

    [[nodiscard]] FluidStack current() const {
        if (!usable()) return {};
        auto const& b   = block();
        int         lvl = std::clamp(level(), 0, kMaxLevel);
        if (lvl == 0) return {};
        std::string_view id;
        switch (b.getState<int>(VanillaStates::CauldronLiquid()).value_or(0)) {
        case static_cast<int>(CauldronLiquidType::Lava):
            id = ids::Lava;
            break;
        case static_cast<int>(CauldronLiquidType::PowderSnow):
            id = ids::PowderSnow;
            break;
        default:
            id = b.getTypeName() == "minecraft:lava_cauldron" ? ids::Lava : ids::Water;
        }
        return {std::string(id), lvl * kBucketVolume / kMaxLevel};
    }

    BlockSource& mRegion;
    BlockPos     mPos;
};

// ---- items ----------------------------------------------------------------------------------------

/// A swap container (bucket, bottle, ...) as a one-tank item handler working on its own copy.
/// Whole containers only, and only for a single item (count 1), like NeoForge's bucket wrapper.
class SwapContainerHandler final : public IFluidHandlerItem {
public:
    SwapContainerHandler(ItemStack stack, core::FluidRegistry const& registry) : mStack(std::move(stack)), mRegistry(registry) {}

    [[nodiscard]] ItemStack const& container() const override { return mStack; }
    [[nodiscard]] int              tanks() const override { return 1; }
    [[nodiscard]] FluidStack       fluidInTank(int tank) const override {
        auto c = contents();
        return tank == 0 && c ? c->fluid : FluidStack{};
    }
    [[nodiscard]] int tankCapacity(int tank) const override {
        if (tank != 0) return 0;
        if (auto c = contents()) return c->fluid.amount;
        int  best = 0;
        auto id   = game::ItemConv::idOf(mStack);
        for (auto const& e : mRegistry.containers()) {
            if (e.emptyItem == id) best = std::max(best, e.fluid.amount);
        }
        return best;
    }
    [[nodiscard]] bool isFluidValid(int tank, FluidStack const& s) const override {
        return tank == 0 && !contents() && mRegistry.fill(game::ItemConv::idOf(mStack), s).has_value();
    }

    int fill(FluidStack const& resource, bool simulate) override {
        if (resource.empty() || mStack.isNull() || mStack.mCount != 1 || contents()) return 0;
        auto f = mRegistry.fill(game::ItemConv::idOf(mStack), resource);
        if (!f || resource.amount < f->amount) return 0;
        if (!simulate) mStack = game::ItemConv::make(f->item, 1, f->aux);
        return f->amount;
    }

    FluidStack drain(FluidStack const& resource, bool simulate) override {
        auto c = contents();
        if (!c || !c->fluid.isSameFluid(resource)) return {};
        return drainAny(resource.amount, simulate);
    }

    FluidStack drainAny(int maxDrain, bool simulate) override {
        auto c = contents();
        if (!c || mStack.mCount != 1 || maxDrain < c->fluid.amount) return {};
        if (!simulate) mStack = game::ItemConv::make(c->emptyItem, 1, 0);
        return c->fluid;
    }

private:
    [[nodiscard]] std::optional<core::FluidRegistry::Contents> contents() const {
        if (mStack.isNull()) return std::nullopt;
        return mRegistry.contentsOf(game::ItemConv::idOf(mStack), mStack.getAuxValue());
    }

    ItemStack                  mStack;
    core::FluidRegistry const& mRegistry;
};

/// A backpack item's tanks (the backpack item itself never changes).
class BackpackItemFluid final : public IFluidHandlerItem {
public:
    BackpackItemFluid(ItemStack stack, std::shared_ptr<IFluidHandler> inner) : mStack(std::move(stack)), mInner(std::move(inner)) {}

    [[nodiscard]] ItemStack const& container() const override { return mStack; }
    [[nodiscard]] int              tanks() const override { return mInner->tanks(); }
    [[nodiscard]] FluidStack       fluidInTank(int tank) const override { return mInner->fluidInTank(tank); }
    [[nodiscard]] int              tankCapacity(int tank) const override { return mInner->tankCapacity(tank); }
    [[nodiscard]] bool isFluidValid(int tank, FluidStack const& s) const override { return mInner->isFluidValid(tank, s); }
    int                fill(FluidStack const& r, bool simulate) override { return mInner->fill(r, simulate); }
    FluidStack         drain(FluidStack const& r, bool simulate) override { return mInner->drain(r, simulate); }
    FluidStack         drainAny(int maxDrain, bool simulate) override { return mInner->drainAny(maxDrain, simulate); }

private:
    ItemStack                      mStack;
    std::shared_ptr<IFluidHandler> mInner;
};

/// Energy kept as an int in an item's user data.
class UserDataEnergy final : public EnergyStorage {
public:
    UserDataEnergy(ItemStack& stack, int capacity, int maxReceive, int maxExtract, std::string key)
    : EnergyStorage(capacity, maxReceive, maxExtract, read(stack, key)),
      mStack(stack),
      mKey(std::move(key)) {}

private:
    static int read(ItemStack const& stack, std::string const& key) {
        if (stack.isNull() || !stack.mUserData || !stack.mUserData->contains(key, Tag::Type::Int)) return 0;
        int v = (*stack.mUserData)[key];
        return v;
    }

    void onChanged() override {
        if (mStack.isNull()) return;
        auto tag = mStack.mUserData ? mStack.mUserData->clone() : std::make_unique<CompoundTag>();
        tag->putInt(mKey, mEnergy);
        mStack.setUserData(std::move(tag));
    }

    ItemStack&  mStack;
    std::string mKey;
};

std::shared_ptr<game::Backpack> backpackByUuid(std::string_view uuid) {
    auto& storage = game::Storage::get();
    if (uuid.empty() || !storage.available()) return nullptr;
    return storage.find(std::string(uuid));
}

std::shared_ptr<game::Backpack> backpackOfItem(ItemStack const& stack) {
    if (!game::isBackpack(stack)) return nullptr;
    auto uuid = game::backpackUuid(stack);
    return uuid ? backpackByUuid(*uuid) : nullptr;
}

// ---- the service ----------------------------------------------------------------------------------

class ServiceImpl final : public CapabilityService {
public:
    static constexpr int kApiVersion = 1;

    ServiceImpl() : mFluids(core::FluidRegistry::withBuiltins()) { registerBuiltins(); }

    [[nodiscard]] int apiVersion() const noexcept override { return kApiVersion; }

    bool unregister(Token token) override {
        auto it = mRegs.find(token);
        if (it == mRegs.end()) return false;
        auto const& r = it->second;
        switch (r.kind) {
        case Kind::ItemEnergy:
            mItemEnergy.erase(r.key);
            break;
        case Kind::BlockEnergy:
            mBlockEnergy.erase(r.key);
            break;
        case Kind::ItemFluid:
            mItemFluid.erase(r.key);
            break;
        case Kind::BlockFluid:
            mBlockFluid.erase(r.key);
            break;
        case Kind::Fluid:
            mFluids.removeFluid(r.key);
            break;
        case Kind::Container:
            mFluids.removeContainer(r.key, r.aux);
            break;
        }
        mRegs.erase(it);
        return true;
    }

    // ---- energy ----------------------------------------------------------------------------------
    Token registerItemEnergy(std::string_view itemId, ItemEnergyProvider provider) override {
        return add(mItemEnergy, Kind::ItemEnergy, itemId, std::move(provider));
    }
    Token registerBlockEnergy(std::string_view blockId, BlockEnergyProvider provider) override {
        return add(mBlockEnergy, Kind::BlockEnergy, blockId, std::move(provider));
    }

    std::shared_ptr<IEnergyStorage> itemEnergy(ItemStack& stack) override {
        if (stack.isNull()) return nullptr;
        auto it = mItemEnergy.find(game::ItemConv::idOf(stack));
        return it == mItemEnergy.end() ? nullptr : it->second.second(stack);
    }

    std::shared_ptr<IEnergyStorage> blockEnergy(BlockSource& region, BlockPos const& pos, Side side) override {
        auto it = mBlockEnergy.find(region.getBlock(pos).getTypeName());
        return it == mBlockEnergy.end() ? nullptr : it->second.second(region, pos, side);
    }

    std::shared_ptr<IEnergyStorage> backpackEnergy(std::string_view uuid) override {
        return backpack::energyStorage(backpackByUuid(uuid));
    }

    std::shared_ptr<IEnergyStorage>
    makeItemEnergyStorage(ItemStack& stack, int capacity, int maxReceive, int maxExtract, std::string_view key) override {
        if (key.empty()) return nullptr;
        return std::make_shared<UserDataEnergy>(stack, capacity, maxReceive, maxExtract, std::string(key));
    }

    // ---- fluid registry --------------------------------------------------------------------------
    Token registerFluid(FluidInfo info) override {
        auto key = info.id;
        if (!mFluids.addFluid(std::move(info))) return 0;
        return remember(Kind::Fluid, std::move(key));
    }
    std::optional<FluidInfo> fluid(std::string_view id) const override {
        auto const* f = mFluids.fluid(id);
        return f ? std::optional<FluidInfo>(*f) : std::nullopt;
    }
    std::vector<FluidInfo> fluids() const override { return mFluids.fluids(); }
    bool fluidHasTag(std::string_view fluidId, std::string_view tag) const override { return mFluids.hasTag(fluidId, tag); }

    Token registerFluidContainer(FluidContainer container) override {
        auto key = container.filledItem;
        int  aux = container.filledAux;
        if (!mFluids.addContainer(std::move(container))) return 0;
        return remember(Kind::Container, std::move(key), aux);
    }

    std::optional<ContainerContents> containerContents(ItemStack const& filled) const override {
        if (filled.isNull()) return std::nullopt;
        auto c = mFluids.contentsOf(game::ItemConv::idOf(filled), filled.getAuxValue());
        if (!c) return std::nullopt;
        auto empty = game::ItemConv::make(c->emptyItem, 1, 0);
        if (empty.isNull()) return std::nullopt;
        return ContainerContents{std::move(c->fluid), std::move(empty)};
    }

    std::optional<FilledContainer> fillContainer(ItemStack const& empty, FluidStack const& fluid) const override {
        if (empty.isNull() || empty.getAuxValue() != 0) return std::nullopt;
        auto f = mFluids.fill(game::ItemConv::idOf(empty), fluid);
        if (!f) return std::nullopt;
        auto filled = game::ItemConv::make(f->item, 1, f->aux);
        if (filled.isNull()) return std::nullopt;
        return FilledContainer{std::move(filled), f->amount};
    }

    // ---- fluid capabilities ----------------------------------------------------------------------
    Token registerItemFluid(std::string_view itemId, ItemFluidProvider provider) override {
        return add(mItemFluid, Kind::ItemFluid, itemId, std::move(provider));
    }
    Token registerBlockFluid(std::string_view blockId, BlockFluidProvider provider) override {
        return add(mBlockFluid, Kind::BlockFluid, blockId, std::move(provider));
    }

    std::shared_ptr<IFluidHandlerItem> itemFluid(ItemStack const& stack) override {
        if (stack.isNull()) return nullptr;
        auto id = game::ItemConv::idOf(stack);
        if (auto it = mItemFluid.find(id); it != mItemFluid.end()) return it->second.second(stack);
        if (mFluids.contentsOf(id, stack.getAuxValue()) || mFluids.isEmptyContainer(id)) {
            return std::make_shared<SwapContainerHandler>(stack, mFluids);
        }
        return nullptr;
    }

    std::shared_ptr<IFluidHandler> blockFluid(BlockSource& region, BlockPos const& pos, Side side) override {
        auto it = mBlockFluid.find(region.getBlock(pos).getTypeName());
        return it == mBlockFluid.end() ? nullptr : it->second.second(region, pos, side);
    }

    std::shared_ptr<IFluidHandler> backpackFluid(std::string_view uuid) override {
        return backpack::fluidHandler(backpackByUuid(uuid));
    }

    // ---- world helpers ---------------------------------------------------------------------------
    FluidStack pickupFluidSource(BlockSource& region, BlockPos const& pos, bool simulate) override {
        Block const& block = region.getBlock(pos);
        auto const*  info  = sourceFluidOf(block);
        if (!info) return {};
        // Copy first: block-change listeners may (un)register fluids while the block changes.
        FluidStack out{info->id, kBucketVolume};
        if (!simulate) {
            auto const* air = blockByName("minecraft:air");
            if (!air || !setBlock(region, pos, *air)) return {};
        }
        return out;
    }

    int placeFluidSource(BlockSource& region, BlockPos const& pos, FluidStack const& fluid, bool simulate) override {
        if (fluid.amount < kBucketVolume || !fluid.nbt.empty()) return 0;
        auto const* info = mFluids.fluid(fluid.id);
        if (!info || info->sourceBlock.empty()) return 0;
        auto const* source = blockByName(info->sourceBlock);
        if (!source) return 0;
        Block const& there = region.getBlock(pos);
        bool         free  = there.isAir() || (isLiquidBlock(there) && !sourceFluidOf(there));
        if (!free) return 0;
        if (fluid.id == ids::Water && region.getDimension().mUltraWarm) return 0;
        if (!simulate && !setBlock(region, pos, *source)) return 0;
        return kBucketVolume;
    }

    [[nodiscard]] core::FluidRegistry const& registry() const { return mFluids; }

private:
    enum class Kind { ItemEnergy, BlockEnergy, ItemFluid, BlockFluid, Fluid, Container };
    struct Registration {
        Kind        kind;
        std::string key;
        int         aux = 0;
    };
    template <class P>
    using ProviderMap = std::map<std::string, std::pair<Token, P>, std::less<>>;

    Token remember(Kind kind, std::string key, int aux = 0) {
        Token t = mNextToken++;
        mRegs.emplace(t, Registration{kind, std::move(key), aux});
        return t;
    }

    template <class P>
    Token add(ProviderMap<P>& map, Kind kind, std::string_view key, P provider) {
        if (key.empty() || !provider || map.find(key) != map.end()) return 0;
        Token t = remember(kind, std::string(key));
        map.emplace(std::string(key), std::make_pair(t, std::move(provider)));
        return t;
    }

    static bool isLiquidBlock(Block const& block) {
        auto const& n = block.getTypeName();
        return n == "minecraft:water" || n == "minecraft:flowing_water" || n == "minecraft:lava" || n == "minecraft:flowing_lava";
    }

    /// The registered fluid whose 1000 mB source block `block` is (liquids: only sources).
    FluidInfo const* sourceFluidOf(Block const& block) const {
        std::string name = block.getTypeName();
        if (name == "minecraft:flowing_water") name = "minecraft:water";
        else if (name == "minecraft:flowing_lava") name = "minecraft:lava";
        if (isLiquidBlock(block)) {
            auto depth = block.getState<int>(VanillaStates::LiquidDepth());
            if (!depth || *depth != 0) return nullptr;
        }
        for (auto const& f : mFluids.fluids()) {
            if (!f.sourceBlock.empty() && f.sourceBlock == name) return &f;
        }
        return nullptr;
    }

    void registerBuiltins() {
        auto cauldron = [](BlockSource& region, BlockPos const& pos, Side) -> std::shared_ptr<IFluidHandler> {
            return std::make_shared<CauldronHandler>(region, pos);
        };
        registerBlockFluid("minecraft:cauldron", cauldron);
        registerBlockFluid("minecraft:lava_cauldron", cauldron);

        // Placed backpacks expose their tanks and battery to pipes / cables (SB block capabilities).
        for (auto const& id : placed::blockIds()) {
            registerBlockFluid(id, [](BlockSource& region, BlockPos const& pos, Side) {
                return backpack::fluidHandler(placed::PlacedBackpacks::get().backpackAt(region, pos));
            });
            registerBlockEnergy(id, [](BlockSource& region, BlockPos const& pos, Side) {
                return backpack::energyStorage(placed::PlacedBackpacks::get().backpackAt(region, pos));
            });
        }

        // Backpack items expose them too (SB item capabilities).
        for (auto const& t : core::kTiers) {
            auto id = game::backpackItemId(t.tier);
            registerItemEnergy(id, [](ItemStack& stack) { return backpack::energyStorage(backpackOfItem(stack)); });
            registerItemFluid(id, [](ItemStack const& stack) -> std::shared_ptr<IFluidHandlerItem> {
                if (!admin::sbServer().itemFluidHandlerEnabled) return nullptr; // SB server config
                auto inner = backpack::fluidHandler(backpackOfItem(stack));
                return inner ? std::make_shared<BackpackItemFluid>(stack, std::move(inner)) : nullptr;
            });
        }
    }

    core::FluidRegistry                        mFluids;
    ProviderMap<ItemEnergyProvider>            mItemEnergy;
    ProviderMap<BlockEnergyProvider>           mBlockEnergy;
    ProviderMap<ItemFluidProvider>             mItemFluid;
    ProviderMap<BlockFluidProvider>            mBlockFluid;
    std::unordered_map<Token, Registration>    mRegs;
    Token                                      mNextToken = 1;
};

std::shared_ptr<ServiceImpl> const& instance() {
    static auto const s = std::make_shared<ServiceImpl>();
    return s;
}

/// Publishes the service to other mods while the mod is enabled.
class CapabilityModule {
public:
    void install() {
        if (ll::service::ServiceManager::getInstance().registerService(instance())) {
            log().info("Capability API registered (service {} v{})", CapabilityService::ServiceId.name, ServiceImpl::kApiVersion);
        } else {
            log().warn("Capability API: service {} is already registered", CapabilityService::ServiceId.name);
        }
    }
    void uninstall() { ll::service::ServiceManager::getInstance().unregisterService(CapabilityService::ServiceId); }
};

} // namespace

CapabilityService& service() { return *instance(); }

core::FluidRegistry const& fluidRegistry() { return instance()->registry(); }

} // namespace abp::api

ABP_REGISTER_MODULE_ORDERED(abp::api::CapabilityModule, abp::ModuleOrder::Upgrades + 1);
