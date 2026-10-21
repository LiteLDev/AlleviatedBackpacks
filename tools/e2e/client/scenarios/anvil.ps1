# U18: Anvil & Smithing upgrades. Selftests in a local world, then the real GUI with real clicks:
# install both upgrades, repair a sword in the anvil tab, rename it through the form, then upgrade
# it to netherite in the smithing tab.
foreach ($glob in 'anvil.*', 'smithing.*') {
    $r = Invoke-SelfTest $glob
    if ($r.Fail -gt 0 -or $r.Pass -lt 3) { throw "selftests ${glob}: pass=$($r.Pass) fail=$($r.Fail)" }
}

Invoke-Chat '/clear @s'
Invoke-Chat '/xp 30L @s'
Invoke-Chat '/replaceitem entity @s slot.hotbar 0 alleviatedbackpacks:diamond_backpack'
Invoke-Chat '/replaceitem entity @s slot.hotbar 1 alleviatedbackpacks:anvil_upgrade'
Invoke-Chat '/replaceitem entity @s slot.hotbar 2 diamond_sword 1 1200'
Invoke-Chat '/replaceitem entity @s slot.hotbar 3 diamond 5'
Invoke-Chat '/replaceitem entity @s slot.hotbar 4 alleviatedbackpacks:smithing_upgrade'
Invoke-Chat '/replaceitem entity @s slot.hotbar 5 netherite_upgrade_smithing_template 1'
Invoke-Chat '/replaceitem entity @s slot.hotbar 6 netherite_ingot 1'
Press-Key '1'
Start-Sleep -Seconds 1

# Double chest layout in fractions of the 1600x900 client area.
function Menu-Slot([int]$i) { return @((0.365 + 0.035 * ($i % 9)), (0.232 + 0.0622 * [Math]::Floor($i / 9))) }
function Hotbar-Slot([int]$i) { return @((0.365 + 0.035 * $i), 0.819) }
function Inv-Slot([int]$i) { return @((0.365 + 0.035 * ($i % 9)), (0.632 + 0.0622 * [Math]::Floor($i / 9))) }
function Click-At($p) { Click $p[0] $p[1] -Relative; Start-Sleep -Milliseconds 450 }
function Wait-Menu([string]$title, $mark) { Wait-LogLine "menu shown: $title \(54 slots\)" 10 -Since $mark | Out-Null; Start-Sleep -Milliseconds 900 }

# 1. Open the backpack and install both upgrades on the upgrades page.
$mark = Get-LogMark
Click 0.5 0.5 -Relative -Right
Wait-Menu 'Diamond Backpack' $mark
Click-At (Menu-Slot 52)
Start-Sleep -Milliseconds 500
Click-At (Hotbar-Slot 1); Click-At (Menu-Slot 0)
Click-At (Hotbar-Slot 4); Click-At (Menu-Slot 1)
Save-Screenshot 'upgrades-installed' | Out-Null
# Re-enter the upgrades page so its button rows are rebuilt for the new upgrades.
Click-At (Menu-Slot 45)
Start-Sleep -Milliseconds 500
Save-Screenshot 'storage-buttons' | Out-Null
Click-At (Menu-Slot 52)
Start-Sleep -Milliseconds 500
Save-Screenshot 'upgrades-page' | Out-Null

# 2./3. The installed upgrade's button (row 3 of the upgrades page) opens the anvil tab
# (the storage page also gets Anvil/Smithing buttons, possibly behind the "More buttons" pager).
# Repair the sword with diamonds.
$mark = Get-LogMark
Click-At (Menu-Slot 18)
Wait-Menu 'Anvil' $mark
Save-Screenshot 'anvil-open' | Out-Null
Click-At (Hotbar-Slot 2); Click-At (Menu-Slot 2)
Click-At (Hotbar-Slot 3); Click-At (Menu-Slot 4)
Start-Sleep -Milliseconds 600
Save-Screenshot 'anvil-inputs' | Out-Null
Move-Mouse (Menu-Slot 6)[0] (Menu-Slot 6)[1] -Relative
Start-Sleep -Milliseconds 800
Save-Screenshot 'anvil-result-hover' | Out-Null

# 4. Rename through the form, then take the result.
$mark = Get-LogMark
Click-At (Menu-Slot 8)
Start-Sleep -Seconds 2
Save-Screenshot 'rename-form' | Out-Null
Click 0.5 0.365 -Relative          # the text field
Start-Sleep -Milliseconds 500
Type-Text 'Excalibur'
Start-Sleep -Milliseconds 500
Save-Screenshot 'rename-typed' | Out-Null
Click 0.5 0.482 -Relative          # Submit
Wait-Menu 'Anvil' $mark
Move-Mouse (Menu-Slot 6)[0] (Menu-Slot 6)[1] -Relative
Start-Sleep -Milliseconds 800
Save-Screenshot 'after-form' | Out-Null

# 5. Take the repaired sword into the inventory (first slot of the main inventory).
Click-At (Menu-Slot 6)
Click-At (Inv-Slot 0)
Start-Sleep -Milliseconds 600
Save-Screenshot 'anvil-taken' | Out-Null

# 6. Smithing tab: diamond sword -> netherite sword.
$mark = Get-LogMark
Click-At (Menu-Slot 0)
Wait-Menu 'Diamond Backpack' $mark
Save-Screenshot 'back-to-backpack' | Out-Null
Click-At (Menu-Slot 52)
Start-Sleep -Milliseconds 500
$mark = Get-LogMark
Click-At (Menu-Slot 19)
Wait-Menu 'Smithing' $mark
Click-At (Hotbar-Slot 5); Click-At (Menu-Slot 2)
Click-At (Inv-Slot 0); Click-At (Menu-Slot 3)
Click-At (Hotbar-Slot 6); Click-At (Menu-Slot 4)
Start-Sleep -Milliseconds 600
Save-Screenshot 'smithing-inputs' | Out-Null
Click-At (Menu-Slot 6)
Click-At (Inv-Slot 1)
Start-Sleep -Milliseconds 600
Save-Screenshot 'smithing-taken' | Out-Null
Close-Screen
Assert-NoErrors
