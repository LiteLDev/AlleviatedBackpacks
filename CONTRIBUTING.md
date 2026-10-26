# Contributing to AlleviatedBackpacks

Thanks for helping! Bug reports, ideas and pull requests are all welcome.

## Reporting bugs and requesting features

Use the [issue templates](https://github.com/LiteLDev/AlleviatedBackpacks/issues/new/choose). For bugs, include:

- the AlleviatedBackpacks version;
- the LeviLamina version;
- the Minecraft version;
- single player or server, and whether the player runs the client mod;
- steps to reproduce;
- the relevant part of `logs/latest.log` from your game or server folder.

## Development setup

See [Development](#development) below. [AGENTS.md](AGENTS.md) is the developer guide. It covers the layout of the code, the feature-unit rules, the contracts between modules, the client companion and the Bedrock / LeviLamina rules. Please read it before changing anything under `src/`.

## Pull requests

1. Fork the repository and create a branch from `main`.
2. Keep each pull request focused on one change. Describe what it does and how you tested it.
3. Follow the existing style: C++20, the provided `.clang-format` and `.editorconfig`, English in code and comments.
4. User-facing text goes through `abp::tr` / `abp::trFor` with entries in **both** `assets/lang/en_US/` and `assets/lang/zh_CN/` (and the resource pack's `texts.d/`).
5. Logic in `src/core` must stay free of game headers and come with unit tests in `tests/`:
   `xmake build -y abp-tests; xmake run abp-tests`. Features come with in-game selftests.
6. Both builds must compile: `pwsh tools/build.ps1 -Target all`.
7. If you change something visible in game, run the relevant end-to-end scenario from [tools/e2e](tools/e2e/README.md) and check its screenshots. Better still, add a scenario.
8. CI must pass before a pull request is merged.

Commit messages use the imperative mood ("Add jukebox tab", not "Added …"). Update [CHANGELOG.md](CHANGELOG.md) under *Unreleased* for user-visible changes.

Never copy code or assets from the Java Sophisticated Backpacks repository: it is a behaviour reference only.

## Development

### Building

You need Windows x64, [xmake](https://xmake.io), Visual Studio Build Tools with MSVC 14.4x or newer, and LLVM (`clang-cl`).

```powershell
git config --global core.longpaths true
pwsh tools/build.ps1 -Target all -Tests     # or -Target server / -Target client
```

Outputs: `bin/server/AlleviatedBackpacks/` (copy to `<BDS>/plugins/`) and
`bin/client/AlleviatedBackpacks/` (copy to `%APPDATA%\levilauncher.exe\versions\<version>\mods\`).
There are two builds from one source: `server` for BDS and `client` for the game. The client
build is the only backpack UI: it draws Sophisticated Backpacks' screens, adds the key bindings and
the Shift contents tooltip, while every item move is still done by the server.

### Tests

- Unit tests for `src/core`: `xmake build -y abp-tests; xmake run abp-tests`.
- In-game selftests: `/backpack selftest ["glob"]` (operators / console).
- End-to-end tests that drive the real server and game: [tools/e2e/README.md](tools/e2e/README.md).

### Showcase media

The screenshots and videos in `docs/showcase/` (README and the
[LeviMC Mods gallery](https://levimc.org/mods/github.com/LiteLDev/AlleviatedBackpacks/?tab=gallery),
listed in `docs/showcase/showcase.json`) come from the client scenario `showcase`:

```powershell
pwsh -NoProfile -File tools/e2e/client/run.ps1 -Scenario showcase
pwsh tools/e2e/client/showcase-encode.ps1      # client/out/showcase -> docs/showcase (needs ffmpeg)
```

### How it works

- [AGENTS.md](AGENTS.md): code layout, feature units, contracts between modules, the client
  companion and the Bedrock / LeviLamina rules.
- [docs/API.md](docs/API.md): the energy (FE) and fluid (mB) API that the Tank, Battery, Pump and
  XP Pump upgrades use, public for other mods.
- [docs/FEATURES.md](docs/FEATURES.md): feature-by-feature comparison with Sophisticated Backpacks.
- [docs/ADMIN.md](docs/ADMIN.md): every command and config file, for server operators.

## License

By contributing you agree that your contributions are licensed under the [LGPL-3.0-or-later](COPYING.LESSER), the license of this project.
