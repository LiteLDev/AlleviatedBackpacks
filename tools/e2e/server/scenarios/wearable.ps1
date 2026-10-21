# U19 wearable backpacks: chest-slot wearing, worn ticking/GUI, double sneak, mob backpacks
# (spawn, drops, lazy loot) and loot-chest injection. Mob tests raise the difficulty themselves.
$r = Invoke-SelfTest 'wearable.*'
if ($r.Fail -gt 0 -or $r.Pass -lt 11) { throw "wearable selftests: pass=$($r.Pass) fail=$($r.Fail)" }

