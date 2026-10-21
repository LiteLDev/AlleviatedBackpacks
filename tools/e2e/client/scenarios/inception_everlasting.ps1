# U15: Inception & Everlasting upgrades. Selftests in a local world, then the real GUI:
# install the Inception (and Everlasting) upgrade by shift-click, store a backpack inside, open it
# from the GUI (Q on its slot, or the "Nested backpacks" button) and go back with the Back button.
$problems = @()
foreach ($glob in 'inception.*', 'everlasting.*') {
    $r = Invoke-SelfTest $glob
    if ($r.Fail -gt 0 -or $r.Pass -lt 5) { $problems += "selftests $glob : pass=$($r.Pass) fail=$($r.Fail)" }
}

Invoke-Chat '/clear @s'
Invoke-Chat '/replaceitem entity @s slot.hotbar 0 alleviatedbackpacks:iron_backpack'
Invoke-Chat '/replaceitem entity @s slot.hotbar 1 alleviatedbackpacks:inception_upgrade'
Invoke-Chat '/replaceitem entity @s slot.hotbar 2 alleviatedbackpacks:leather_backpack'
Invoke-Chat '/replaceitem entity @s slot.hotbar 3 alleviatedbackpacks:everlasting_upgrade'
Press-Key '1'
Start-Sleep -Seconds 1

# Fractions of the 1600x900 client area for the double-chest layout (see foundation.ps1).
$col0 = 0.365; $colW = 0.035; $row0 = 0.232; $rowH = 0.0622; $hotbarY = 0.819
function Slot-X([int]$col) { $col0 + $col * $colW }
function Menu-Slot([int]$i) { @((Slot-X ($i % 9)), ($row0 + [math]::Floor($i / 9) * $rowH)) }
function Hotbar([int]$i) { @((Slot-X $i), $hotbarY) }
function Shift-Click($pos) {
    [AbpE2E.Native]::Key(0x10, $false)
    Click $pos[0] $pos[1] -Relative
    [AbpE2E.Native]::Key(0x10, $true)
    Start-Sleep -Milliseconds 800
}
function Click-Slot($pos) { Click $pos[0] $pos[1] -Relative; Start-Sleep -Milliseconds 800 }

# 1. Open the iron backpack, go to the upgrades page and shift-click both upgrades in.
$mark = Get-LogMark
Click 0.5 0.5 -Relative -Right
Wait-LogLine 'menu shown: Iron Backpack \(54 slots\)' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 1500
Click-Slot (Menu-Slot 52)
Shift-Click (Hotbar 1)
Shift-Click (Hotbar 3)
Save-Screenshot 'upgrades-installed' | Out-Null

# 2. Back to storage, shift-click the leather backpack in.
Click-Slot (Menu-Slot 45)
Shift-Click (Hotbar 2)
Save-Screenshot 'backpack-stored' | Out-Null

# 3. Q on the stored backpack opens it.
$mark = Get-LogMark
$s0 = Menu-Slot 0
Move-Mouse $s0[0] $s0[1] -Relative
Start-Sleep -Milliseconds 300
Press-Key 'Q'
$opened = $true
try { Wait-LogLine 'menu shown: Leather Backpack \(54 slots\)' 8 -Since $mark | Out-Null }
catch { $opened = $false }
if ($opened) { Write-Step 'Q opened the nested backpack' }
else {
    Write-Step 'Q did not open the nested backpack: using the Nested backpacks button (slot 48)'
    $mark = Get-LogMark
    Click-Slot (Menu-Slot 48)
    Wait-LogLine 'menu shown: Leather Backpack \(54 slots\)' 10 -Since $mark | Out-Null
}
Start-Sleep -Milliseconds 1500
Save-Screenshot 'nested-open' | Out-Null

# 4. Back button (slot 46) returns to the iron backpack.
$mark = Get-LogMark
Click-Slot (Menu-Slot 46)
Wait-LogLine 'menu shown: Iron Backpack \(54 slots\)' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 1500
Save-Screenshot 'back-to-outer' | Out-Null
Close-Screen

# 5. The backpack is still in the iron backpack (not dropped by Q), both upgrades installed.
$mark = Get-LogMark
Invoke-Chat '/backpack info'
$line = Wait-LogLine 'backpack info: ' 10 -Since $mark
if ($line -notmatch ' items=1 ') { $problems += "expected the nested backpack inside, got: $line" }
if ($line -notmatch 'inception' -or $line -notmatch 'everlasting') { $problems += "expected both upgrades installed, got: $line" }
Assert-NoErrors
if ($problems.Count) { throw ($problems -join '; ') }
