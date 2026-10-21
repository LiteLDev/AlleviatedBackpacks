# U13 cooking: selftests in a local world, then the Smelting upgrade GUI driven by real clicks.
$r = Invoke-SelfTest 'cooking.*'
if ($r.Fail -gt 0 -or $r.Pass -lt 13) { throw "cooking selftests: pass=$($r.Pass) fail=$($r.Fail)" }

Invoke-Chat '/clear @s'
Invoke-Chat '/replaceitem entity @s slot.hotbar 0 alleviatedbackpacks:diamond_backpack'
Invoke-Chat '/replaceitem entity @s slot.hotbar 1 alleviatedbackpacks:smelting_upgrade'
Invoke-Chat '/replaceitem entity @s slot.hotbar 2 raw_iron 8'
Invoke-Chat '/replaceitem entity @s slot.hotbar 3 coal 4'
Press-Key '1'
Start-Sleep -Seconds 1

# Fractions of the 1600x900 client area. Double chest (backpack GUI):
function DoubleSlot([int]$i) { @((0.365 + ($i % 9) * 0.035), (0.232 + [math]::Floor($i / 9) * 0.0622)) }
$dHotbar = { param([int]$i) @((0.365 + $i * 0.035), 0.819) }

# 1. Open the backpack and switch to the upgrades page.
$mark = Get-LogMark
Click 0.5 0.5 -Relative -Right
Wait-LogLine 'menu shown: Diamond Backpack \(54 slots\)' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 800
$s = DoubleSlot 52; Click $s[0] $s[1] -Relative
Start-Sleep -Milliseconds 800
Save-Screenshot 'upgrades-page' | Out-Null

# 2. Install the Smelting upgrade: pick it up from the hotbar, drop it into upgrade slot 0.
$h = & $dHotbar 1; Click $h[0] $h[1] -Relative
Start-Sleep -Milliseconds 500
$s = DoubleSlot 0; Click $s[0] $s[1] -Relative
Start-Sleep -Milliseconds 800
Save-Screenshot 'upgrade-installed' | Out-Null

# 3. Open its settings: the furnace menu.
$mark = Get-LogMark
$s = DoubleSlot 18; Click $s[0] $s[1] -Relative
Wait-LogLine 'menu shown: Smelting Upgrade \(27 slots\)' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 1000
Save-Screenshot 'smelting-menu' | Out-Null
Close-Screen
Assert-NoErrors
