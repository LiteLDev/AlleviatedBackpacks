# U7 Feeding upgrade in a real local world: the selftests, then a real player installs an Advanced
# Feeding upgrade through the GUI, edits its settings with real clicks and gets fed while hungry.
$r = Invoke-SelfTest 'feeding.*'
if ($r.Fail -gt 0 -or $r.Pass -lt 17) { throw "feeding selftests: pass=$($r.Pass) fail=$($r.Fail)" }

Invoke-Chat '/gamemode survival'
Invoke-Chat '/difficulty easy'
Invoke-Chat '/clear @s'
Invoke-Chat '/replaceitem entity @s slot.hotbar 0 alleviatedbackpacks:diamond_backpack'
Invoke-Chat '/replaceitem entity @s slot.hotbar 1 alleviatedbackpacks:advanced_feeding_upgrade'
Invoke-Chat '/replaceitem entity @s slot.hotbar 2 cooked_beef 16'
Press-Key '1'
Start-Sleep -Seconds 1

# Fractions of the 1600x900 client area. Slots are 0.035 wide and 0.0622 high.
function Slot-Double([int]$i) { @((0.365 + ($i % 9) * 0.035), (0.232 + [Math]::Floor($i / 9) * 0.0622)) }
function Hotbar-Double([int]$i) { @((0.365 + $i * 0.035), 0.819) }
function Slot-Single([int]$i) { @((0.365 + ($i % 9) * 0.035), (0.325 + [Math]::Floor($i / 9) * 0.0622)) }
function Click-At($p, [switch]$Shift) {
    if ($Shift) { [AbpE2E.Native]::Key(0x10, $false) }
    Click $p[0] $p[1] -Relative
    if ($Shift) { [AbpE2E.Native]::Key(0x10, $true) }
    Start-Sleep -Milliseconds 600
}
function Get-BackpackInfo {
    $mark = Get-LogMark
    Invoke-Chat '/backpack info'
    $line = Wait-LogLine 'backpack info: ' 10 -Since $mark
    if ($line -notmatch ' items=(\d+)') { throw "no item count in: $line" }
    return @{ Line = $line; Items = [int]$Matches[1] }
}

# 1. Open the backpack, shift-click the steak in; upgrades page, shift-click the upgrade in.
$mark = Get-LogMark
Click 0.5 0.5 -Relative -Right
Wait-LogLine 'menu shown: Diamond Backpack \(54 slots\)' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 800
Click-At (Hotbar-Double 2) -Shift
Click-At (Slot-Double 52)
Click-At (Hotbar-Double 1) -Shift
Move-Mouse (Slot-Double 0)[0] (Slot-Double 0)[1] -Relative
Start-Sleep -Milliseconds 600
Save-Screenshot 'upgrade-installed' | Out-Null

# 2. Its settings button opens the feeding settings (single chest: 16 filter slots + controls).
$mark = Get-LogMark
Click-At (Slot-Double 18)
Wait-LogLine 'menu shown: Advanced Feeding Upgrade \(27 slots\)' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 800
Save-Screenshot 'settings' | Out-Null

# 3. Hunger mode (control slot 5): Half -> Full -> Always; hover it for the tooltip.
Click-At (Slot-Single 23)
Click-At (Slot-Single 23)
Move-Mouse (Slot-Single 23)[0] (Slot-Single 23)[1] -Relative
Start-Sleep -Milliseconds 600
Save-Screenshot 'settings-mode-always' | Out-Null

# 4. Back to the backpack, close it.
$mark = Get-LogMark
Click-At (Slot-Single 26)
Wait-LogLine 'menu shown: .*\(54 slots\)' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 800
Save-Screenshot 'back-to-upgrades' | Out-Null
Close-Screen
Start-Sleep -Milliseconds 500

$info = Get-BackpackInfo
if ($info.Items -ne 16) { throw "expected 16 steak in the backpack: $($info.Line)" }
if ($info.Line -notmatch 'advanced_feeding') { throw "advanced feeding upgrade not installed: $($info.Line)" }

# 5. Get hungry: the backpack feeds the player.
Invoke-Chat '/effect @s hunger 8 250'
Start-Sleep -Seconds 10
Save-Screenshot 'fed' | Out-Null
$info = Get-BackpackInfo
if ($info.Items -ge 16) { throw "expected the feeding upgrade to eat steak: $($info.Line)" }
Assert-NoErrors
