# misc_upgrades: first person camera (the scenario looks down at the ground and mines it) and the
# tool swap key bound to G (SB leaves it unbound by default; the binding is a persisted client
# option, `keyboard_type_0_key.AlleviatedBackpacks.tool_swap`). Only this slot's instance is
# touched; misc_upgrades.restore.ps1 puts every changed options.txt back afterwards.
$state = Join-Path $env:TEMP "abp-e2e-misc-options-$script:VersionName"
New-Item -ItemType Directory -Force $state | Out-Null
$i = 0
foreach ($f in Get-ChildItem (Get-GameDataDir) -Recurse -Filter options.txt -ErrorAction SilentlyContinue) {
    $text = Get-Content -Raw $f.FullName
    # Back up the original once (a killed earlier run may have left its backup: keep that one).
    $i++
    $backup = Join-Path $state "options-$i.txt"
    if (-not (Test-Path $backup)) {
        [System.IO.File]::WriteAllText($backup, $text)
        Add-Content (Join-Path $state 'map.txt') "$backup|$($f.FullName)"
    }
    $text = $text -replace 'game_thirdperson:\d+', 'game_thirdperson:0'
    $line = 'keyboard_type_0_key.AlleviatedBackpacks.tool_swap:71'
    if ($text -match 'keyboard_type_0_key\.AlleviatedBackpacks\.tool_swap:[^\r\n]*') {
        $text = $text -replace 'keyboard_type_0_key\.AlleviatedBackpacks\.tool_swap:[^\r\n]*', $line
    } else {
        $text = $text.TrimEnd() + "`n" + $line + "`n"
    }
    [System.IO.File]::WriteAllText($f.FullName, $text)
    Write-Host "[e2e] first person camera, tool swap key = G in $($f.FullName)"
}
