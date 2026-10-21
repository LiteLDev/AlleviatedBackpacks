# U3 (gui): selftests in a local world, then the control row driven by real clicks:
# move all in / out, sort, sort-mode cycling, the settings form and `/backpack open`.
$r = Invoke-SelfTest 'gui.*'
if ($r.Fail -gt 0 -or $r.Pass -lt 12) { throw "gui selftests: pass=$($r.Pass) fail=$($r.Fail)" }

Invoke-Chat '/clear @s'
Invoke-Chat '/replaceitem entity @s slot.hotbar 0 alleviatedbackpacks:diamond_backpack'
Invoke-Chat '/replaceitem entity @s slot.hotbar 1 diamond 20'
Invoke-Chat '/replaceitem entity @s slot.inventory 0 dirt 30'
Invoke-Chat '/replaceitem entity @s slot.inventory 1 apple 5'
Invoke-Chat '/replaceitem entity @s slot.inventory 2 stone 40'
Press-Key '1'
Start-Sleep -Seconds 1

# Fractions of the 1600x900 client area: double chest, 9 columns x 6 rows above the inventory.
$col0 = 0.365; $colStep = 0.035
$row0 = 0.232; $rowStep = 0.0622
function Slot-X([int]$i) { $col0 + ($i % 9) * $colStep }
function Slot-Y([int]$i) { $row0 + [math]::Floor($i / 9) * $rowStep }
function Click-Slot([int]$i, [switch]$Shift) {
    if ($Shift) { [AbpE2E.Native]::Key(0x10, $false) }
    Click (Slot-X $i) (Slot-Y $i) -Relative
    if ($Shift) { [AbpE2E.Native]::Key(0x10, $true) }
    Start-Sleep -Milliseconds 700
}
function Open-Backpack([string]$shot) {
    $mark = Get-LogMark
    Click 0.5 0.5 -Relative -Right
    Wait-LogLine 'menu shown: Diamond Backpack \(54 slots\)' 10 -Since $mark | Out-Null
    Start-Sleep -Milliseconds 800
    Save-Screenshot $shot | Out-Null
}
function Get-BackpackItems {
    $mark = Get-LogMark
    Invoke-Chat '/backpack info'
    $line = Wait-LogLine 'backpack info: ' 10 -Since $mark
    if ($line -notmatch ' items=(\d+) ') { throw "unexpected info line: $line" }
    return [int]$Matches[1]
}

# Control row: 48 = Sort, 49 = Move all into the backpack, 50 = Move all to inventory (when no
# other unit registers buttons before them; with more than 4 buttons slot 51 pages them).
Open-Backpack 'controls'
Move-Mouse (Slot-X 49) (Slot-Y 49) -Relative
Start-Sleep -Milliseconds 800
Save-Screenshot 'tooltip-move-in' | Out-Null
Click-Slot 49
Save-Screenshot 'moved-in' | Out-Null
Close-Screen
$n = Get-BackpackItems
# dirt 30 + apple 5 + stone 40 (the hotbar diamonds stay: "keep hotbar" is the default).
if ($n -ne 75) { throw "move all into backpack: expected 75 items, got $n" }

Open-Backpack 'reopened'
Click-Slot 48
Save-Screenshot 'sorted' | Out-Null
Click-Slot 48 -Shift
Move-Mouse (Slot-X 48) (Slot-Y 48) -Relative
Start-Sleep -Milliseconds 800
Save-Screenshot 'sort-mode-count' | Out-Null
Click-Slot 50
Save-Screenshot 'moved-out' | Out-Null
Close-Screen
$n = Get-BackpackItems
if ($n -ne 0) { throw "move all to inventory: expected 0 items, got $n" }

# Settings form (comparator, slot 53).
Open-Backpack 'before-settings'
$mark = Get-LogMark
Click-Slot 53
Start-Sleep -Seconds 2
Save-Screenshot 'settings-form' | Out-Null
Press-Key 'ESC'
Start-Sleep -Seconds 1

# /backpack open with an empty hand opens the carried backpack.
Press-Key '3'
Start-Sleep -Milliseconds 500
$mark = Get-LogMark
Invoke-Chat '/backpack open'
Wait-LogLine 'menu shown: Diamond Backpack \(54 slots\)' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 800
Save-Screenshot 'command-open' | Out-Null
Close-Screen
Assert-NoErrors
