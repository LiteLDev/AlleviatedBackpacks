# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- A logo (`docs/logo.png`), shown on [LeviMC Mods](https://levimc.org/mods/) through `avatar_url` in `tooth.json`.
- Screenshots and a video for the README and the LeviMC Mods gallery (`docs/showcase/`), captured
  by the client e2e scenario `showcase`.

### Changed

- The README is written for players; building, tests and the API moved to `CONTRIBUTING.md`, and
  the full command and config reference for server operators to `docs/ADMIN.md`.

## [0.1.0] - 2026-10-24

First release, for Minecraft Bedrock Edition **1.26.51** and LeviLamina **26.51.7** (Windows x64).
There are two builds: `server` for BDS and `client` for the game. Every player needs the client
build, in single player and on a server.

### Added

- **Six backpack tiers**: Leather (27 storage / 1 upgrade slots), Copper (45/1), Iron (54/2),
  Gold (81/3), Diamond (108/5) and Netherite (120/7), with Sophisticated Backpacks' recipes, tier
  upgrades and dyeing.
  - Contents belong to the backpack, not to the player, and are stored in the world itself.
  - Backpacks can be worn, placed in the world, opened from another player's back and linked with
    the Ender Linker.
- **Sophisticated Backpacks' screens**, drawn by the client mod:
  - the storage screen with scroll bar, search (`@mod`, `#tooltip`, name), sort / sort-by,
    transfer buttons, abbreviated counts, memory and no-sort slots;
  - the upgrade column with on/off switches and an upgrade tab for every upgrade;
  - the settings screens (main, memory, no sort, item display, templates).
- **Every Sophisticated Backpacks upgrade**, basic and advanced: Pickup, Filter, Magnet, Feeding,
  Compacting, Void, Restock, Deposit, Refill, Tool Swapper, Jukebox, Pump, Alchemy, Mob Catcher;
  Stack (Starter, Tier 1-4, Omega, downgrades and conversion items); Crafting, Stonecutter, Anvil,
  Smithing; Smelting, Smoking, Blasting and their Auto versions; Inception, Everlasting, Infinity,
  Survival Infinity; Tank, Battery, XP Pump. Only the Chipped and Sawmill compat upgrades are
  missing (Java-only mods). Also the Hopper and Advanced Hopper upgrades from Sophisticated Storage.
- **Key bindings** (Settings > Keyboard & Mouse): open backpack, inventory interaction, tool swap,
  toggle upgrade 1-5, sort, transfer to storage / inventory, and right-click stashing.
- **Shift contents tooltip** on backpack items, and 3D backpack models worn, held, dropped, placed
  and in item frames.
- **World systems**: mobs spawning with backpacks, backpacks and upgrades in loot chests,
  duplicate backpack detection, piglins and the Gold Backpack, dispensers and hoppers.
- **Energy and fluid API** (FE and mB) for the Tank, Battery, Pump and XP Pump upgrades, public for
  other mods: see [docs/API.md](docs/API.md).
- **Configuration** with Sophisticated Backpacks' own config keys, names, defaults and ranges.
- **Commands**: `/backpack` (alias `/abp`) and `/sbp` (alias `/sophisticatedbackpacks`).
- English and Simplified Chinese; the GUI follows each player's language.

[Unreleased]: https://github.com/LiteLDev/AlleviatedBackpacks/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/LiteLDev/AlleviatedBackpacks/releases/tag/v0.1.0
