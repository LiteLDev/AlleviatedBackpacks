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

See [Building](README.md#building) in the README. [AGENTS.md](AGENTS.md) is the developer guide. It covers the layout of the code, the feature-unit rules, the contracts between modules, the client companion and the Bedrock / LeviLamina rules. Please read it before changing anything under `src/`.

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

## License

By contributing you agree that your contributions are licensed under the [LGPL-3.0](COPYING.LESSER), the license of this project.
