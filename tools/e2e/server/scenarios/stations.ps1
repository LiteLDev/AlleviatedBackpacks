# P2-5 stations: Smelting / Smoking / Blasting (+ Auto), Crafting and Stonecutter upgrades with
# SB behaviour. The selftests drive the native tabs through the companion screen actions.
foreach ($glob in 'cooking.*', 'crafting.*', 'stonecutter.*') {
    $r = Invoke-SelfTest $glob
    if ($r.Fail -gt 0 -or $r.Pass -lt 5) { throw "selftests ${glob}: pass=$($r.Pass) fail=$($r.Fail)" }
}
# The upgrade items come from the behavior pack and must exist.
foreach ($id in 'smelting', 'smoking', 'blasting', 'auto_smelting', 'auto_smoking', 'auto_blasting', 'crafting', 'stonecutter') {
    Send-Console "give @a alleviatedbackpacks:${id}_upgrade"
}
Wait-Seconds 2
if (Get-ServerLog | Select-String 'Unknown item|unknown item|Syntax error') { throw 'station upgrade items are not registered' }
