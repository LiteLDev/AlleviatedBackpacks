# AlleviatedBackpacks

Backpacks with upgrades for **Minecraft Bedrock Edition**, as a [LeviLamina](https://github.com/LiteLDev/LeviLamina)
mod. It brings the gameplay of the Java mod *Sophisticated Backpacks* to Bedrock and works on
dedicated servers (BDS) and in single-player / local worlds on the client.

- Minecraft **1.26.51**, LeviLamina **26.51.7**, Windows x64.
- Players need the **client mod** (the `client` build, [download](https://github.com/LiteLDev/AlleviatedBackpacks/releases)):
  it draws Sophisticated Backpacks' own screens (storage with scroll bar and search, upgrade column
  with switches, upgrade tabs, settings screen), adds the key bindings and the Shift contents
  tooltip. Every item move is still done by the server. A player without the client mod can join
  and carry backpacks (upgrades keep working), but opening a backpack screen shows
  "AlleviatedBackpacks needs its client mod" with the download link.

## Features

- **Six backpack tiers** (backpacks never stack):

  | Tier | Storage slots | Upgrade slots |
  | ---- | ------------- | ------------- |
  | Leather | 27 | 1 |
  | Copper | 45 | 1 |
  | Iron | 54 | 2 |
  | Gold | 81 | 3 |
  | Diamond | 108 | 5 |
  | Netherite | 120 | 7 |

- **Right-click a backpack (or press B) to open it**: Sophisticated Backpacks' screen — 9 or 12
  columns, a scroll bar when the rows don't fit, the upgrade column with on/off switches, upgrade
  tabs on the right, the search box (`@mod`, `#tooltip`, name), sort / sort-by and transfer buttons,
  abbreviated counts for large stacks, memory and no-sort slots. The settings tab opens the
  backpack settings screen (memory, no-sort, context and main options).
- **Key bindings** (Settings > Keyboard & Mouse): Open Backpack `B` (also over a backpack in any
  inventory, and over a nested backpack), Inventory Interaction `C` (Deposit / Restock on the
  container you look at), Tool Swap (unbound), Toggle Upgrade 1/2 `Alt+Z` / `Alt+X` (3-5 unbound),
  Sort (middle mouse), Transfer to Storage / Inventory `[` / `]` (Shift: everything).
- **Shift over a backpack item** shows its contents: stack multiplier, tanks, upgrades with their
  on/off state and the items, most first.
- **Contents belong to the backpack, not to the player**: drop it, give it away, put it in a chest —
  the items stay inside. Data is stored in the world itself, so it moves with the world folder.
- **Upgrades** go into the backpack's upgrade slots and can be switched on and off individually;
  many have their own settings page with item filters (allow/deny list, match by item, namespace or
  tag, NBT and durability options). The upgrade set follows Sophisticated Backpacks (pickup,
  filter, magnet, feeding, compacting, void, restock/deposit, refill, stack, crafting, smelting,
  inception, everlasting, jukebox, tool swapper, tank, pump, xp pump, alchemy, anvil, ...).
  - The **Battery upgrade** has no Bedrock equivalent (there is no Forge Energy) and is not provided.
- Backpacks can't be put into themselves, and by default not into other backpacks
  (an Inception upgrade allows it). Like Sophisticated Backpacks, shulker boxes fit in backpacks
  (`containerItemsDisallowed` refuses them) and backpacks don't fit in shulker boxes
  (`canBePlacedInContainerItems`).
- World systems as in Sophisticated Backpacks: the **Ender Linker** links backpacks into a linked
  storage group with shared contents; duplicated backpacks (same storage) are sorted out; right-click
  another player's back to open their worn backpack; mobs sometimes spawn wearing filled backpacks;
  dungeon and other loot chests sometimes hold backpacks and upgrades.
- English and Simplified Chinese; the GUI follows each player's language.

Commands (alias `/abp`):

| Command | Who | What |
| ------- | --- | ---- |
| `/backpack open` | everyone | open the backpack in your main hand |
| `/backpack info` | everyone | uuid, tier and upgrades of the held backpack |
| `/backpack give <tier>` | operators | get a backpack |
| `/backpack reload` | operators / console | reload config files and translations |
| `/backpack selftest [pattern]` | operators / console | run the built-in self tests |
| `/sbp list [playerName]` | operators / console | opened backpacks, newest first (alias `/sophisticatedbackpacks`) |
| `/sbp give <targets> <backpackUuid>` | operators / console | give a backpack bound to a logged backpack |
| `/sbp removeNonPlayer <onlyWithEmptyInventory>` | operators / console | delete backpacks no player ever opened |
| `/backpack info <uuid>`, `stats`, `remove <uuid>`, `cleanup`, `openfor <uuid>` | operators / console | admin extras |

## Install

### Dedicated server (BDS + LeviLamina)

1. Install LeviLamina 26.51.7 for the server (`lip install github.com/LiteLDev/LeviLamina@26.51.7`).
2. Download the `server` build (or build it, see below) and copy the folder
   `AlleviatedBackpacks/` into the server's `plugins/` folder.
3. Start the server. On first start the mod adds its resource pack to
   `worlds/<level-name>/world_resource_packs.json` and asks clients to download it. The behavior pack
   (items and recipes) is loaded automatically.

### Client (LeviLauncher) — required to use backpacks, also for single player

1. Install the 1.26.51 game version with LeviLamina in LeviLauncher.
2. Copy the `client` build folder `AlleviatedBackpacks/` into
   `%APPDATA%\levilauncher.exe\versions\<version>\mods\`.
3. Start the game; every local world gets the backpacks. No world settings need changing.

### Configuration

`AlleviatedBackpacks/config/general.json` (created on first start):

| Key | Default | Meaning |
| --- | ------- | ------- |
| `language` | `en_US` | default language for messages |
| `usePlayerLocale` | `true` | use each player's game language when available |
| `upgradeTickInterval` | `10` | ticks between upgrade updates (20 = 1 second) |
| `requireResourcePack` | `true` | BDS: players must accept the resource pack |
| `allowNesting` | `false` | allow backpacks in backpacks without an Inception upgrade |
| `allowShulkerBoxes` | `true` | allow shulker boxes in backpacks |

Sophisticated Backpacks' own settings, with SB's names, defaults and ranges:
`config/sophisticatedbackpacks-server.json` (disallowed items, slot counts per tier, entity backpack
additions, nerfs, max upgrades per storage, other-player opening, dedup, ...),
`config/sophisticatedbackpacks-common.json` (`chestLootEnabled`) and
`config/sophisticatedcore-common.json` (`enabledItems`). Upgrades add their own
`config/<upgrade>.json` files; `config/admin.json` and `config/wearable.json` hold
AlleviatedBackpacks extras that are off by default.

## Building

Requirements: Windows x64, [xmake](https://xmake.io), Visual Studio 2022+ build tools, LLVM (clang-cl),
git with `core.longpaths` enabled.

```powershell
pwsh tools/build.ps1 -Target all -Tests
```

Outputs: `bin/server/AlleviatedBackpacks/` and `bin/client/AlleviatedBackpacks/`.

## Credits

Inspired by Sophisticated Backpacks by P3pp3rF1y. This is an independent implementation for
Bedrock; no code or assets are taken from the original mod.
