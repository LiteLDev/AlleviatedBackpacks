# AlleviatedBackpacks capability API

Bedrock has no Forge Energy and no fluid-handler system. AlleviatedBackpacks provides both as a
small public API for LeviLamina mods. The Tank, Battery, Pump and XP Pump upgrades are built on
it, and other mods can use it to:

- make their items chargeable (energy items) or fluid containers;
- give their blocks an energy storage or fluid tanks (machines, cables, pipes);
- read and change the tanks and battery of backpacks, whether carried as items, placed as
  blocks, or looked up by uuid;
- register new fluids.

Units are **FE** for energy and **mB** for fluids (1 bucket = 1000 mB).

## Headers

Copy `src/api/abp/` into your include path. The API is header-only and needs no import library.

| Header | Contents | Game headers? |
| ------ | -------- | ------------- |
| `abp/Energy.h` | `IEnergyStorage`, `EnergyStorage` (buffer with transfer limits), `transferEnergy` | no |
| `abp/Fluid.h` | `FluidStack`, `IFluidHandler`, `FluidTank`, `CombinedFluidHandler`, `FluidInfo`, `FluidContainer`, fluid ids, `mergeFluid` | no |
| `abp/Xp.h` | `abp::api::xp`: points ↔ levels ↔ mB (matches SophisticatedCore's `XpHelper`) | no |
| `abp/Capabilities.h` | `CapabilityService` (the LeviLamina service), `IFluidHandlerItem`, `Side`, helpers | LeviLamina / `mc` |

## Getting the service

When AlleviatedBackpacks is enabled, it registers `abp::api::CapabilityService` with LeviLamina's
`ll::service::ServiceManager`. The service id is `AlleviatedBackpacks.CapabilityService`, version 1.

```cpp
#include "abp/Capabilities.h"

if (auto caps = abp::api::capabilities()) {   // nullptr while AlleviatedBackpacks is not enabled
    ...
}
// or get told when it appears or goes away:
auto listener = ll::service::ServiceManager::getInstance()
    .subscribeService<abp::api::CapabilityService>([](std::shared_ptr<abp::api::CapabilityService> const& caps) {
        if (caps) registerMyStuff(*caps);
    });
```

Add `AlleviatedBackpacks` to the `dependencies` in your `manifest.json` so that it is enabled
before your mod.

Rules:
- **Thread:** call everything on the server thread.
- **Lifetime:** handlers returned by the service are short-lived views. Use them right away and
  do not keep them across ticks.
  - An item energy storage writes into the `ItemStack&` you passed. That stack must outlive the
    storage.
  - Block handlers refer to the `BlockSource` you passed.
- **Registrations:** every `register*` call returns a `Token`. The value is `0` when the
  registration is refused, which happens when the key is already taken or the input is invalid.
  Call `caps->unregister(token)` when your mod is disabled. Providers are keyed by item or block
  id, and each id can have only one provider.
- **ABI:** the service is a pure interface. New functions are only ever added at the end, and
  `apiVersion()` goes up when they are. A breaking change would get a new service version.

## Energy

`IEnergyStorage` follows the NeoForge contract:
- `receive(max, simulate)` and `extract(max, simulate)` return the amount moved, or the amount
  that would move when `simulate` is true.
- `stored()`, `capacity()`, `canReceive()` and `canExtract()` describe the storage.

### Make an item chargeable

```cpp
auto token = caps->registerItemEnergy("mymod:battery", [caps](ItemStack& stack) {
    // 50 000 FE, 1 000 FE in and out per call; stored as an int in the item's user data.
    return caps->makeItemEnergyStorage(stack, 50000, 1000, 1000, "mymod_energy");
});
```

You can also return your own `IEnergyStorage`, for example a subclass of `abp::api::EnergyStorage`
that persists in `onChanged()`. The Battery upgrade charges and discharges any item that has an
energy storage.

### Give a block an energy storage

```cpp
caps->registerBlockEnergy("mymod:generator", [](BlockSource& region, BlockPos const& pos, abp::api::Side side)
        -> std::shared_ptr<abp::api::IEnergyStorage> {
    return myGeneratorAt(region, pos); // nullptr: no capability on that side
});
```

### Query energy

```cpp
auto item  = caps->itemEnergy(stack);                       // any registered item, and backpacks
auto block = caps->blockEnergy(region, pos, Side::Up);      // registered blocks, and placed backpacks
auto pack  = caps->backpackEnergy(uuid);                    // a backpack's Battery upgrade
if (item && block) abp::api::transferEnergy(*block, *item, 1000);
```

## Fluids

### Fluid stacks and ids

A `FluidStack` has three parts:
- `id`: a namespaced fluid id;
- `amount`: the amount in mB;
- `nbt`: extra identity, written as compact JSON object text with sorted keys, or `""` for none.
  Two stacks hold the same fluid when both `id` and `nbt` are equal (`isSameFluid`). Build the
  `nbt` text canonically, for example with `abp::api::intNbt(key, value)`.

Built-in fluids (`abp::api::fluids::*`):

| Id | Source block | Tags | Containers |
| -- | ------------ | ---- | ---------- |
| `minecraft:water` | `minecraft:water` | `c:water` | water bucket (1000), water bottle = potion aux 0 (250) |
| `minecraft:lava` | `minecraft:lava` | `c:lava` | lava bucket (1000) |
| `minecraft:milk` | – | `c:milk` | milk bucket (1000) |
| `minecraft:powder_snow` | `minecraft:powder_snow` | – | powder snow bucket (1000) |
| `minecraft:honey` | – | `c:honey` | honey bottle (250) |
| `minecraft:potion`, `minecraft:splash_potion`, `minecraft:lingering_potion` | – | `c:potion` | potion / splash / lingering bottle (250); nbt `{"potion":<aux>}` |
| `abp:experience` | – | `c:experience` | bottle o' enchanting (160 = 8 points); 20 mB per XP point |

### Register a fluid

```cpp
caps->registerFluid({
    .id = "mymod:oil", .nameKey = "mymod.fluid.oil", .nameFallback = "Oil",
    .color = 0xFF202020, .tags = {"c:oil"}, .sourceBlock = "mymod:oil",  // "" = cannot be placed
});
```

If your mod has its own liquid experience, tag it `c:experience`. The XP Pump and
`fillTagged`/`drainTagged` then treat it as experience, as in SophisticatedCore.

### Fluid containers

The simplest kind is a swap container, where one filled item holds a fixed amount:

```cpp
caps->registerFluidContainer({
    .filledItem = "mymod:oil_can", .filledAux = 0, .emptyItem = "mymod:empty_can",
    .fluid = {"mymod:oil", 500},
});
```

`filledAux = -1` with `auxNbtKey` covers every aux value of an item. The aux goes into the fluid's
nbt; this is how potions work. The Tank upgrade's input and output slots and `itemFluid()` use
these containers automatically. Other helpers:
- `containerContents(filled)` returns the fluid and the empty container;
- `fillContainer(empty, fluid)` returns the filled container and the amount it takes.

For anything more complex, such as a portable tank item, register a full handler instead. An item
handler works on a copy of the stack, so put `container()` back where the item came from:

```cpp
caps->registerItemFluid("mymod:portable_tank", [](ItemStack const& stack) -> std::shared_ptr<abp::api::IFluidHandlerItem> {
    return std::make_shared<MyTankItemHandler>(stack);
});

auto h = caps->itemFluid(heldItem);            // registered handlers, else swap containers
if (h && h->fill({"minecraft:water", 1000}, false) == 1000) setHeldItem(h->container());
```

### Blocks

```cpp
caps->registerBlockFluid("mymod:tank", [](BlockSource& region, BlockPos const& pos, abp::api::Side side)
        -> std::shared_ptr<abp::api::IFluidHandler> { return myTankAt(region, pos); });

auto h = caps->blockFluid(region, pos, Side::North);
```

Built-in block handlers:
- **Cauldrons** (`minecraft:cauldron`, `minecraft:lava_cauldron`) have one 1000 mB tank of water,
  lava or powder snow. As in NeoForge, only whole buckets move: an empty cauldron takes 1000 mB
  and a full one gives 1000 mB.
  - A partly filled cauldron reports `fill_level / 6` of a bucket but cannot be drained.
  - Potion and dyed-water cauldrons are left alone.
- **Placed backpacks** expose their tanks and battery (see below).

World helpers for pumps (they do no permission checks; the caller handles that):
- `pickupFluidSource(region, pos, simulate)` takes a 1000 mB source block, such as water or lava
  with `liquid_depth` 0 or powder snow, and leaves air.
- `placeFluidSource(region, pos, fluid, simulate)` places one. The target must be air or a
  flowing liquid. Water is refused in ultra-warm dimensions.

Use `CombinedFluidHandler` to expose several tanks as one handler, and `FluidTank` as a ready-made
single tank (override `onContentsChanged()` to save it).

## Backpacks

As in Sophisticated Backpacks, a backpack's tanks and battery are available in three ways:
- **by uuid:** `caps->backpackFluid(uuid)` and `caps->backpackEnergy(uuid)`;
- **as an item:** `caps->itemFluid(backpackStack)` and `caps->itemEnergy(backpackStack)`;
- **as a placed block** (`alleviatedbackpacks:placed_backpack`): `caps->blockFluid(region, pos)`
  and `caps->blockEnergy(region, pos)`, for pipes and cables.

All of them return nullptr when the backpack has no Tank (fluids) or no Battery (energy).

SB's rules apply:
- **Tanks:** all Tank upgrades, combined in upgrade-slot order.
  - `fill` pours into each tank in turn.
  - `drain(resource)` collects that fluid from every tank.
  - `drainAny` drains the first tank that has anything.
- **Energy:** the first battery-like upgrade in slot order.

## XP helper

`abp/Xp.h` provides:
- `experienceForLevel`, `experienceLimitOnLevel`, `levelForExperience`, `levelsForExperience` and
  `totalExperience(level, progress)`;
- `experienceToLiquid(points)` and `liquidToExperience(mB)`, at 20 mB per point.

## For AlleviatedBackpacks units (internal)

- `src/api/BackpackCapabilities.h` lets upgrades contribute to the backpack handlers:
  - `ABP_REGISTER_BACKPACK_TANK("<upgrade id>", factory)` registers a factory that returns an
    `IFluidHandler` view of one installed upgrade. Its tanks join the combined handler.
  - `ABP_REGISTER_BACKPACK_BATTERY("<upgrade id>", factory)` registers an `IEnergyStorage` factory.
  - `addFluidWrapper` and `addEnergyWrapper` add wrappers, for example Inception (nested
    backpacks) or Void (swallowing fluids).
  - `slotRows(bp)` and `stackMultiplier(bp)` return the sizing inputs.
- `src/core/Capacity.h` has SB's sizing rules:
  - `slotRows` is ceil(slots/9) up to 81 slots, ceil(slots/12) above;
  - battery capacity is 10000 FE × rows × adjusted multiplier;
  - battery IO is 20 × rows × multiplier;
  - tank capacity is 4000 mB × rows × multiplier;
  - tank IO is max(1000, 20 × rows × multiplier);
  - the adjusted multiplier is `1 + ratio × (multiplier − 1)`.
  The phase-1 Tank upgrade still uses its own sizing (`core/Fluid.h` `tankCapacity`); the
  phase-2 Tank and Battery units switch to these.
- `src/api/Service.h` gives `service()` and `fluidRegistry()` for in-mod use.
- `src/core/FluidRegistry.h` maps phase-1 tank ids to API stacks (`toApi`, `fromApi`,
  `legacyIdOf`): `"water"` becomes `minecraft:water`, `"potion:7"` becomes `minecraft:potion` with
  nbt `{"potion":7}`, and `"experience"` becomes `abp:experience`. The Tank upgrade still stores
  phase-1 ids.
