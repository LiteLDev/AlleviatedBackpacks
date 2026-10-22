# Sophisticated Backpacks parity

This file maps every feature of Sophisticated Backpacks (SB) and Sophisticated Core (SC) 1.21.x to
its state in AlleviatedBackpacks. The map is at roughly class or feature granularity. It was produced
by an audit of SB/SC's `upgrades/`, `backpack/`, `common/`, `client/`, `settings/`, `crafting/`,
`command/`, `data/`, `network/`, `util/`, `Config.java` and lang files against `src/**` and
`assets/**`.

Statuses:

- **Exact**: same behaviour, numbers, defaults, texts and controls as SB/SC. Bedrock-only mechanics
  are noted where they differ only in form (e.g. LevelDB instead of NBT files).
- **Gap**: SB does something we don't do, or do differently. The row describes both sides.
- **GX-1 / GX-2 / GX-3**: a known gap already assigned to the rendering (GX-1), upgrade/world
  behaviour (GX-2) or GUI/settings/input (GX-3) unit.
- **N/A**: only SB's compat with Java-only mods (Chipped, Sawmill; also Curios, Accessories,
  Botania, JEI/EMI/REI, Create, FTB Chunks, Mouse Tweaks, Inventory Sorter, ...).

"GX-4" in a row means the gap was found and fixed by the parity audit (this revision).

## Items, tiers and recipes

| Feature (SB/SC) | Status | Where | Notes |
|---|---|---|---|
| Six backpacks: leather (27/1), copper (45/1), iron (54/2), gold (81/3), diamond (108/5), netherite (120/7) | Exact | `core/Tier.h`, `feature/admin/SbConfig.*` | Slot counts come from `sophisticatedbackpacks-server.json` (`<tier>Backpack.inventorySlotCount/upgradeSlotCount`, 1..144 / 0..10) |
| Backpacks never stack; every upgrade, conversion and downgrade item stacks to 64; upgrade base 16 | Exact (GX-4) | `assets/pack/bp/items/*.json` | Before: 1 or 16 for most upgrades |
| Netherite backpack is fire resistant as an item | Exact (GX-4) | `netherite_backpack.json` (`minecraft:fire_resistant`) | |
| Backpacks can't go into bundles or shulker boxes (`canBePlacedInContainerItems` = false) | Exact (GX-4 for bundles) | item JSON `minecraft:storage_weight_modifier`, `feature/admin/SbConfig.cpp` | The bundle rule is static: it ignores `canBePlacedInContainerItems = true` |
| Backpack recipe, tier upgrade recipes keeping contents, netherite smithing | Exact | `assets/pack/bp/recipes/`, `feature/tiers/TierCrafting.cpp` | |
| Tier upgrade refused for a linked non-primary endpoint | Exact (GX-4) | `TierCrafting.cpp` (`Storage::tierGrowthGate`) | |
| `backpack_dye` (1-8 dyes, main/accent by column) | Exact | `recipes/dye/`, `feature/tiers/DyeTag.cpp` | |
| Cauldron wash resets main and accent colour | Exact (GX-4) | `feature/tiers/TierLore.cpp` | Before: only the main colour |
| All 63 regular upgrade recipes (patterns, keys, counts) | Exact | `assets/pack/bp/recipes/*.json` | |
| Ingredient tags `c:chests/wooden`, `c:chests`, `c:glass_blocks`, `c:obsidians` | Exact (GX-4) | `feature/tiers/DyeTag.cpp` (item tags `alleviatedbackpacks:wooden_chests/chests/glass_blocks/obsidians`) | Before: chest, glass and obsidian only |
| `upgrade_next_tier` keeps the upgrade's data | Exact | `TierCrafting.cpp` (`carryUpgradeData`) | |
| SC `upgrade_clear` (an upgrade alone in the grid loses its data) | Exact (GX-4) | `assets/pack/bp/recipes/clear/*.json` | |
| Ender Linker item and recipe | Exact | `ender_linker.json` | Linking/clear crafting: GX-2 |
| Hopper / Advanced Hopper upgrade | Extra | `upgrade/hopper` | Sophisticated Storage upgrades, not in SB |
| SC `xp_bucket` | N/A | | Bedrock has no custom fluids; experience is stored as a tank fluid |
| Chest loot (9 tables, weights, `chestLootEnabled`) | Exact | `feature/wearable/ChestLoot.cpp` | |

## Backpack item, block and world

| Feature (SB class) | Status | Where | Notes |
|---|---|---|---|
| `BackpackItem.use` / sneak to place (facing, waterlogged, wool sounds, creative keeps item) | Exact | `feature/placed/PlacedBackpacks.cpp` | |
| `BackpackItem.cloneBackpack` -> `cloneSubbackpacks` (creative placement) | Gap | `PlacedBackpacks::placeFromHand` | SB gives nested backpacks of the clone new storages; ours copies the data, so nested backpacks share uuids with the originals |
| `inventoryTick` skips spectators and the dead | Exact (GX-4) | `game/PlayerTicker.cpp` | |
| `onDroppedByPlayer` (the open backpack can't be dropped) | Exact | `net/NativeScreens.cpp` | |
| `makesPiglinsNeutral` (gold backpack worn) | Gap | | Bedrock piglins only check golden armour |
| `createEntity` / UUID deduplication | Exact | `feature/dedup` | |
| Everlasting item entity (no despawn, floats from void/lava, fire immune, particles) | Exact | `upgrade/everlasting` | Wither/dragon: GX-2. Explosion knock-back (`ignoreExplosion`) is unverified |
| `BackpackBlock`: open, sneak pick-up, comparator, waterlogging, states, hardness, pistons | Exact | `feature/placed` | |
| `BackpackBlock.useItemOn` with a fluid container: fill/drain tanks, bucket sounds; a bucket does nothing without tanks | Exact (GX-4) | `PlacedBackpacks::useFluidContainer` | Before: no sound; a bucket on a backpack without tanks opened the GUI |
| Dispenser places a backpack (`BackpackDispenseBehavior`) | Gap | | |
| `BackpackBlockEntity`: placed ticks, hopper/automation I/O, `noConnectionBlocks`, loot on first access | Exact | `feature/placed`, `feature/admin/Connections.cpp` | |
| `BackpackStorage` drops empty, never-accessed storages on load | Gap | `game/Storage.cpp` | `removeNonPlayer` is exact |
| `BackpackAccessLogger`, `/sbp list/give/removeNonPlayer` | Exact | `feature/admin` | Bedrock chat has no click/hover events |
| Backpack templates (`/sbp template ...`) | Exact | `feature/templates` | SNBT export: GX-2 |
| Opening another player's worn backpack | Exact | `feature/access` | |
| Mob backpacks (spawn chances, weights, loot, potions, armour, jukebox, drops) | Exact | `feature/wearable/MobBackpacks.cpp` | Mob jukebox disc count follows `advancedJukeboxUpgrade` size (GX-4). Converted-mob health, creeper buff strip, jukebox follow: GX-2 |
| Too-many-backpacks slowness, `onlyWornBackpackTriggersUpgrades` | Exact | `feature/nerf` | |
| Linked storage (Ender Linker groups, primary/secondary endpoints) | Exact | `feature/linked` | Ender Linker crafting, mob catcher x linked: GX-2 |
| Automatic insert order: same-item stacks, then empty memory slots, then empty slots | Exact (GX-4) | `core/BackpackInventory.cpp` | Before: memory slots first |
| `LegacyBackpackDataMigration`, shape reload listener | N/A | | Java-only |

## Upgrades

| Upgrade (SB/SC classes) | Status | Where | Notes |
|---|---|---|---|
| Pickup / Advanced Pickup | Exact | `upgrade/pickup` | |
| Filter / Advanced Filter (direction, contents filter) | Exact | `upgrade/filter` | |
| Several Filter upgrades combine as SC `FilteredItemHandler` (OR of allow lists, else AND) | Exact (GX-4) | `FilterUpgrade.cpp` | Before: always AND |
| Filter logic (item/mod/tags, durability, NBT, any/all tags, primary match) | Exact | `core/Filter.*`, `upgrade/filter/FilterCommon.cpp` | |
| Tag list order (TreeSet<ResourceLocation>: path, then namespace) | Exact (GX-4) | `core::tagLess` | Before: plain string order |
| Magnet / Advanced Magnet (range 3/5, cooldowns, XP into tanks) | Exact | `upgrade/magnet` | |
| Feeding / Advanced Feeding | Exact | `upgrade/feeding` | |
| Compacting / Advanced Compacting | Exact | `upgrade/compacting` | Leftovers: GX-2 |
| Void / Advanced Void | Exact | `upgrade/void` | |
| Restock / Deposit (+ advanced) | Exact | `upgrade/restock`, `upgrade/deposit` | Inception combined inventories: GX-2 |
| Refill / Advanced Refill (6/12 slots, targets, pick block) | Exact | `upgrade/refill` | Slot counts configurable in `config/refill.json` (GX-4) |
| Stack upgrades (starter..omega, downgrades, conversions) | Exact | `upgrade/stack` | |
| Smelting / Smoking / Blasting and Auto versions | Exact | `upgrade/cooking` | Particles and sound: GX-2 |
| Crafting | Exact | `upgrade/crafting` | Several per backpack: see the limits row |
| Stonecutter (SC `blockconverter` base) | Exact | `upgrade/stonecutter` | |
| Jukebox / Advanced Jukebox | Exact | `upgrade/jukebox` | |
| Tank (capacity, I/O rates, container slots, cooldown) | Exact | `upgrade/tank` | Fluid textures and potion tint: GX-1 |
| Battery (FE storage, I/O) | Exact | `upgrade/battery` | Energy API: `docs/API.md` |
| Pump / Advanced Pump | Exact | `upgrade/pump` | |
| XP Pump (levels, mending) | Exact | `upgrade/xppump` | |
| Alchemy / Advanced Alchemy | Exact | `upgrade/alchemy` | Multi-match and splash: GX-2 |
| Infinity / Survival Infinity | Exact | `upgrade/infinity` | Item tooltips added (GX-4) |
| Everlasting, Inception, Tool Swapper "already in another upgrade slot" messages | Exact (GX-4) | `UpgradeInfo::existsKey` | Before: the generic "Only 1x ..." text |
| Inception (nesting, both config flags) | Exact | `upgrade/inception` | Combined inventories: GX-2 |
| Tool Swapper / Advanced Tool Swapper (click/attack swaps, key, modes) | Exact | `upgrade/toolswapper` | The swap key now works in creative, as in SB (GX-4) |
| Advanced Tool Swapper filter with TAGS mode (SC FilterLogicControl.Advanced) | Exact (GX-4) | `client/ui/tabs/toolswapper`, `toolswapper/Upgrades.cpp` | Slot count from `config/toolswapper.json` `toolSwapperUpgrade` |
| Anvil, Smithing | Exact | `upgrade/anvil`, `upgrade/smithing` | |
| Mob Catcher / Advanced Mob Catcher | Exact | `upgrade/mobcatcher` | Config list defaults are empty, as in SB (GX-4). Variants, shrink animation: GX-1 |
| Upgrade limits per backpack: SB only limits through `maxUpgradesPerStorage` (stack 3, furnace 1, jukebox 1) and its conflict rules | Gap | `IUpgrade.h` `maxPerBackpack` | Crafting, stonecutter, anvil, smithing and refill are limited to 1 per backpack; cooking, jukebox and stack have built-in limits that config can't raise |
| Chipped / Sawmill compat upgrades | N/A | | Java-only mods |

## Configuration

| SB/SC key | Status | Notes |
|---|---|---|
| `sophisticatedbackpacks-server.json`: disallowed items, container items, no-interaction/no-connection blocks, slot counts, entity additions, nerfs, `maxUpgradesPerStorage`, fluid handler, other-player opening, item display, dedup, `canBePlacedInContainerItems` | Exact | `allowOpeningOtherPlayerBackpacks` and `itemDisplayDisabled` were split with `gui.json`: both files now count (off in either = off) (GX-4) |
| `sophisticatedbackpacks-common.json` `chestLootEnabled`, `sophisticatedcore-common.json` `enabledItems` | Exact | |
| Per-upgrade sections (filter slots, ranges, multipliers, tank/battery/pump/xp pump, cooking, compacting, inception, mob catcher) | Exact | Refill and Tool Swapper sections added (GX-4) |
| Jukebox `advancedJukeboxUpgrade.numberOfSlots/slotsInRow`, Alchemy `alchemyUpgrade.filterSlots` | Gap | Same values, but flat key names (`advancedSlots`, `alchemyFilterSlots`) instead of SB's sections |
| SC client `sortButtonsPosition`, `playButtonSound` | Exact | `client/ui/screen/ScreenConfig.*` |
| Unopened backpack lore uses the configured slot counts | Exact (GX-4) | `TierLore.cpp` |

## Client GUI, input and rendering

| Feature (SB/SC class) | Status | Where | Notes |
|---|---|---|---|
| StorageScreenBase layout, upgrade column, switches, sort/transfer buttons, search box, scroll panel, counts, memory/no-sort overlays, error overlay | Exact | `client/ui/screen/BackpackScreen.cpp`, `core/screen` | Bar error overlay, cursor draw order: GX-3 |
| Closed tabs drawn unless fully covered; tooltip only when the open tab doesn't cover its top | Exact (GX-4) | `BackpackScreen.cpp` | The two checks were swapped |
| Storage settings tab opens on any mouse button | Exact (GX-4) | `BackpackScreen.cpp` | |
| XP Pump action buttons work with any mouse button | Exact (GX-4) | `tabs/xppump/XpPumpTab.cpp` | |
| Empty upgrade slot icon | Gap | `SbStyle.cpp` | |
| Switching an upgrade on/off closes the open tab (SC RELOAD_NEEDED) | Gap | `BackpackScreen::rebuildTabs` | |
| Enter in text boxes keeps focus | Gap | `widgets/TextBox.cpp` | |
| Linked-storage role icon before the title | Gap | `BackpackScreen.cpp` | |
| Truncated open-tab title tooltip | Gap | `BackpackScreen.cpp` | |
| Clicking a tab border doesn't throw the cursor item | Gap | `BackpackScreen::onMouseButton` | |
| Middle-click sort anywhere but the player inventory, before other handlers | Gap | `BackpackScreen::onMouseButton` | |
| Drag distribution over upgrade slots | Gap | `BackpackScreen::canDragTo` | |
| Q / 1-9 over upgrade slots; bound (not literal) keys | Gap | `BackpackScreen::onKey`, `NativeScreens.cpp` | |
| Jukebox playtime overlay updates smoothly | Gap | `tabs/jukebox` | |
| Crafting "Previous" tooltip on the left | Gap | `tabs/crafting` | |
| LargeResultSelector, anvil name box, smithing help tooltips, stonecutter hit box details | Gap | `tabs/stonecutter`, `upgrade/anvil/AnvilTab.cpp`, `upgrade/smithing` | |
| Settings screens (main, memory, no sort, item display, templates) | Exact | `client/ui/settings`, `feature/gui` | Item Display 3D preview: GX-1. Server-side Keep Tab Open, Match-NBT sort: GX-3 |
| Key bindings (names, defaults, categories, open/inventory interaction/tool swap/toggles, sort, transfer) | Exact | `client/input` | |
| Right-click stashing | Exact | `client/input/Stash.cpp`, `feature/stash` | Stashing in all slots: GX-3 |
| Backpack tooltip and Shift contents tooltip | Exact | `client/tooltip` | Tooltip wrapping: GX-3 |
| Worn backpack model, upgrade particles, item colours | Exact | `client/render` | Dropped/item-frame model, two-colour slot icons, armour-stand base plate: GX-1 |
| Button sounds, crafting result preview colours, click-equip bug, diamond durability workaround | GX-3 / GX-1 | | |

## Strings

All 548 SB/SC strings in scope match SB's English (Java config-screen and compat strings are N/A).
GX-4 added the Infinity / Survival Infinity tooltips and the three "already in another upgrade slot"
messages. Messages now use SB's "Backpack" for the leather tier. Our zh_CN is an independent
translation: every string has one, but about 220 are worded differently from SB's zh_cn.json
(e.g. item names such as 压缩升级 vs SB's 压制升级). That is a Gap if SB's Chinese wording is required.
