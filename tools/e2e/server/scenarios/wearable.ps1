# U19 wearable backpacks: chest-slot wearing, worn ticking/GUI, double sneak, mob backpacks
# (spawn, drops, lazy loot) and loot-chest injection. Hostile mobs need a difficulty > peaceful.
Send-Console 'difficulty normal'
Wait-Seconds 1
$r = Invoke-SelfTest 'wearable.*'
if ($r.Fail -gt 0 -or $r.Pass -lt 9) { throw "wearable selftests: pass=$($r.Pass) fail=$($r.Fail)" }
Send-Console 'difficulty peaceful'
Wait-Seconds 1
