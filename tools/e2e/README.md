# AlleviatedBackpacks end-to-end tests

There are two harnesses, one per LeviLamina platform. Both run the real game binaries:

| | Server (`tools/e2e/server`) | Client (`tools/e2e/client`) |
|---|---|---|
| Runs | Bedrock Dedicated Server + LeviLamina 26.51.7 (`bedrock_server_mod.exe`) | Minecraft 1.26.51 through LeviLauncher (instance `1.26.51.01`) |
| Mod taken from | `bin/server/AlleviatedBackpacks` | `bin/client/AlleviatedBackpacks` |
| Driven by | console stdin/stdout | real mouse/keyboard input (`SendInput`) and window screenshots |
| Parallel runs | yes: 3 slots, so 3 runs at a time (more runs wait) | no: one game per machine (lock shared with BEI) |
| Speed | ~20 s plus the tests | ~2 min plus the tests |
| Output | `server/out/<scenario>/` `server.log`, `selftest.json` | `client/out/<scenario>/` `*.png`, `client.log`, `selftest.json` |

Prefer the **server** harness for logic: it is fast, headless and parallel, and the selftests can spawn SimulatedPlayers there. Use the **client** for UI, rendering and anything that needs a real player. Both exit `0` on pass and `1` on fail.

```powershell
pwsh tools/build.ps1 -Target all                                   # or pass -Build below
pwsh -NoProfile -File tools/e2e/server/run.ps1 -Scenario smoke     # [-Build] [-TimeoutSec 900] [-Quiet]
pwsh -NoProfile -File tools/e2e/server/run.ps1 -Scenario full
pwsh -NoProfile -File tools/e2e/client/run.ps1 -Scenario smoke     # [-Build] [-KeepOpen] [-KeepOtherMods]
pwsh -NoProfile -File tools/e2e/client/run.ps1 -Scenario full
```

## Prerequisites

- **Server:** [lip](https://github.com/futrime/lip) on `PATH`. The first `run.ps1` calls `server/install.ps1`. It runs `lip install github.com/LiteLDev/LeviLamina@26.51.7` in an empty folder, which brings in BDS 1.26.51, CrashLogger, PreLoader and PeEditor, and installs everything into `%LOCALAPPDATA%\abp-e2e\bds-base`. This takes about 1–3 minutes, once per machine. `install.ps1 -Force` reinstalls. Set `$env:ABP_E2E_HOME` to use a different folder.
- **Client:** LeviLauncher at `%USERPROFILE%\Desktop\LeviLauncher.exe` (or `$env:ABP_E2E_LAUNCHER`). The instance `%APPDATA%\levilauncher.exe\versions\1.26.51.01` must have been started and signed in by hand once. The screen must stay unlocked, and no one should use the mouse during a run.

## What a run does

**Server** (`server/run.ps1`):
1. Optional `-Build`.
2. `install.ps1` (does nothing if already installed).
3. Takes a free slot.
4. Syncs the slot from `bds-base` and wipes its world, logs and configs.
5. Copies the mod to `plugins\AlleviatedBackpacks`. LeviLamina 26.x servers load mods from `plugins\`, not `mods\`.
6. Writes `server.properties` for a fresh world `ABP-Test`: FLAT, seed 20260521, survival, peaceful, cheats on, `texturepack-required=true`, content log on the console.
7. Runs `scenarios/<name>.prelaunch.ps1` if it exists.
8. Boots the server and waits for `AlleviatedBackpacks enabled` and `Server started`.
9. Runs `gamerule dodaylightcycle false`, `time set noon`, `weather clear`, no weather cycle and no mob spawning.
10. Dot-sources `scenarios/<name>.ps1`.
11. Sends `stop`.

The run **fails** if any of these happen:
- the scenario throws;
- a selftest fails, never prints `DONE`, or matches no test;
- an `ERR`/`FTL` line comes from `AlleviatedBackpacks`, `LeviLamina` or `CrashLogger`;
- a file appears in `logs\crash`;
- the exit code is non-zero;
- the server has to be killed because `stop` did not end it within 60 s.

**Client** (`client/run.ps1`):
1. Optional `-Build`. This happens before the lock, so the game is not blocked while compiling.
2. Takes the game lock.
3. Closes a running game.
4. Deploys the mod to `mods\AlleviatedBackpacks`, always as a fresh copy.
5. Moves every other mod except LeviLamina and CrashLogger (BEI, ...) to `versions\1.26.51.01\mods.abp-e2e-aside\`.
6. Installs the `ABP-Test` world with `LastPlayed` set to now, so it is the first world in the list.
7. Launches the game, waits for `AlleviatedBackpacks enabled`, clicks Play and then the first world, and waits for `Player Spawned`. If the game shows "Connection timed out (Kelp)", it retries twice, then restarts the game, up to 3 launches.
8. Runs the scenario.
9. Checks the log as the server harness does.
10. **Always** cleans up: closes the game, deletes the `ABP-Test` world, removes our mod, moves the other mods back, runs `scenarios/<name>.restore.ps1` and releases the lock.

If a run was killed half-way, the next run puts the aside mods back first.

## Writing a scenario

A scenario is `server/scenarios/<name>.ps1` or `client/scenarios/<name>.ps1`. It is dot-sourced once the mod is enabled and, on the client, once the player is in the world. Put feature logic in **selftests inside the mod** (`/backpack selftest <glob>`) and use the scenario to start them. On the client, the scenario also covers what only a real client can show: screens, clicks and screenshots.

```powershell
# server/scenarios/upgrades.ps1
Invoke-SelfTest 'upgrade.*' | Out-Null                    # records the result; run.ps1 fails the run on any FAIL
Send-Console 'give @a abp:backpack 1'
Wait-LogLine 'Gave .*backpack' 10 | Out-Null

# client/scenarios/backpack-ui.ps1
Invoke-Chat '/give @s abp:backpack 1'
Click 0.5 0.5 -Relative -Right                             # use the item
Start-Sleep -Milliseconds 800
Save-Screenshot 'backpack-open' | Out-Null                 # -> client/out/backpack-ui/backpack-open.png
Close-Screen
$r = Invoke-SelfTest 'ui.*'
if ($r.Pass -lt 3) { throw "expected at least 3 ui tests" }   # throw = failure
```

Optional companion files, on both platforms:
- `<name>.prelaunch.ps1` is dot-sourced before the game or server starts. Use it to seed a mod config, for example. On the server, `$script:SlotDir` is the server folder.
- `<name>.restore.ps1` (client only) runs in a new `pwsh` after the game is closed.

The server `run.ps1` also takes `-Property gamemode=creative,difficulty=easy` for extra `server.properties` values.

### Helpers

| Helper | Server | Client | What it does |
|---|:-:|:-:|---|
| `Invoke-SelfTest 'glob' [-TimeoutSeconds 600] [-AllowEmpty]` | x | x | Sends `backpack selftest "<glob>"` (console on the server, chat on the client; no glob = all tests). Waits for `SELFTEST DONE` and returns `{Pass, Fail, Passed[], Failed[{Name,Reason}], Results[], Ok, Problems[]}`. It does **not** throw. The run fails at the end if any call was not OK. |
| `Wait-LogLine 'regex' [timeoutSec] [-Since $mark]` | x | x | Returns the matching line. Throws on timeout or if the server/game exits. |
| `Get-LogMark` / `Get-ServerLog [-Since $mark]` | x | x | Current log position / log lines of this run. On the client, `Get-LeviLog` does the same. |
| `Send-Console 'cmd'` | x | | Writes one line to the server's stdin. Do not add a leading `/`. |
| `Wait-Seconds n` | x | | Sleeps while still echoing server output. |
| `Invoke-Chat '/cmd'` | | x | Opens chat with `T`, types the text and presses Enter. Cheats are on. |
| `Open-Inventory` / `Close-Screen` | | x | Presses `E` / `Esc` and waits for the screen. |
| `Click x y -Relative [-Right]`, `Move-Mouse x y -Relative`, `Scroll n` | | x | Takes fractions of the 1600x900 client area. |
| `Press-Key E` / `Press-Key ESC` / `Type-Text 'abc'` | | x | Keyboard input. |
| `Save-Screenshot 'name'` | | x | Saves `client/out/<scenario>/name.png`, downscaled to 1280x720. |
| `Assert-NoErrors` | | x | Throws now if the log has ERR/FTL lines. `run.ps1` checks this at the end anyway. |

### Selftest protocol (contract with the mod)

The mod logs these lines, with any prefix:

```
SELFTEST PASS <name>
SELFTEST FAIL <name>: <reason>
SELFTEST DONE pass=<N> fail=<M>
```

The glob is sent **quoted** (`backpack selftest "core.*"`) because `*` is not valid in an unquoted command argument. The mod's `glob` parameter must therefore be a string parameter. Lines may appear twice (once from the logger and once from command output); the harness removes duplicates by name. A command parser error (`Syntax error`, `Unknown command`) stops the wait immediately.

## Locks and concurrency

- **Client:** `%APPDATA%\levilauncher.exe\bei-e2e.lock` is the **same file BEI's harness uses**. There is one game per machine, so ABP and BEI runs from any worktree queue behind each other. The harness polls every 15 s and gives up after 2 h.
- **Server:** each slot `%LOCALAPPDATA%\abp-e2e\slots\<n>\` has a lock `slots\<n>.lock`, so up to `$env:ABP_E2E_SERVER_SLOTS` runs (default 3) go in parallel. Slot n uses UDP port `19200 + 10n` (the next free pair if that is taken). `-Slot n` forces a slot. Installs are serialized by `install.lock` and happen in a temporary folder that is renamed at the end, so concurrent first runs are safe.
- Every lock is a file opened exclusively. **The OS releases it when the process dies**, so locks never go stale. The holder writes its pid, repo and time to `<lock>.owner`, which a waiting run prints. A server left running by a killed run is killed by the next run that takes the same slot.
- `%LOCALAPPDATA%` is virtualized when the harness is started from a packaged app such as the Claude desktop app. The files then really live in `%LOCALAPPDATA%\Packages\<app>\LocalCache\Local\abp-e2e`. Processes started from the same app see the same files, but Explorer shows the real path. `%APPDATA%\levilauncher.exe` is not affected because that folder already exists.

## Reading results

- **Console output** ends with `PASSED` or a `FAILED:` list of reasons. Every selftest failure is printed as `FAIL <name>: <reason>`.
- **`server.log` / `client.log`** contain the full log of this run only. LeviLamina appends `latest.log` across game sessions, but the harness keeps just this session.
- **`selftest.json`** has every `Invoke-SelfTest` result.
- **Screenshots:** open them with the Read tool, which shows images. They are the evidence that a UI feature works. A 1280x720 screenshot point `(px, py)` is `-Relative (px/1280) (py/720)` for `Click`. On failure the client saves `failure.png`, plus `join-failed-N.png` if entering the world failed.

## Pitfalls

- **Elevated windows block injected input.** While a UAC prompt or a Windows Firewall "allow access" dialog is in the foreground, every click and key is dropped. The input helpers then throw "Minecraft is not the foreground window". Close such dialogs by hand.
- **Windows Firewall asks once per new executable path.** Each server slot has its own `bedrock_server_mod.exe` path, so slot n's first run can show that dialog. It does not block the server harness, but it can block a client run happening at the same time. Answer it once and it does not come back. Slots 0–2 have already been answered on the dev machine. Keep `ABP_E2E_SERVER_SLOTS` small.
- **Bedrock pauses when it loses focus.** `Focus-Game` brings the window back before each input. Do not use the mouse or keyboard during a client run.
- **The client window is forced to a 1600x900 client area**, and the harness is DPI-aware, so `-Relative` coordinates are stable.
- **The "Connection timed out (Kelp)" error** sometimes appears when joining the local world. Once it happens, it tends to repeat for the rest of that game session, so `run.ps1` retries twice and then restarts the game, up to 3 launches. The cause is environmental (the game's online signaling/websocket), not the mod.
- **Do not leave worlds or mods behind.** BEI's harness opens the *first* world in the list and loads every mod in `mods\`. `run.ps1` cleans up in `finally`. If you test by hand with `-KeepOpen`, close the game normally so the cleanup runs.
- The fixture world (`client/fixtures/ABP-Test/`) contains only `level.dat`; the game generates flat terrain on load. `client/fixtures/build-fixture.ps1 -Source <world dir|zip>` rebuilds it, for example after a game update. `common/nbt.ps1` reads and writes `level.dat` (`Read-LevelDat`, `Set-NbtValue`, `Show-Nbt`).
