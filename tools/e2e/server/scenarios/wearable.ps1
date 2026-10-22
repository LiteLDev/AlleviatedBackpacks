# Wearable backpacks: chest-slot wearing, worn ticking/GUI, double sneak (extra, off by default),
# SB's mob backpacks (spawn, buffs, jukebox, drops, lazy loot) and chest loot injection. Mob tests
# raise the difficulty themselves.
$r = Invoke-SelfTest 'wearable.*'
if ($r.Fail -gt 0 -or $r.Pass -lt 12) { throw "wearable selftests: pass=$($r.Pass) fail=$($r.Fail)" }
