<#
.SYNOPSIS
  Run an AlleviatedBackpacks scenario in the real game client (LeviLauncher instance).

.DESCRIPTION
  1. (-Build) builds bin/client/AlleviatedBackpacks via tools/build.ps1 -Target client (before
     taking the lock, so the game is not held while compiling).
  2. Takes the machine-wide game lock (%APPDATA%\levilauncher.exe\bei-e2e.lock, shared with
     BEI's harness; waits while another run holds it).
  3. Closes a running game, restores mods a killed run left aside, deploys the mod to
     mods\AlleviatedBackpacks, moves every other mod (e.g. BEI) aside, installs the ABP-Test world.
  4. Runs optional scenarios/<name>.prelaunch.ps1, launches the game, waits for
     "AlleviatedBackpacks enabled", enters the world, dot-sources scenarios/<name>.ps1.
  5. Fails on: scenario exception, failing/empty/unfinished selftests, ERR/FTL lines from
     AlleviatedBackpacks/LeviLamina/CrashLogger.
  6. Always: closes the game, removes the ABP-Test world and our mod, restores the other mods,
     runs scenarios/<name>.restore.ps1, releases the lock.
  Output: tools/e2e/client/out/<scenario>/ (screenshots, client.log, selftest.json). Look at the
  screenshots.

  Modes (-Mode):
    local           (default) the ABP-Test local world: the integrated server runs the mod too.
    remote-mod      starts a BDS slot with the server build (tools/e2e/server, scenario
                    client-host), joins it through the Servers tab; the client runs the mod.
    remote-vanilla  same BDS, but our client mod is moved aside: a vanilla client (plus
                    LeviLamina) that only gets the server's resource pack. Backpack screens must be
                    refused with the "needs the client mod" message.
  In remote modes the BDS console log is merged into the session log ("[host] " lines) and
  Send-HostConsole runs server console commands.

.EXAMPLE
  pwsh tools/e2e/client/run.ps1 -Scenario smoke -Build
  pwsh tools/e2e/client/run.ps1 -Scenario companion -Mode remote-mod
  pwsh tools/e2e/client/run.ps1 -Scenario smoke -KeepOpen   # play around; close the game to finish
#>
param(
    [Parameter(Mandatory)] [string]$Scenario,
    [switch]$Build,
    [switch]$KeepOpen,          # leave the game running; the harness waits until you close it, then cleans up
    [switch]$KeepOtherMods,     # do not move other mods (BEI, ...) aside
    [string]$ModDir,            # default: <repo>/bin/client/AlleviatedBackpacks
    [ValidateSet('local', 'remote-mod', 'remote-vanilla')][string]$Mode = 'local'
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/lib.ps1"

$scenarioFile = Join-Path $PSScriptRoot "scenarios/$Scenario.ps1"
if (-not (Test-Path $scenarioFile)) { throw "Unknown scenario: $scenarioFile" }
if (-not $ModDir) { $ModDir = Join-Path $script:RepoRoot "bin/client/$script:ModName" }
$script:Unit = $Scenario
$outDir = Join-Path $script:OutRoot $Scenario
if (Test-Path $outDir) { Remove-Item -Recurse -Force $outDir }
New-Item -ItemType Directory -Force $outDir | Out-Null

$failures = [System.Collections.Generic.List[string]]::new()
$script:Mode   = $Mode
$remote        = $Mode -ne 'local'
$hostProc      = $null
$script:HostDir = $null
try {
    if ($Build) { Invoke-ModBuild client; if ($remote) { Invoke-ModBuild server } }
    if ($remote) {
        # A BDS slot with the server build; it runs until we write <hostDir>\stop.
        $script:HostDir = Join-Path $outDir 'host'
        New-Item -ItemType Directory -Force $script:HostDir | Out-Null
        $env:ABP_E2E_HOST_DIR = $script:HostDir
        $serverRun = Join-Path $PSScriptRoot '../server/run.ps1'
        $hostProc  = Start-Process pwsh -ArgumentList @('-NoProfile', '-File', $serverRun, '-Scenario', 'client-host', '-Quiet', '-TimeoutSec', '7200') `
            -PassThru -WindowStyle Hidden -RedirectStandardOutput (Join-Path $outDir 'host-harness.log') -RedirectStandardError (Join-Path $outDir 'host-harness.err')
        Write-Step "starting the BDS host (pid $($hostProc.Id))"
        $ready = Join-Path $script:HostDir 'ready.json'
        $deadline = (Get-Date).AddMinutes(10)
        while (-not (Test-Path $ready)) {
            if ($hostProc.HasExited) { throw "the BDS host exited early (see $outDir\host-harness.log)" }
            if ((Get-Date) -gt $deadline) { throw 'the BDS host did not get ready in 10 min' }
            Start-Sleep -Seconds 2
        }
        $hostInfo = Get-Content $ready -Raw | ConvertFrom-Json
        $script:HostLog = Join-Path $PSScriptRoot '../server/out/client-host/server.log'
        Write-Step "BDS host ready on port $($hostInfo.port)"
    }
    Enter-E2ELock
    try {
        if (Get-GameProcess) { Write-Step "closing a running game first"; Stop-Game }
        Restore-OtherMods
        if ($Mode -eq 'remote-vanilla') {
            Remove-DeployedMod # vanilla client: only LeviLamina (+ CrashLogger)
        } else {
            Deploy-Mod $ModDir
        }
        if (-not $KeepOtherMods) { Move-OtherModsAside }
        if ($remote) { Set-ExternalServer $hostInfo.port } else { Restore-TestWorld }
        Reset-CameraPerspective
        $prelaunch = Join-Path $PSScriptRoot "scenarios/$Scenario.prelaunch.ps1"
        if (Test-Path $prelaunch) { . $prelaunch }
        try {
            # Joining the local world sometimes fails with "Connection timed out (Kelp)" and,
            # once it does, keeps failing in that game session: retry with a fresh game.
            for ($launch = 1; ; $launch++) {
                if ($remote) { Clear-OurServerEntries }
                Start-Game | Out-Null
                Wait-GameReady -WithoutMod:($Mode -eq 'remote-vanilla')
                try {
                    if ($remote) { Enter-RemoteServer -Attempts 1 } else { Enter-TestWorld -Attempts 2 }
                    break
                } catch {
                    if ($launch -ge 3) { throw }
                    Write-Step "could not enter the world ($_); restarting the game (launch $($launch + 1)/3)"
                    Stop-Game
                    Start-Sleep -Seconds 3
                }
            }
            Write-Step "running scenario '$Scenario'"
            . $scenarioFile
        } catch {
            $failures.Add("scenario: $_")
            Write-Host "[e2e] $($_.ScriptStackTrace)" -ForegroundColor DarkGray
            try { Save-Screenshot 'failure' | Out-Null } catch {}
            Get-LeviLog | Select-Object -Last 40 | ForEach-Object { Write-Host "  | $_" }
        }
        $errs = Get-ErrorLines
        if ($errs) {
            $failures.Add("$($errs.Count) ERR/FTL line(s) from $script:ModName/LeviLamina/CrashLogger")
            $errs | Select-Object -First 20 | ForEach-Object { Write-Host "  ! $_" -ForegroundColor Red }
        }
        foreach ($p in Get-SelfTestProblems) { $failures.Add($p) }
        Save-SelfTestReport (Join-Path $outDir 'selftest.json')
        Get-LeviLog | Set-Content -Path (Join-Path $outDir 'client.log') -Encoding UTF8
    } finally {
        if ($KeepOpen -and (Get-GameProcess)) {
            Write-Step "-KeepOpen: the game stays open; close it to finish the run (lock still held)"
            while (Get-GameProcess) { Start-Sleep -Seconds 2 }
        }
        Stop-Game
        if ($remote) { Restore-ExternalServers } else { Remove-TestWorld }
        Remove-DeployedMod
        Restore-OtherMods
        $restore = Join-Path $PSScriptRoot "scenarios/$Scenario.restore.ps1"
        if (Test-Path $restore) { & pwsh -NoProfile -File $restore }
        Exit-E2ELock
    }
} catch {
    $failures.Add("harness: $_")
    Write-Host "[e2e] $($_.ScriptStackTrace)" -ForegroundColor DarkGray
} finally {
    if ($hostProc) {
        Set-Content -Path (Join-Path $script:HostDir 'stop') -Value 'stop'
        if (-not $hostProc.WaitForExit(120000)) { $hostProc.Kill($true); $failures.Add('BDS host did not stop') }
        elseif ($hostProc.ExitCode -ne 0) { $failures.Add("BDS host failed (exit $($hostProc.ExitCode), see tools/e2e/server/out/client-host/)") }
        Remove-Item Env:ABP_E2E_HOST_DIR -ErrorAction SilentlyContinue
    }
}

if ($failures.Count -gt 0) {
    Write-Host "[e2e] client scenario '$Scenario' FAILED:" -ForegroundColor Red
    $failures | ForEach-Object { Write-Host "[e2e]   - $_" -ForegroundColor Red }
    Write-Host "[e2e] output: $outDir"
    exit 1
}
Write-Host "[e2e] client scenario '$Scenario' PASSED (screenshots: $outDir)" -ForegroundColor Green
exit 0
