# U5 Pickup & Filter upgrades: selftests in a local world, then the upgrade settings GUIs driven by
# real clicks (install via shift-click, filter ghost slots, match/direction buttons).
foreach ($glob in 'pickup.*', 'filter.*') {
    $r = Invoke-SelfTest $glob
    if ($r.Fail -gt 0 -or $r.Pass -lt 5) { throw "${glob}: pass=$($r.Pass) fail=$($r.Fail)" }
}

Invoke-Chat '/clear @s'
Invoke-Chat '/replaceitem entity @s slot.hotbar 0 alleviatedbackpacks:iron_backpack'
Invoke-Chat '/replaceitem entity @s slot.hotbar 1 alleviatedbackpacks:advanced_pickup_upgrade'
Invoke-Chat '/replaceitem entity @s slot.hotbar 2 apple 5'
Invoke-Chat '/replaceitem entity @s slot.hotbar 3 alleviatedbackpacks:filter_upgrade'
Press-Key '1'
Start-Sleep -Seconds 1

# Fractions of the 1600x900 client area. Columns are shared; rows depend on the chest size.
function ColX([int]$c) { 0.365 + 0.035 * $c }
function DoubleY([int]$r) { if ($r -lt 6) { 0.232 + 0.0622 * $r } elseif ($r -lt 9) { 0.632 + 0.0622 * ($r - 6) } else { 0.819 } }
function SingleY([int]$r) { if ($r -lt 3) { 0.325 + 0.0622 * $r } elseif ($r -lt 6) { 0.539 + 0.0622 * ($r - 3) } else { 0.726 } }
function Wait-Menu([string]$pattern, $mark) {
    Wait-LogLine "menu shown: $pattern" 10 -Since $mark | Out-Null
    Start-Sleep -Milliseconds 800
}
function Shift-Click([double]$x, [double]$y) {
    [AbpE2E.Native]::Key(0x10, $false)
    Click $x $y -Relative
    [AbpE2E.Native]::Key(0x10, $true)
    Start-Sleep -Milliseconds 600
}

# 1. Open the backpack, go to the upgrades page.
$mark = Get-LogMark
Click 0.5 0.5 -Relative -Right
Wait-Menu 'Iron Backpack \(54 slots\)' $mark
Click (ColX 7) (DoubleY 5) -Relative      # menu slot 52: upgrades
Start-Sleep -Milliseconds 800
Save-Screenshot 'upgrades-empty' | Out-Null

# 2. Shift-click both upgrades from the hotbar into the upgrade slots.
Shift-Click (ColX 1) (DoubleY 9)
Shift-Click (ColX 3) (DoubleY 9)
Save-Screenshot 'upgrades-installed' | Out-Null

# 3. Advanced pickup settings (slot 18 = settings button of upgrade slot 0).
$mark = Get-LogMark
Click (ColX 0) (DoubleY 2) -Relative
Wait-Menu 'Advanced Pickup Upgrade \(27 slots\)' $mark
Save-Screenshot 'pickup-settings' | Out-Null
Click (ColX 2) (SingleY 9) -Relative      # apples to the cursor
Start-Sleep -Milliseconds 400
Click (ColX 0) (SingleY 0) -Relative      # ghost slot 0
Start-Sleep -Milliseconds 400
Click (ColX 2) (SingleY 9) -Relative      # apples back
Start-Sleep -Milliseconds 600
Click (ColX 1) (SingleY 2) -Relative      # match by: item -> namespace
Start-Sleep -Milliseconds 800
Move-Mouse (ColX 1) (SingleY 2) -Relative
Start-Sleep -Milliseconds 600
Save-Screenshot 'pickup-filter-set' | Out-Null
Click (ColX 1) (SingleY 2) -Relative      # namespace -> tag
Start-Sleep -Milliseconds 500
Click (ColX 1) (SingleY 2) -Relative      # tag -> item
Start-Sleep -Milliseconds 500

# 4. Back to the upgrades page, then the filter upgrade's settings (slot 19).
$mark = Get-LogMark
Click (ColX 8) (SingleY 2) -Relative
Wait-Menu 'Iron Backpack \(54 slots\)' $mark
Move-Mouse (ColX 0) (DoubleY 0) -Relative
Start-Sleep -Milliseconds 600
Save-Screenshot 'upgrades-lore' | Out-Null
$mark = Get-LogMark
Click (ColX 1) (DoubleY 2) -Relative
Wait-Menu 'Filter Upgrade \(27 slots\)' $mark
Click (ColX 5) (SingleY 2) -Relative      # direction: both -> input
Start-Sleep -Milliseconds 800
Move-Mouse (ColX 5) (SingleY 2) -Relative
Start-Sleep -Milliseconds 600
Save-Screenshot 'filter-settings' | Out-Null
Close-Screen
Start-Sleep -Seconds 1

Save-Screenshot 'closed' | Out-Null
Assert-NoErrors
