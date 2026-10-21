# Tank / Pump / XP Pump upgrades (U17) in a real local world: selftests, then the tank GUI driven
# by real clicks (install a tank, open its menu, pour a water bucket in) with screenshots.
foreach ($glob in 'tank.*', 'pump.*', 'xppump.*') {
    $r = Invoke-SelfTest $glob
    if ($r.Fail -gt 0 -or $r.Pass -lt 4) { throw "$glob selftests: pass=$($r.Pass) fail=$($r.Fail)" }
}

Invoke-Chat '/clear @s'
Invoke-Chat '/replaceitem entity @s slot.hotbar 0 alleviatedbackpacks:iron_backpack'
Invoke-Chat '/replaceitem entity @s slot.hotbar 1 alleviatedbackpacks:tank_upgrade'
Invoke-Chat '/replaceitem entity @s slot.hotbar 2 water_bucket'
Invoke-Chat '/replaceitem entity @s slot.hotbar 3 alleviatedbackpacks:xp_pump_upgrade'
Press-Key '1'
Start-Sleep -Seconds 1
Save-Screenshot 'holding' | Out-Null

# Double chest (backpack GUI): slot (row r, column c) and the hotbar, as fractions of the client area.
function DoubleSlot([int]$i) { return @((0.365 + 0.035 * ($i % 9)), (0.232 + 0.0622 * [math]::Floor($i / 9))) }
function DoubleHotbar([int]$i) { return @((0.365 + 0.035 * $i), 0.819) }
# Single chest (upgrade menus): the same grid, shifted up by the three missing rows.
function SingleSlot([int]$i) { return @((0.365 + 0.035 * ($i % 9)), (0.325 + 0.0622 * [math]::Floor($i / 9))) }
function SingleHotbar([int]$i) { return @((0.365 + 0.035 * $i), 0.726) }
function ClickAt($xy) { Click $xy[0] $xy[1] -Relative; Start-Sleep -Milliseconds 500 }

# 1. Open the backpack, go to the upgrades page, install the tank from the hotbar.
$mark = Get-LogMark
Click 0.5 0.5 -Relative -Right
Wait-LogLine 'menu shown: Iron Backpack \(54 slots\)' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 800
ClickAt (DoubleSlot 52)
Save-Screenshot 'upgrades-page' | Out-Null
ClickAt (DoubleHotbar 1)   # pick up the tank upgrade
ClickAt (DoubleSlot 0)     # put it into upgrade slot 0
ClickAt (DoubleHotbar 3)   # pick up the XP pump upgrade
ClickAt (DoubleSlot 1)     # upgrade slot 1
Start-Sleep -Milliseconds 500
Save-Screenshot 'upgrades-installed' | Out-Null

# 2. The tank's settings button opens the tank menu.
$mark = Get-LogMark
ClickAt (DoubleSlot 18)
Wait-LogLine 'menu shown: Tank Upgrade \(27 slots\)' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 800
Save-Screenshot 'tank-menu' | Out-Null

# 3. Pour the water bucket in through the input slot (11).
ClickAt (SingleHotbar 2)
ClickAt (SingleSlot 11)
Start-Sleep -Milliseconds 800
Save-Screenshot 'tank-filled' | Out-Null
# Hover the gauge to show its tooltip.
Move-Mouse (SingleSlot 4)[0] (SingleSlot 4)[1] -Relative
Start-Sleep -Milliseconds 800
Save-Screenshot 'tank-gauge' | Out-Null

# 4. Back to the upgrades page, open the XP pump menu.
$mark = Get-LogMark
ClickAt (SingleSlot 18)
Wait-LogLine 'menu shown: Iron Backpack \(54 slots\)' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 800
$mark = Get-LogMark
ClickAt (DoubleSlot 19)
Wait-LogLine 'menu shown: XP Pump Upgrade \(27 slots\)' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 800
Save-Screenshot 'xppump-menu' | Out-Null
Close-Screen
Start-Sleep -Seconds 1
Save-Screenshot 'closed' | Out-Null
Assert-NoErrors
