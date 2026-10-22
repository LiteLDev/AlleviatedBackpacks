# misc_upgrades: first person camera (the scenario looks down at the ground and mines it) and the
# tool swap key bound to G (SB leaves it unbound by default; the binding is a persisted client
# option, `keyboard_type_0_key.AlleviatedBackpacks.tool_swap`).
$opts = Get-ChildItem "$env:APPDATA\levilauncher.exe\versions\1.26.51.01\Minecraft Bedrock\Users" -Recurse -Filter options.txt -ErrorAction SilentlyContinue
foreach ($f in $opts) {
    $text = Get-Content -Raw $f.FullName
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
