# Server operator reference

Everything a server operator (or a single-player world with cheats) can use. Players only need the
[README](../README.md).

## Commands

`/backpack` has the alias `/abp`; `/sbp` has the alias `/sophisticatedbackpacks`.

| Command | Who | What |
| ------- | --- | ---- |
| `/backpack open` | everyone | open the backpack in your main hand |
| `/backpack info` | everyone | uuid, tier and upgrades of the held backpack |
| `/backpack give <tier>` | operators | get a backpack |
| `/backpack reload` | operators / console | reload config files and translations |
| `/backpack info <uuid>`, `stats`, `remove <uuid>`, `cleanup`, `openfor <uuid>` | operators / console | inspect, delete and open stored backpacks |
| `/backpack selftest [pattern]` | operators / console | run the built-in self tests |
| `/sbp list [playerName]` | operators / console | opened backpacks, newest first |
| `/sbp give <targets> <backpackUuid>` | operators / console | give a backpack bound to a logged backpack |
| `/sbp removeNonPlayer <onlyWithEmptyInventory>` | operators / console | delete backpacks no player ever opened |
| `/sbp template list / create / delete / give / export` | operators / console | Sophisticated Backpacks' backpack templates |
| `/sbp template dynamic begin / additem / addupgrade / end` | operators / console | build a template backpack item by item |

## Configuration

The files live in `plugins/AlleviatedBackpacks/config/` on a server and in
`mods/AlleviatedBackpacks/config/` in the game. They are created on first start;
`/backpack reload` applies changes.

`general.json`:

| Key | Default | Meaning |
| --- | ------- | ------- |
| `language` | `en_US` | default language for messages |
| `usePlayerLocale` | `true` | use each player's game language when available |
| `upgradeTickInterval` | `10` | ticks between upgrade updates (20 = 1 second) |
| `requireResourcePack` | `true` | BDS: players must accept the resource pack |
| `allowNesting` | `false` | allow backpacks in backpacks without an Inception upgrade |
| `allowShulkerBoxes` | `true` | allow shulker boxes in backpacks |

Sophisticated Backpacks' own settings, with SB's names, defaults and ranges:
`sophisticatedbackpacks-server.json` (disallowed items, slot counts per tier, entity backpack
additions, nerfs, max upgrades per storage, other-player opening, dedup, ...),
`sophisticatedbackpacks-common.json` (`chestLootEnabled`) and `sophisticatedcore-common.json`
(`enabledItems`). Upgrades add their own `<upgrade>.json` files with SB's sections for that upgrade
(for example `refill.json`: `refillUpgrade.filterSlots`). `admin.json` and `wearable.json` hold
AlleviatedBackpacks extras that are off by default. `client_gui.json` (game only) holds SB's client
options `sortButtonsPosition` and `playButtonSound`.

## Rules worth knowing

- Backpacks can't be put into themselves, and by default not into other backpacks (an Inception
  upgrade allows it). Shulker boxes fit in backpacks (`containerItemsDisallowed` refuses them);
  backpacks don't fit in shulker boxes or bundles (`canBePlacedInContainerItems`).
- Backpack contents are stored in the world itself, so they move with the world folder.
- On a dedicated server the mod adds its resource pack to
  `worlds/<level-name>/world_resource_packs.json` on first start; the behavior pack is loaded
  automatically.
