# U1 tiers (client): selftests in a local world, then a real tier-upgrade craft in a crafting table.
$r = Invoke-SelfTest 'tiers.*'
if ($r.Fail -gt 0 -or $r.Pass -lt 7) { throw "tiers selftests: pass=$($r.Pass) fail=$($r.Fail)" }

Invoke-Chat '/clear @s'
Invoke-Chat '/replaceitem entity @s slot.hotbar 0 alleviatedbackpacks:leather_backpack'
Invoke-Chat '/replaceitem entity @s slot.hotbar 1 copper_ingot 8'
Invoke-Chat '/replaceitem entity @s slot.hotbar 2 red_dye 1'
Invoke-Chat '/replaceitem entity @s slot.hotbar 3 diamond 20'
Invoke-Chat '/setblock ~ ~ ~2 crafting_table'
Invoke-Chat '/tp @s ~ ~ ~ facing ~ ~0.5 ~2'
Press-Key '1'
Start-Sleep -Seconds 1
Save-Screenshot 'hud' | Out-Null
Click 0.5 0.5 -Relative -Right
Start-Sleep -Seconds 2
Save-Screenshot 'table' | Out-Null
Close-Screen
