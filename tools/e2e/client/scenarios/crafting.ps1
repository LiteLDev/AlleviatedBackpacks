# U14: Crafting + Stonecutter upgrades. Selftests in a real local world, then the crafting and
# stonecutter tabs driven by real clicks (screenshots in out/crafting/).
foreach ($glob in 'crafting.*', 'stonecutter.*') {
    $r = Invoke-SelfTest $glob
    if ($r.Fail -gt 0 -or $r.Pass -lt 1) { throw "selftests ${glob}: pass=$($r.Pass) fail=$($r.Fail)" }
}

Invoke-Chat '/clear @s'
Invoke-Chat '/replaceitem entity @s slot.hotbar 0 alleviatedbackpacks:iron_backpack'
Invoke-Chat '/replaceitem entity @s slot.hotbar 1 alleviatedbackpacks:crafting_upgrade'
Invoke-Chat '/replaceitem entity @s slot.hotbar 2 alleviatedbackpacks:stonecutter_upgrade'
foreach ($i in 3..6) { Invoke-Chat "/replaceitem entity @s slot.hotbar $i oak_planks 1" }
Invoke-Chat '/replaceitem entity @s slot.hotbar 7 stone 8'
Press-Key '1'
Start-Sleep -Seconds 1

# Double-chest geometry (fractions of the 1600x900 client area).
function Slot-Pos([int]$slot) { @((0.365 + ($slot % 9) * 0.035), (0.232 + [math]::Floor($slot / 9) * 0.0623)) }
function Hotbar-Pos([int]$i) { @((0.365 + $i * 0.035), 0.819) }
function Inv-Pos([int]$row, [int]$col) { @((0.365 + $col * 0.035), (0.632 + $row * 0.0623)) }
function Click-At($p) { Click $p[0] $p[1] -Relative; Start-Sleep -Milliseconds 350 }
function Shift-Click-At($p) {
    [AbpE2E.Native]::Key(0x10, $false)
    Click $p[0] $p[1] -Relative
    [AbpE2E.Native]::Key(0x10, $true)
    Start-Sleep -Milliseconds 500
}
function Click-Menu([int]$slot, [string]$waitTitle) {
    $mark = Get-LogMark
    Click-At (Slot-Pos $slot)
    if ($waitTitle) {
        Wait-LogLine "menu shown: $waitTitle \(54 slots\)" 10 -Since $mark | Out-Null
        Start-Sleep -Milliseconds 800
    }
}
function Assert-Info([string]$pattern) {
    $mark = Get-LogMark
    Invoke-Chat '/backpack info'
    $line = Wait-LogLine 'backpack info: ' 10 -Since $mark
    if ($line -notmatch $pattern) { throw "backpack info: expected /$pattern/, got: $line" }
}

# 1. Open the backpack, go to the upgrades page and install both upgrades by hand.
$mark = Get-LogMark
Click 0.5 0.5 -Relative -Right
Wait-LogLine 'menu shown: Iron Backpack \(54 slots\)' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 800
Click-Menu 52
Start-Sleep -Milliseconds 600
Click-At (Hotbar-Pos 1); Click-At (Slot-Pos 0)
Click-At (Hotbar-Pos 2); Click-At (Slot-Pos 1)
Start-Sleep -Milliseconds 600
Save-Screenshot 'upgrades-installed' | Out-Null

# 2. Settings button of the crafting upgrade -> the crafting tab.
Click-Menu 18 'Crafting'
Save-Screenshot 'crafting-empty' | Out-Null

# 3. Put four planks into the 2x2 corner of the grid: the result shows a crafting table.
$grid = @(1, 2, 10, 11)
for ($i = 0; $i -lt 4; $i++) {
    Click-At (Hotbar-Pos (3 + $i))
    Click-At (Slot-Pos $grid[$i])
}
Start-Sleep -Milliseconds 600
Save-Screenshot 'crafting-grid-filled' | Out-Null

# 4. Click the result: the crafting table goes onto the cursor; drop it into the inventory.
Click-At (Slot-Pos 14)
Start-Sleep -Milliseconds 500
Save-Screenshot 'crafting-result-on-cursor' | Out-Null
Click-At (Inv-Pos 0 0)
Start-Sleep -Milliseconds 600
Save-Screenshot 'crafting-done' | Out-Null

# 5. Switch to the stonecutter tab, put stone in and pick an output.
Click-Menu 8 'Stonecutter'
Click-At (Hotbar-Pos 7); Click-At (Slot-Pos 10)
Start-Sleep -Milliseconds 600
Save-Screenshot 'stonecutter-options' | Out-Null
Click-At (Slot-Pos 3)
Start-Sleep -Milliseconds 600
Save-Screenshot 'stonecutter-selected' | Out-Null
# 6. Shift-click the result: everything is cut into the backpack.
Shift-Click-At (Slot-Pos 19)
Start-Sleep -Milliseconds 600
Save-Screenshot 'stonecutter-shift-crafted' | Out-Null
Close-Screen
Start-Sleep -Seconds 1

Assert-Info 'upgrades=\[crafting, stonecutter\]'
Assert-Info ' items=([1-9]\d*) '
Assert-NoErrors
