# U6 Magnet upgrade: selftests in a local world (they also drive the settings menu through
# MenuManager::simulate), then install an Advanced Magnet through the real backpack GUI and let it
# collect items the player drops.
#
# The settings chest itself is not opened here: the real client doesn't display a single-chest
# (27 slot) menu opened from the backpack GUI (foundation MenuManager issue, see the PR).
$r = Invoke-SelfTest 'magnet.*'
if ($r.Fail -gt 0 -or $r.Pass -lt 10) { throw "magnet selftests: pass=$($r.Pass) fail=$($r.Fail)" }

Invoke-Chat '/clear @s'
Invoke-Chat '/replaceitem entity @s slot.hotbar 0 alleviatedbackpacks:diamond_backpack'
Invoke-Chat '/replaceitem entity @s slot.hotbar 1 alleviatedbackpacks:advanced_magnet_upgrade'
Invoke-Chat '/replaceitem entity @s slot.hotbar 2 cobblestone 16'
Press-Key '1'
Start-Sleep -Seconds 1
Save-Screenshot 'holding' | Out-Null

function Get-BackpackInfo {
    $mark = Get-LogMark
    Invoke-Chat '/backpack info'
    return Wait-LogLine 'backpack info: ' 10 -Since $mark
}

# Fractions of the 1600x900 client area for the double chest (see foundation.ps1).
$col0 = 0.365; $dx = 0.035; $row0 = 0.232; $dy = 0.0622
function Slot-Pos([int]$slot) { return @(($col0 + ($slot % 9) * $dx), ($row0 + [Math]::Floor($slot / 9) * $dy)) }
$hotbar1 = @(0.400, 0.819)

# 1. Open the backpack, switch to the upgrades page.
$mark = Get-LogMark
Click 0.5 0.5 -Relative -Right
Wait-LogLine 'menu shown: Diamond Backpack \(54 slots\)' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 800
$p = Slot-Pos 52
Click $p[0] $p[1] -Relative
Start-Sleep -Milliseconds 800
Save-Screenshot 'upgrades-page' | Out-Null

# 2. Pick up the magnet from the hotbar and put it into upgrade slot 0 (hovered: shows its lore).
Click $hotbar1[0] $hotbar1[1] -Relative
Start-Sleep -Milliseconds 500
$p = Slot-Pos 0
Click $p[0] $p[1] -Relative
Start-Sleep -Milliseconds 800
Save-Screenshot 'magnet-installed' | Out-Null
Close-Screen
Start-Sleep -Milliseconds 500

$info = Get-BackpackInfo
if ($info -notmatch 'upgrades=\[advanced_magnet\]') { throw "magnet not installed: $info" }

# 3. Drop cobblestone; the magnet (or the pickup path) puts it into the backpack.
Press-Key '3'
Start-Sleep -Milliseconds 300
for ($i = 0; $i -lt 5; $i++) { Press-Key 'Q'; Start-Sleep -Milliseconds 250 }
Press-Key '1'
Start-Sleep -Seconds 1
Save-Screenshot 'dropped' | Out-Null
Start-Sleep -Seconds 6
Save-Screenshot 'collected' | Out-Null
$info = Get-BackpackInfo
if ($info -notmatch ' items=5 ') { throw "expected the 5 dropped cobblestone in the backpack: $info" }
Assert-NoErrors
