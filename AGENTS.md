# AGENTS.md

AlleviatedBackpacks is a Bedrock port of the *behaviour* of the Java mod Sophisticated Backpacks,
written as a LeviLamina native mod. It targets MC **1.26.51** / LeviLamina **26.51.7**, builds
with xmake + clang-cl on Windows x64, and runs on **both** the dedicated server (BDS) and the
client (local worlds through LeviLauncher). One code base, two builds (`--target_type=server|client`).

Policy: never copy code or assets from the Java Sophisticated Backpacks repository (behaviour
reference only). Textures are original pixel art drawn by `tools/textures/*.ps1`.

## Layout

| Path | What it is |
| ---- | ---------- |
| `src/core/` | Pure C++ (no `mc/`/`ll/` includes): `Tier`, `ItemStackData`, `BackpackInventory`, `Filter`, `BackpackData`, `Codec`, `Format`. Static lib `abp-core`, unit-tested in `tests/`. |
| `src/game/` | Game adapters: `ItemConv` (ItemStack <-> core), `BackpackItem` (item ids, uuid NBT), `Backpack` + `Storage` (per-world persistence), `PlayerScan`, `PlayerTicker`. |
| `src/upgrade/` | `IUpgrade.h` (upgrade contract + registry), `UpgradeManager.h` (install/remove/toggle/pickup dispatch). |
| `src/ui/menu/` | Fake-chest GUI framework: `Slot.h`, `Menu.h`, `MenuManager.h`, `BackpackMenu.h` (main GUI + button/setting registries), `FilterMenu.h`. |
| `src/selftest/` | `SelfTest.h` (in-game test framework) + `FoundationTests.cpp`. |
| `src/pack/` | `PackDeployer` (BDS: puts the resource pack on the world). |
| `src/mod/` | Entry (`Main.cpp`), `Log.h`, `Module.h`, `Config.h`, `GeneralConfig.h`, `I18n.h`, `Commands.h`, `Registry.h`. |
| `assets/lang/<locale>/<unit>.json` | Mod (server-side) translations, merged into `lang/<locale>.json`. |
| `assets/pack/bp/` | Behavior pack: `items/*.json`, `recipes/*.json`, `manifest.json`. |
| `assets/pack/rp/` | Resource pack: `textures/items/*.png`, fragments `item_texture.d/*.json`, `terrain_texture.d/*.json`, `texts.d/<locale>/*.lang`, `sounds.d/*.json` (`sound_definitions` entries). |
| `tools/build.ps1` | Builds server and/or client into `bin/<target>/AlleviatedBackpacks/`. |
| `tools/xmake/assemble.lua` | Post-build: copies binaries, merges lang + pack fragments. |
| `tools/textures/` | `lib.ps1` (pixel-art helpers), `backpacks.ps1`, `upgrade_base.ps1`. |
| `tools/e2e/` | Real-game harnesses (server + client); see `tools/e2e/README.md`. |

## Work as a feature unit (no shared-file edits)

Each feature unit owns its own files and registers itself. **Do not edit** `src/mod/Main.cpp`,
existing contracts or other units' files. Extend contracts compatibly (add, never change/remove)
and only when your unit really needs it.

Files a unit typically adds (`<unit>` = e.g. `pickup`):
- `src/upgrade/<unit>/*.cpp|h` (or `src/feature/<unit>/`), namespace `abp::upgrade::<unit>` / `abp::<dir>`.
- `assets/lang/en_US/<unit>.json` + `assets/lang/zh_CN/<unit>.json` (both required).
- `assets/pack/bp/items/<unit>_upgrade.json`, `assets/pack/bp/recipes/<unit>_upgrade.json`.
- `assets/pack/rp/textures/items/<unit>_upgrade.png` (drawn by `tools/textures/<unit>.ps1`, use `lib.ps1`).
- `assets/pack/rp/item_texture.d/<unit>.json`, `assets/pack/rp/texts.d/{en_US,zh_CN}/<unit>.lang`.
- Selftests in the unit's .cpp (`ABP_REGISTER_SELFTEST("<unit>.<case>", ...)`), e2e scenarios in
  `tools/e2e/{server,client}/scenarios/<unit>.ps1`.

Duplicate keys in any merged fragment (lang JSON, item_texture, terrain_texture, texts, sounds) fail the build.

### Registration macros

| Macro | Header | Purpose |
| ----- | ------ | ------- |
| `ABP_REGISTER_MODULE(T)` / `ABP_REGISTER_MODULE_ORDERED(T, order)` | `mod/Module.h` | `T` has `install()`/`uninstall()` (+ optional `load()` at mod load, + optional `static T& get()`). Installed on enable in `order`, uninstalled in reverse. |
| `ABP_REGISTER_UPGRADE(T)` | `upgrade/IUpgrade.h` | `T : IUpgrade`, default-constructible, stateless singleton. |
| `ABP_REGISTER_CONFIG("section", T)` | `mod/Config.h` | Reflectable struct with `int version`; file `config/<section>.json`; read with `abp::config::get<T>()`; `/backpack reload` reloads. |
| `ABP_REGISTER_SELFTEST("unit.case", fn)` | `selftest/SelfTest.h` | `fn(Context&)` or `ll::coro::CoroTask<>(Context&)`. |
| `ABP_REGISTER_BACKPACK_BUTTON(BackpackButton{...})` | `ui/menu/BackpackMenu.h` | Button in the storage page control row (max 4 shown). |
| `ABP_REGISTER_BACKPACK_SETTING(BackpackSetting{...})` | `ui/menu/BackpackMenu.h` | Entry in the backpack settings form. |
| `ABP_REGISTER_SUBCOMMAND(fn)` | `mod/Commands.h` | Adds overloads to `/backpack` (alias `/abp`). |

Registrations live in .cpp files linked directly into the DLL, so they are never stripped.

## Contracts in short

- **Items**: backpacks are `alleviatedbackpacks:<tier>_backpack` (tiers in `core/Tier.h`:
  leather 27/1, copper 45/1, iron 54/2, gold 81/3, diamond 108/5, netherite 120/7 storage/upgrade
  slots). The uuid lives in the item's user data (`abp_uuid`), assigned on first open
  (`Storage::forItem`). Upgrade items are `alleviatedbackpacks:<id>_upgrade` (by convention), crafted
  from `alleviatedbackpacks:upgrade_base`.
- **Storage** (`game/Storage.h`): `find(uuid)`, `create(tier)`, `forItem(stack, &changed)`,
  `save`, `erase`. Returns `std::shared_ptr<game::Backpack>`; keep shared_ptrs, never raw pointers.
  Data is JSON (`core::BackpackData`, versioned via `kVersion`) in the world's LevelDB.
- **Backpack** (`game/Backpack.h`): `inventory()` (core `BackpackInventory`), `data()`
  (settings JSON, upgrade slots), `markDirty()`, `revision()`, `rebuildHooks()`, capabilities
  (`addCapability("allow_nesting")`), `transient<T>(key)`.
- **Inventory hooks** (`core/BackpackInventory.h`): insert filters return Allow/Deny/Void/VoidOverflow
  (first non-Allow in priority order wins; `slot == -1` is the whole-stack check before automatic
  placement), extract filters, limit modifiers (stack upgrades), change listeners. Every insert
  carries an `AccessContext{Source::Player|Pickup|Automation|Upgrade|Internal, user}`.
  Register hooks **only** in `IUpgrade::attach()` with `ctx.hookOwner()`.
- **Upgrades** (`upgrade/IUpgrade.h`): `info()` (id, itemId, nameKey, tier, group, maxPerBackpack,
  conflicts, hasSettings, toggleable), `canInstall/onInstall/canRemove/onRemove`, `attach`,
  `onTick(TickContext)` (every `general.upgradeTickInterval` ticks while carried), `onPickup(ctx,
  item)` (reduce `item.count` by what you took), `openSettings`/`createSettingsMenu`, `describe`.
  Per-instance settings: `ctx.settings()` + `ctx.saveSettings(rebuildHooks)`. Settings travel with
  the upgrade item when it is removed.
- **Menus** (`ui/menu/*.h`): subclass `Menu`, fill slots in `build()`; slot types `StorageSlot`,
  `GhostSlot`, `ButtonSlot`, `StaticSlot`, `CallbackSlot`; `ui::icon(id, name, lore)`.
  `MenuManager::get().open/close/closeThen/reopen`. Use `closeThen` before showing a form.
  `FilterMenu::forUpgrade(player, ctx, "filter", 9)` + `core::Filter::fromJson(ctx.settings()["filter"], 9)`.
- **Strings**: `abp::tr("abp.key", "English fallback", args...)` / `abp::trFor(player, ...)` with
  `{}`/`{0}` placeholders. Keys start with `abp.<unit>.`. Item names come from RP `texts.d`.
- **Logging**: `abp::log()` (`mod/Log.h`). No per-tick logging.
- **Selftests** (`selftest/SelfTest.h`): `ctx.player()` (SimulatedPlayer), `ctx.createBackpack(tier)`,
  `ctx.backpackItem(bp)`, `ctx.give(player, item, slot)`, `ctx.installUpgrade(bp, "id")`,
  `co_await ctx.ticks(n)`, `ctx.check/require/fail/info`. Menus can be driven without a client with
  `MenuManager::simulate(player, {ActionType, src, dst, amount})` + `forceShown` + `shownItem`.

## Build, test, run

In a fresh pwsh refresh PATH first (the build script does this and adds LLVM if needed):
`$env:Path=[Environment]::GetEnvironmentVariable('Path','Machine')+';'+[Environment]::GetEnvironmentVariable('Path','User')`

```powershell
pwsh tools/build.ps1 -Target all            # bin/server/AlleviatedBackpacks + bin/client/AlleviatedBackpacks
pwsh tools/build.ps1 -Target server -Tests  # one platform + unit tests
xmake build -y abp-tests; xmake run abp-tests
```

- `git config --global core.longpaths true` is required (long LeviLamina header paths).
- `clang-cl` must be on PATH for xmake's package builds (`C:\Program Files\LLVM\bin`). LeviLamina is
  built from source (no prebuilt package), so whichever `clang-cl` comes first on PATH compiles it.
  `tools/build.ps1` appends the registry PATH after the caller's PATH and prefers `$env:LLVM_PATH`, so
  CI uses setup-llvm's LLVM 22. The runner's preinstalled `C:\Program Files\LLVM` fails on LL's headers.
- CI: a failed package install prints `installdir.failed\logs\install.txt` in the "Dump xmake package
  install logs" step. The `download ... rapidjson ... failed` line is harmless (xmake falls back to git clone).
- `xmake-requires.lock` is committed; it holds entries for both target types.
- In game: `/backpack selftest ["glob"]` (alias `/abp`; op or server console). Quote globs that
  contain `*` or `.`, e.g. `backpack selftest "menu.*"`; the parser rejects them unquoted. Output lines are
  exactly `SELFTEST PASS <name>`, `SELFTEST FAIL <name>: <reason>`, `SELFTEST DONE pass=<N> fail=<M>`.
- Log contract: `AlleviatedBackpacks enabled` on enable; `AlleviatedBackpacks world ready` when a
  world is available (BDS: at enable; client: every local world load).
- E2E harnesses for BDS and the client: see `tools/e2e/README.md`. Scenarios per unit live in
  `tools/e2e/{server,client}/scenarios/<unit>.ps1`. There is one game client per machine: respect the
  e2e lock files in `%APPDATA%\levilauncher.exe\` (the harness handles them).

Deploying by hand: copy `bin/server/AlleviatedBackpacks` to `<BDS>/plugins/` (LeviLamina's BDS mod
folder is `plugins`), or `bin/client/AlleviatedBackpacks` to
`%APPDATA%\levilauncher.exe\versions\1.26.51.01\mods\`.

## Bedrock / LeviLamina rules (learned the hard way)

- Headers: `%LOCALAPPDATA%\.xmake\packages\l\levilamina\26.51.7\<hash>\include\{ll,mc}` (one hash per
  target type); LL sources in `%LOCALAPPDATA%\lip\cache\git%2Bhttps%3A%2F%2Fgithub.com%2FLiteLDev%2FLeviLamina.git%23v26.51.7`.
  Check them before guessing a name or signature.
- Callable: `MCAPI`, `MCFOLD`, `MCTAPI`, unmarked `virtual`. **Never** `MCNAPI`. `ll::TypedStorage`
  members are fields (`.get()`, `*`, `->`). Packets without an MCAPI constructor are created with
  `MinecraftPackets::createPacket(MinecraftPacketIds::X)` + `static_cast`; send with `pkt.sendTo(player)`.
- Hook virtuals via `&Cls::$fn` with `LL_TYPE_INSTANCE_HOOK`; `hook()` in `install()`, `unhook()` in `uninstall()`.
- In `ServerNetworkHandler::$handle` hooks `this` is the `NetEventCallback` base subobject: use
  `thisFor<NetEventCallback>()->_getServerPlayer(source, packet.mSenderSubId)` (`this->` crashes).
- **No `dynamic_cast` on game types** (they have no usable RTTI: it throws `__non_rtti_object`).
  A Player that is not `isClientSide()` is a `ServerPlayer`.
- Types used as command parameters must have external linkage (not in an anonymous namespace).
- `ItemStack` has no `(name, count, aux)` constructor: use `game::ItemConv::make(id, count, aux)`.
  Counts are `uchar` (max 255); core uses `int` counts and the GUI shows bigger counts in the lore.
- Copying an `ItemStackRequestId` needs vtable stubs; `MenuManager.cpp` already provides them.
- BDS enables mods **after** "Server started" (the world already exists at `install()`); the client
  enables mods at game start (no world yet) and fires `ServerStartedEvent` for every local world.
  Write modules that work in both orders.
- `ll::service::getLevel()` on the client is the integrated server's level (null outside a world).
- Behavior packs in `mods/<name>/behavior_packs/` are added to every world by LeviLamina (both
  platforms); resource packs in `resource_packs/` are added to the client's global stack. On BDS
  `PackDeployer` adds the RP to `worlds/<level>/world_resource_packs.json` and marks it required.
- Menus are server-authoritative fake chests; never call `MenuManager::open` re-entrantly from inside
  a slot callback expecting immediate effect (it is deferred until the request finishes).
- Game threads: everything here runs on the server thread. Don't keep `Player*` across ticks;
  store `getOrCreateUniqueID()` and look the player up again.

## Fake chest GUI rules

`MenuManager` implements these; keep them if you touch it, and use `open/close/closeThen` instead
of sending container packets yourself.

- **Answer every ContainerClose.** When the client closes one of our screens it sends a
  ContainerClose and waits for one back (`serverInitiated=false`, same id and type). It does the same
  after a *server-initiated* close (menu replaced or closed by the server). Until that answer comes,
  the client opens **no** other screen: no chest, and no chat either ("chat broken until rejoin").
- **Switching menus = close, wait, then place.** Send the server-initiated close, restore the fake
  blocks, wait for the client's answering close (at most 20 ticks), and only then send the new fake
  chest. The client sends its answering close only after its close transition, about 8-9 ticks
  (~430 ms) later. A ContainerOpen that arrives before that is dropped: the server thinks the menu
  is open, and the client shows nothing. This applies to *any* open after a server close (replace,
  `close` + `open`, `closeThen`, tabs), not only to a direct replace. `MenuManager::swap` is just
  `open`.
- **Another screen up = rejected open.** If the chat screen (or another screen) is still up, for
  example right after a chat command that opens a menu, the client rejects the ContainerOpen at once
  with a ContainerClose for id -1. Vanilla answers that close; we send the open again 10 ticks later.
- **Place, then open.** Send UpdateBlock for all chest halves first, then their BlockActorData
  (`CustomName`, and for 54 slots the `pairx/pairz/pairlead` pairing), wait about 3 ticks, then
  send ContainerOpen and the slot contents.
- **Fake chest position.** Put it 3-4 blocks above the feet (or below for the world top). That is
  well inside the client's container reach and out of the head's way when jumping. Prefer real air,
  and never reuse a position that held a fake chest in the last ~5 s: the client may still have the
  old chest's state there (a double chest's pairing).
- `config/menu.json` `"debugPackets": true` logs every fake-chest packet sent and received
  (`[menu-dbg]`). On the client build it also logs what the game client receives and its top
  screen. With it on, `/backpack menudebug closeopen` opens the carried backpack, closes it from the
  server and reopens it 4 ticks later. The client e2e scenario `menus` uses both.

## Conventions

- C++20, `.clang-format` (LLVM-based, 4 spaces, 120 columns). Namespaces follow directories
  (`abp::core`, `abp::game`, `abp::upgrade`, `abp::ui`, `abp::selftest`, ...).
- English in code and comments; user-facing text through `tr`/`trFor` + lang fragments (en_US and zh_CN).
- Every `src/core` change comes with gtest coverage in `tests/`; every feature comes with selftests.
- Commit in small, buildable steps. Both platforms must build (`pwsh tools/build.ps1 -Target all`).
