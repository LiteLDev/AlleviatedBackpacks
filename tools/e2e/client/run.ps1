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

.EXAMPLE
  pwsh tools/e2e/client/run.ps1 -Scenario smoke -Build
  pwsh tools/e2e/client/run.ps1 -Scenario smoke -KeepOpen   # play around; close the game to finish
#>
param(
    [Parameter(Mandatory)] [string]$Scenario,
    [switch]$Build,
    [switch]$KeepOpen,          # leave the game running; the harness waits until you close it, then cleans up
    [switch]$KeepOtherMods,     # do not move other mods (BEI, ...) aside
    [string]$ModDir             # default: <repo>/bin/client/AlleviatedBackpacks
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
try {
    if ($Build) { Invoke-ModBuild client }
    Enter-E2ELock
    try {
        if (Get-GameProcess) { Write-Step "closing a running game first"; Stop-Game }
        Restore-OtherMods
        Deploy-Mod $ModDir
        if (-not $KeepOtherMods) { Move-OtherModsAside }
        Restore-TestWorld
        $prelaunch = Join-Path $PSScriptRoot "scenarios/$Scenario.prelaunch.ps1"
        if (Test-Path $prelaunch) { . $prelaunch }
        try {
            # Joining the local world sometimes fails with "Connection timed out (Kelp)" and,
            # once it does, keeps failing in that game session: retry with a fresh game.
            for ($launch = 1; ; $launch++) {
                Start-Game | Out-Null
                Wait-GameReady
                try { Enter-TestWorld -Attempts 2; break } catch {
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
        Remove-TestWorld
        Remove-DeployedMod
        Restore-OtherMods
        $restore = Join-Path $PSScriptRoot "scenarios/$Scenario.restore.ps1"
        if (Test-Path $restore) { & pwsh -NoProfile -File $restore }
        Exit-E2ELock
    }
} catch {
    $failures.Add("harness: $_")
    Write-Host "[e2e] $($_.ScriptStackTrace)" -ForegroundColor DarkGray
}

if ($failures.Count -gt 0) {
    Write-Host "[e2e] client scenario '$Scenario' FAILED:" -ForegroundColor Red
    $failures | ForEach-Object { Write-Host "[e2e]   - $_" -ForegroundColor Red }
    Write-Host "[e2e] output: $outDir"
    exit 1
}
Write-Host "[e2e] client scenario '$Scenario' PASSED (screenshots: $outDir)" -ForegroundColor Green
exit 0
