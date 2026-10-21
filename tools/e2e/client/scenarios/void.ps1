# Void upgrade (U9): selftests in a local world, then the real GUI: install an Advanced Void
# upgrade, configure it in its settings menu (ghost filter, overflow mode, work in GUI) with real
# clicks, and check that a second stack put in by hand is voided.
$r = Invoke-SelfTest 'void.*'
if ($r.Fail -gt 0 -or $r.Pass -lt 9) { throw "void selftests: pass=$($r.Pass) fail=$($r.Fail)" }

Invoke-Chat '/clear @s'
Invoke-Chat '/replaceitem entity @s slot.hotbar 0 alleviatedbackpacks:diamond_backpack'
Invoke-Chat '/replaceitem entity @s slot.hotbar 1 alleviatedbackpacks:advanced_void_upgrade'
Invoke-Chat '/replaceitem entity @s slot.hotbar 2 cobblestone 64'
Invoke-Chat '/replaceitem entity @s slot.hotbar 3 cobblestone 64'
Press-Key '1'
Start-Sleep -Seconds 1

# Fractions of the 1600x900 client area. Slots are 0.035 wide and 0.0622 high.
function Slot-Double([int]$i) { @((0.365 + ($i % 9) * 0.035), (0.232 + [Math]::Floor($i / 9) * 0.0622)) }
function Hotbar-Double([int]$i) { @((0.365 + $i * 0.035), 0.819) }
function Slot-Single([int]$i) { @((0.365 + ($i % 9) * 0.035), (0.325 + [Math]::Floor($i / 9) * 0.0622)) }
function Hotbar-Single([int]$i) { @((0.365 + $i * 0.035), 0.726) }
function Click-At($p, [switch]$Shift) {
    if ($Shift) { [AbpE2E.Native]::Key(0x10, $false) }
    Click $p[0] $p[1] -Relative
    if ($Shift) { [AbpE2E.Native]::Key(0x10, $true) }
    Start-Sleep -Milliseconds 600
}

# 1. Open the backpack, go to the upgrades page, shift-click the upgrade in.
$mark = Get-LogMark
Click 0.5 0.5 -Relative -Right
Wait-LogLine 'menu shown: Diamond Backpack \(54 slots\)' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 800
Click-At (Slot-Double 52)
Save-Screenshot 'upgrades-view' | Out-Null
Click-At (Hotbar-Double 1) -Shift
Save-Screenshot 'upgrade-installed' | Out-Null

# 2. Its settings button opens the void settings (single chest: 16 filter slots + controls).
$mark = Get-LogMark
Click-At (Slot-Double 18)
Wait-LogLine 'menu shown: Advanced Void Upgrade \(27 slots\)' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 800
Save-Screenshot 'void-settings' | Out-Null

# 3. Ghost cobblestone into filter slot 0 (the cursor stack stays ours), put it back.
Click-At (Hotbar-Single 2)
Click-At (Slot-Single 0)
Click-At (Hotbar-Single 2)
# Overflow mode (control slot 5) and work in GUI (control slot 6).
Click-At (Slot-Single 23)
Click-At (Slot-Single 24)
Move-Mouse (Slot-Single 23)[0] (Slot-Single 23)[1] -Relative
Start-Sleep -Milliseconds 600
Save-Screenshot 'void-settings-configured' | Out-Null

# 4. Back to the backpack; storage page; put both cobblestone stacks in by hand.
$mark = Get-LogMark
Click-At (Slot-Single 26)
Wait-LogLine 'menu shown: .*\(54 slots\)' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 800
Save-Screenshot 'back-to-upgrades' | Out-Null
Click-At (Slot-Double 52)
Click-At (Hotbar-Double 2) -Shift
Click-At (Hotbar-Double 3) -Shift
Save-Screenshot 'overflow-voided' | Out-Null
Close-Screen

# One stack kept, the overflowing second stack voided.
$mark = Get-LogMark
Invoke-Chat '/backpack info'
$line = Wait-LogLine 'backpack info: ' 10 -Since $mark
if ($line -notmatch ' items=64 ') { throw "expected items=64 (second stack voided), got: $line" }
if ($line -notmatch 'advanced_void') { throw "advanced void upgrade not installed: $line" }
Assert-NoErrors
