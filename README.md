# AlleviatedBackpacks

Backpacks with upgrades for **Minecraft Bedrock Edition**, as a [LeviLamina](https://github.com/LiteLDev/LeviLamina)
mod. It brings the gameplay of the Java mod *Sophisticated Backpacks* to Bedrock and works on
dedicated servers (BDS) and in single-player / local worlds on the client.

- Minecraft **1.26.51**, LeviLamina **26.51.7**, Windows x64.
- Players on a server need **no client mod**: the server sends the resource pack, and the backpack
  GUI is a regular chest screen driven by the server.

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

- **Right-click a backpack to open it.** The GUI shows 45 slots per page with page buttons, an
  upgrades page and a settings button. Large stacks (from stack upgrades) show their real count
  in the item tooltip.
- **Contents belong to the backpack, not to the player**: drop it, give it away, put it in a chest —
  the items stay inside. Data is stored in the world itself, so it moves with the world folder.
- **Upgrades** go into the backpack's upgrade slots and can be switched on and off individually;
  many have their own settings page with item filters (allow/deny list, match by item, namespace or
  tag, NBT and durability options). The upgrade set follows Sophisticated Backpacks (pickup,
  filter, magnet, feeding, compacting, void, restock/deposit, refill, stack, crafting, smelting,
  inception, everlasting, jukebox, tool swapper, tank, pump, xp pump, alchemy, anvil, ...).
  - The **Battery upgrade** has no Bedrock equivalent (there is no Forge Energy) and is not provided.
- Backpacks can't be put into themselves, and by default not into other backpacks
  (an Inception upgrade allows it). Shulker boxes are refused unless the config allows them.
- English and Simplified Chinese; the GUI follows each player's language.

Commands (alias `/abp`):

| Command | Who | What |
| ------- | --- | ---- |
| `/backpack open` | everyone | open the backpack in your main hand |
| `/backpack info` | everyone | uuid, tier and upgrades of the held backpack |
| `/backpack give <tier>` | operators | get a backpack |
| `/backpack reload` | operators / console | reload config files and translations |
| `/backpack selftest [pattern]` | operators / console | run the built-in self tests |

## Install

### Dedicated server (BDS + LeviLamina)

1. Install LeviLamina 26.51.7 for the server (`lip install github.com/LiteLDev/LeviLamina@26.51.7`).
2. Download the `server` build (or build it, see below) and copy the folder
   `AlleviatedBackpacks/` into the server's `plugins/` folder.
3. Start the server. On first start the mod adds its resource pack to
   `worlds/<level-name>/world_resource_packs.json` and asks clients to download it. The behavior pack
   (items and recipes) is loaded automatically.

### Client / single player (LeviLauncher)

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
| `allowShulkerBoxes` | `false` | allow shulker boxes in backpacks |

Upgrades add their own `config/<upgrade>.json` files.

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
