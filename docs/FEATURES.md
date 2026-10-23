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
- **PLATFORM-LIMIT**: Bedrock (or LeviLamina) has no way to do what SB does; the row says why.
- **Extra**: ours only (not in SB).
- **N/A**: SB's compat with Java-only mods (Chipped, Sawmill; also Curios, Accessories, Botania,
  JEI/EMI/REI, Create, FTB Chunks, Mouse Tweaks, Inventory Sorter, ...) and Java-only internals.

"GX-1" .. "GX-5" in a row name the unit that closed the gap (GX-1 rendering, GX-2 upgrade / world
behaviour, GX-3 GUI / settings / input, GX-4 the parity audit, GX-5 the audit's leftovers). The
file was re-verified against the code of `main` by the final integration run.

## Items, tiers and recipes

| Feature (SB/SC) | Status | Where | Notes |
|---|---|---|---|
| Six backpacks: leather (27/1), copper (45/1), iron (54/2), gold (81/3), diamond (108/5), netherite (120/7) | Exact | `core/Tier.h`, `feature/admin/SbConfig.*` | Slot counts come from `sophisticatedbackpacks-server.json` (`<tier>Backpack.inventorySlotCount/upgradeSlotCount`, 1..144 / 0..10) |
| Backpacks never stack; every upgrade, conversion and downgrade item stacks to 64; upgrade base 16 | Exact (GX-4) | `assets/pack/bp/items/*.json` | Before: 1 or 16 for most upgrades |
| Netherite backpack is fire resistant as an item | Exact (GX-4) | `netherite_backpack.json` (`minecraft:fire_resistant`) | |
| Backpacks can't go into bundles or shulker boxes (`canBePlacedInContainerItems` = false, the default) | Exact | `feature/bundle/BundleRule.cpp`, `feature/admin/SbConfig.cpp` | Bundles (server validation and companion client prediction) and shulker boxes (screen and hoppers) follow the config |
| `canBePlacedInContainerItems = true` lets backpacks into bundles | Exact | `feature/bundle/BundleRule.cpp`, `client/bundle/BundleRuleClient.cpp` | The server's bundle validation (`DynamicContainerValidation::isItemAllowedInSlot`) follows the live config (`/backpack reload` applies); companion clients get the rule (`BundleRulePacket`) and refuse the move before sending it. A backpack weighs a whole bundle (Java 1/maxStackSize, Bedrock 64 of 64). The items' `storage_weight_modifier` never loaded (format 1.21.40) and was removed |
| Backpack recipe, tier upgrade recipes keeping contents, netherite smithing | Exact | `assets/pack/bp/recipes/`, `feature/tiers/TierCrafting.cpp`, `feature/tiers/SmithingBase.cpp` | GX-3: the Diamond backpack has no durability (like SB); Bedrock's recipe-load check "base item is not a piece of armor or equipment" is answered by a hook (`Recipes::_loadRecipe` + `ComponentItem::isDamageable`). Selftest `tiers.smithing_without_durability`, client e2e `gx3` (smithing table) |
| Tier upgrade refused for a linked non-primary endpoint | Exact (GX-4) | `TierCrafting.cpp` (`Storage::tierGrowthGate`) | |
| `backpack_dye` (1-8 dyes, main/accent by column) | Exact | `recipes/dye/`, `feature/tiers/DyeTag.cpp` | |
| Cauldron wash resets main and accent colour | Exact (GX-4) | `feature/tiers/TierLore.cpp` | Before: only the main colour |
| All 63 regular upgrade recipes (patterns, keys, counts) | Exact | `assets/pack/bp/recipes/*.json` | |
| Ingredient tags `c:chests/wooden`, `c:chests`, `c:glass_blocks`, `c:obsidians` | Exact (GX-4) | `feature/tiers/DyeTag.cpp` (item tags `alleviatedbackpacks:wooden_chests/chests/glass_blocks/obsidians`) | Before: chest, glass and obsidian only |
| `upgrade_next_tier` keeps the upgrade's data | Exact | `TierCrafting.cpp` (`carryUpgradeData`) | |
| SC `upgrade_clear` (an upgrade alone in the grid loses its data) | Exact (GX-4) | `assets/pack/bp/recipes/clear/*.json` | |
| Ender Linker item, recipe, linking and clear crafting | Exact (GX-2) | `ender_linker.json`, `feature/linked/EnderLinkerCrafting.cpp`, `recipes/ender_linker_*.json` | Selftests `linked.craft_recipe`, `linked.craft_finalize` |
| Hopper / Advanced Hopper upgrade | Extra | `upgrade/hopper` | Sophisticated Storage upgrades, not in SB |
| SC `xp_bucket` | PLATFORM-LIMIT | | Bedrock has no custom fluids or fluid buckets; experience is stored as a tank fluid (`abp:experience`) |
| Chest loot (9 tables, weights, `chestLootEnabled`) | Exact | `feature/wearable/ChestLoot.cpp` | |

## Backpack item, block and world

| Feature (SB class) | Status | Where | Notes |
|---|---|---|---|
| `BackpackItem.use` / sneak to place (facing, waterlogged, wool sounds, creative keeps item) | Exact | `feature/placed/PlacedBackpacks.cpp` | |
| `BackpackItem.cloneBackpack` -> `cloneSubbackpacks` (creative placement) | Exact | `PlacedBackpacks::placeFromHand` | The placed clone and every backpack directly inside it get new storages (one level, like SB). Selftest `placed.creative_clone` |
| `inventoryTick` skips spectators and the dead | Exact (GX-4) | `game/PlayerTicker.cpp` | |
| `onDroppedByPlayer` (the open backpack can't be dropped) | Exact | `net/NativeScreens.cpp` | |
| `makesPiglinsNeutral` (gold backpack worn) | Exact (GX-5) | `feature/wearable/PiglinNeutral.cpp` | A worn Gold Backpack counts as gold armour: the piglin's "player without golden armour" target entry is skipped for its wearer (`TargetGoal::_matchesTargetTypes`); its other entries (open container, block breaking, hurt-by) still anger it, as in SB |
| `createEntity` / UUID deduplication | Exact | `feature/dedup` | |
| Everlasting item entity (no despawn, floats from void/lava, fire immune, particles) | Exact | `upgrade/everlasting`, `feature/placed/EntityGriefing.cpp` | Withers and the dragon can't destroy it (GX-2, selftest `everlasting.wither_and_dragon`). Explosions no longer push it (`ignoreExplosion`, GX-5: Bedrock pushed it 1.6 blocks; `Explosion::explode` now restores its motion) |
| `BackpackBlock`: open, sneak pick-up, comparator, waterlogging, states, hardness, pistons | Exact | `feature/placed` | |
| `BackpackBlock.useItemOn` with a fluid container: fill/drain tanks, bucket sounds; a bucket does nothing without tanks | Exact (GX-4) | `PlacedBackpacks::useFluidContainer` | Before: no sound; a bucket on a backpack without tanks opened the GUI |
| Dispenser places a backpack (`BackpackDispenseBehavior`) | Exact (GX-5) | `feature/placed/Dispense.cpp` | In front of the dispenser, facing it (north for up / down); nothing happens (the backpack stays) when it can't be placed |
| `BackpackBlockEntity`: placed ticks, hopper/automation I/O, `noConnectionBlocks`, loot on first access | Exact | `feature/placed`, `feature/admin/Connections.cpp` | |
| `BackpackStorage` drops empty, never-accessed storages on load | Exact (GX-5) | `admin::dropEmptyStorages` | Once per world load (first tick), count logged. No access-log record and no items, upgrades, colours or settings; unopened mob backpacks waiting for loot carry a marker and are kept. Placed backpacks keep their storage. `removeNonPlayer` is exact |
| `BackpackAccessLogger`, `/sbp list/give/removeNonPlayer` | Exact | `feature/admin` | Same texts and arguments; names and uuids autocomplete from the access log |
| Clickable / hoverable `/sbp list` lines | PLATFORM-LIMIT | `feature/admin/SbpCommands.cpp` | Bedrock chat has no click or hover events |
| Backpack templates (`/sbp template ...`) | Exact | `feature/templates` | GX-3: settings and backpack templates are exported as SNBT (`.snbt`, like SB); older `.json` exports still load. Leftover messages take SB's two arguments (`%d x %s`, GX-5) |
| Opening another player's worn backpack | Exact | `feature/access` | |
| Mob backpacks (spawn chances, weights, loot, potions, armour, jukebox, drops) | Exact | `feature/wearable/MobBackpacks.cpp` | Mob jukebox disc count follows `advancedJukeboxUpgrade` size (GX-4). Converted mobs keep the backpack and health bonus, creepers lose their buffs, the jukebox follows the mob (GX-2) |
| Too-many-backpacks slowness, `onlyWornBackpackTriggersUpgrades` | Exact | `feature/nerf` | |
| Linked storage (Ender Linker groups, primary/secondary endpoints) | Exact | `feature/linked` | Ender Linker crafting and Mob Catcher on linked endpoints (GX-2, selftest `mobcatcher.linked_endpoints`) |
| Automatic insert order: same-item stacks, then empty memory slots, then empty slots | Exact (GX-4) | `core/BackpackInventory.cpp` | Before: memory slots first |
| `LegacyBackpackDataMigration`, shape reload listener | N/A | | Java-only internals (no legacy SB data or VoxelShape reloads on Bedrock) |

## Upgrades

| Upgrade (SB/SC classes) | Status | Where | Notes |
|---|---|---|---|
| Pickup / Advanced Pickup | Exact | `upgrade/pickup` | |
| Pickup upgrade awards the "item picked up" statistic | PLATFORM-LIMIT | `upgrade/UpgradeManager.cpp` | Bedrock has no player statistics (`Stats.ITEM_PICKED_UP`) |
| Filter / Advanced Filter (direction, contents filter) | Exact | `upgrade/filter` | |
| Several Filter upgrades combine as SC `FilteredItemHandler` (OR of allow lists, else AND) | Exact (GX-4) | `FilterUpgrade.cpp` | Before: always AND |
| Filter logic (item/mod/tags, durability, NBT, any/all tags, primary match) | Exact | `core/Filter.*`, `upgrade/filter/FilterCommon.cpp` | A tag without a namespace is `minecraft:` (ResourceLocation), so `logs` and `minecraft:logs` are one tag (GX-5) |
| Tag list order (TreeSet<ResourceLocation>: path, then namespace) | Exact (GX-4) | `core::tagLess` | Before: plain string order |
| Magnet / Advanced Magnet (range 3/5, cooldowns, XP into tanks) | Exact | `upgrade/magnet` | |
| Feeding / Advanced Feeding | Exact | `upgrade/feeding` | |
| Compacting / Advanced Compacting | Exact | `upgrade/compacting` | Recipe leftovers are kept (GX-2, selftest `compacting.recipe_remainders`) |
| Void / Advanced Void | Exact | `upgrade/void` | |
| Restock / Deposit (+ advanced) | Exact | `upgrade/restock`, `upgrade/deposit` | With Inception they use the combined inventories (GX-2) |
| Refill / Advanced Refill (6/12 slots, targets, pick block) | Exact | `upgrade/refill` | Slot counts configurable in `config/refill.json` (GX-4) |
| Stack upgrades (starter..omega, downgrades, conversions) | Exact | `upgrade/stack` | |
| Smelting / Smoking / Blasting and Auto versions | Exact | `upgrade/cooking` | Flame / smoke particles and the furnace crackle (10% per render call) on worn and placed backpacks; placed ones at Java's animateTick rate (GX-1) |
| Crafting | Exact | `upgrade/crafting` | Several per backpack, each with its own grid, results and open-tab state (GX-5) |
| Stonecutter (SC `blockconverter` base) | Exact | `upgrade/stonecutter` | |
| Jukebox / Advanced Jukebox | Exact | `upgrade/jukebox` | |
| Tank (capacity, I/O rates, container slots, cooldown) | Exact | `upgrade/tank` | Tank bars use the game's animated still textures (water_still_grey tinted 0x3F76E4, lava_still, milk, potions tinted by potion colour; frame-blended flipbooks) (GX-1). SB's red error overlay on tank / battery bars when a Stack Upgrade removal / swap / downgrade check fails (GX-1). Model tanks: the real animated sprites (water_still, lava_still, grey water for milk / potions) |
| Per-potion tint of the fluid in the 3D model's tanks | PLATFORM-LIMIT | `client/render` | Attachables get their shader constants once per attachable (`ActorShaderManager::setEntityConstants`; render-controller `color` / `overlay_color` / `uv_anim` never reach it) and those constants already carry the cloth / border colours; potions show untinted grey water on the model (the GUI tank bars are tinted) |
| Battery (FE storage, I/O) | Exact | `upgrade/battery` | Energy API: `docs/API.md` |
| Pump / Advanced Pump | Exact | `upgrade/pump` | |
| XP Pump (levels, mending) | Exact | `upgrade/xppump` | |
| Alchemy / Advanced Alchemy | Exact | `upgrade/alchemy` | Several filters matching in one check, splash potions with SC's fall-off (GX-2) |
| Infinity / Survival Infinity | Exact | `upgrade/infinity` | Item tooltips added (GX-4) |
| Everlasting, Inception, Tool Swapper "already in another upgrade slot" messages | Exact (GX-4) | `UpgradeInfo::existsKey` | Before: the generic "Only 1x ..." text |
| Inception (nesting, both config flags) | Exact | `upgrade/inception` | Combined inventories for every upgrade that uses them (GX-2, selftest `inception.combined_inventory_users`) |
| Tool Swapper / Advanced Tool Swapper (click/attack swaps, key, modes) | Exact | `upgrade/toolswapper` | The swap key now works in creative, as in SB (GX-4) |
| Advanced Tool Swapper filter with TAGS mode (SC FilterLogicControl.Advanced) | Exact (GX-4) | `client/ui/tabs/toolswapper`, `toolswapper/Upgrades.cpp` | Slot count from `config/toolswapper.json` `toolSwapperUpgrade` |
| Anvil, Smithing | Exact | `upgrade/anvil`, `upgrade/smithing` | |
| Mob Catcher / Advanced Mob Catcher | Exact | `upgrade/mobcatcher` | Config list defaults are empty, as in SB (GX-4). Captured mobs keep their actor properties (climate variants, ...) in the preview, and SB's 5-tick shrink-into-the-backpack capture animation plays before the poof / pop (GX-1) |
| Upgrade limits per backpack: SB only limits through `maxUpgradesPerStorage` (stack 3, furnace 1, jukebox 1) and its conflict rules | Exact (GX-5) | `admin::maxUpgradesHit`, `NativeScreens.cpp` `installError` | Upgrades SB doesn't limit have `kUnlimitedPerBackpack`; the three groups follow the config (item entries before group entries, `\|0` = not allowed), and a refusal highlights the upgrades of that item / group (SC `checkUpgradePerStorageTypeLimit`). `StackConfig::maxPerBackpack` is gone |
| Chipped / Sawmill compat upgrades | N/A | | Chipped and Sawmill are Java-only mods |

## Configuration

| SB/SC key | Status | Notes |
|---|---|---|
| `sophisticatedbackpacks-server.json`: disallowed items, container items, no-interaction/no-connection blocks, slot counts, entity additions, nerfs, `maxUpgradesPerStorage`, fluid handler, other-player opening, item display, dedup, `canBePlacedInContainerItems` | Exact | `allowOpeningOtherPlayerBackpacks` and `itemDisplayDisabled` were split with `gui.json`: both files now count (off in either = off) (GX-4) |
| `sophisticatedbackpacks-common.json` `chestLootEnabled`, `sophisticatedcore-common.json` `enabledItems` | Exact | |
| Per-upgrade sections (filter slots, ranges, multipliers, tank/battery/pump/xp pump, cooking, compacting, inception, mob catcher) | Exact | Refill and Tool Swapper sections added (GX-4) |
| Jukebox `advancedJukeboxUpgrade.numberOfSlots/slotsInRow`, Alchemy `alchemyUpgrade.filterSlots` / `advancedAlchemyUpgrade.filterSlots` | Exact (GX-5) | SB's section names (`config/jukebox.json` v3, `config/alchemy.json` v2); the old flat keys migrate |
| SC client `sortButtonsPosition`, `playButtonSound` | Exact | `client/ui/screen/ScreenConfig.*` |
| Unopened backpack lore uses the configured slot counts | Exact (GX-4) | `TierLore.cpp` |

## Client GUI, input and rendering

| Feature (SB/SC class) | Status | Where | Notes |
|---|---|---|---|
| StorageScreenBase layout, upgrade column, switches, sort/transfer buttons, search box, scroll panel, counts, memory/no-sort overlays, error overlay | Exact | `client/ui/screen/BackpackScreen.cpp`, `core/screen` | GX-3: the carried item is drawn below the sort buttons, upgrade switches and the error box (SC render order) |
| Closed tabs drawn unless fully covered; tooltip only when the open tab doesn't cover its top | Exact (GX-4) | `BackpackScreen.cpp` | The two checks were swapped |
| Storage settings tab opens on any mouse button | Exact (GX-4) | `BackpackScreen.cpp` | |
| XP Pump action buttons work with any mouse button | Exact (GX-4) | `tabs/xppump/XpPumpTab.cpp` | |
| Empty upgrade slot icon | Exact (GX-3) | `SbStyle.cpp`, `BackpackScreen.cpp` | Original outline art, opaque, in the slot's 16x16 item area (SC upgrade slot noItemIcon) |
| Switching an upgrade on/off closes the open tab (SC RELOAD_NEEDED) | Exact (GX-3) | `BackpackScreen::rebuildTabs`, `NativeScreens.cpp` | The enabled flags are part of the tab signature; the server drops the open tab id |
| Enter in text boxes keeps focus | Exact (GX-3) | `widgets/TextBox.cpp` | |
| Linked-storage role icon before the title | Exact (GX-3) | `BackpackScreen.cpp`, `NativeScreens.cpp`, `SbStyle.cpp` | The server sends the role (`mainscreen.linkedRole`); 16x16 icon (original art) at (6, 2), the title at x + 14, "Main Linked" / "Linked" tooltip |
| Truncated open-tab title tooltip | Exact (GX-3) | `BackpackScreen.cpp` | |
| Clicking a tab border doesn't throw the cursor item | Exact (GX-3) | `BackpackScreen::onMouseButton` | SC hasClickedOutside: upgrade slots rectangle and tab rectangles |
| Middle-click sort anywhere but the player inventory, before other handlers | Exact (GX-3) | `BackpackScreen::onMouseButton` | |
| Drag distribution over upgrade slots | Exact (GX-3) | `BackpackScreen::canDragTo`, `NativeScreens.cpp` | One upgrade per empty slot |
| Q / 1-9 over upgrade slots; bound (not literal) keys | Exact (GX-3) | `BackpackScreen::onKey`, `NativeScreens.cpp`, `input/Hotkeys.cpp` | Inventory / drop / hotbar keys are read from the keyboard layout (`key.inventory`, `key.drop`, `key.hotbar.N`); drop and hotbar swap work on upgrade slots with SC's remove / install checks |
| Jukebox playtime overlay updates smoothly | Exact (GX-3) | `tabs/jukebox` | The client advances the remaining time every frame between the server's whole-second updates (SC computes it from the game time) |
| Crafting "Previous" tooltip on the left | Exact (GX-3) | `tabs/crafting`, `widgets/Tooltip.*` | SC LEFT_SIDE_TOOLTIP_POSITIONER |
| LargeResultSelector, anvil name box, smithing help tooltips, stonecutter hit box details | Exact (GX-3) | `tabs/stonecutter`, `upgrade/anvil/AnvilTab.cpp`, `upgrade/smithing` | Scrollbar moves on drag only, centred "Search" hint (0xBBBBBB); anvil box padding 1 / 86 wide, right click doesn't clear; onboarding tooltips with an item carried; recipe tooltip hit box from x (clicks from x + 1). Alchemy disabled condition button: no background already (SC DISABLED_CONDITION blit only); left as audited |
| Settings screens (main, memory, no sort, item display, templates) | Exact | `client/ui/settings`, `feature/gui` | Item Display preview draws the real 3D model (display entity with the backpack attachable and its display item) with SB's rotation and drag (GX-1). GX-3: the open tab id is kept on the storage (server side, removed when Keep Tab Open is off, like SC removeOpenTabIfKeepOff); Match-NBT memory slots take only the exact stack when sorting, in filter-by-storage and in the stash overlay |
| Key bindings (names, defaults, categories, open/inventory interaction/tool swap/toggles, sort, transfer) | Exact | `client/input` | |
| Right-click stashing | Exact | `client/input/Stash.cpp`, `feature/stash` | GX-3: any vanilla container slot (crafting grids, furnace / blast furnace / smoker incl. result with XP, brewing stand, stations, crafter, horse); main inventory slot indices fixed |
| Backpack tooltip and Shift contents tooltip | Exact | `client/tooltip` | GX-3: long upgrade tooltip lines wrap by NeoForge's rule (only when the tooltip fits on neither side; default font widths) |
| Worn backpack model, upgrade particles, item colours | Exact | `client/render` | Dropped backpacks and item frames show the 3D model with SB's ground / fixed transforms; vanilla slots (inventory, hotbar, containers) draw the two-colour layered icon; the smithing armour stand has no base plate (GX-1) |
| Sort / transfer keys play the button click (`playButtonSound`) | Exact (GX-3) | `BackpackScreen.cpp` | |
| Crafting / smithing output preview: dyed colours, the source backpack (contents tooltip) | Exact (GX-3) | `feature/tiers/CraftPreview.cpp` | |
| Clicking a backpack back into its slot places it (not equipped) | Exact (GX-3) | `client/input/Stash.cpp`, `Hotkeys.cpp`, `SlotHover.cpp` | The main inventory grid's indices start at 0 (= slot 9); they collided with the hotbar |
| Diamond backpack durability workaround | Exact (GX-3) | `feature/tiers/SmithingBase.cpp` | Removed: no durability |

## Strings

All 548 SB/SC strings in scope match SB's English (Java config-screen and compat strings are N/A).
GX-4 added the Infinity / Survival Infinity tooltips and the three "already in another upgrade slot"
messages. Messages now use SB's "Backpack" for the leather tier. zh_CN (GX-5): every string that has
an SB / SC `zh_cn.json` equivalent uses SB's exact text (342 strings in the mod lang files and the
resource pack `texts.d`, e.g. 压制升级, 铜制背包, 空白升级); strings SB doesn't have keep our own
translation.
