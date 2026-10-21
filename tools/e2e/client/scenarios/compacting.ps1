# U8 Compacting upgrade: selftests in a local world, then the real GUI: install an Advanced
# Compacting Upgrade by hand, open its settings, and shift-click iron ingots into the backpack.
$r = Invoke-SelfTest 'compacting.*'
if ($r.Fail -gt 0 -or $r.Pass -lt 6) { throw "compacting selftests: pass=$($r.Pass) fail=$($r.Fail)" }

Invoke-Chat '/clear @s'
Invoke-Chat '/replaceitem entity @s slot.hotbar 0 alleviatedbackpacks:diamond_backpack'
Invoke-Chat '/replaceitem entity @s slot.hotbar 1 alleviatedbackpacks:advanced_compacting_upgrade'
Invoke-Chat '/replaceitem entity @s slot.hotbar 2 iron_ingot 64'
Press-Key '1'
Start-Sleep -Seconds 1

# Double chest geometry (fractions of the 1600x900 client area): 56 px slots.
$x0 = 0.365; $dx = 0.035
$y0 = 0.232; $dy = 0.0622
function Menu-Slot([int]$i) { return @(($x0 + ($i % 9) * $dx), ($y0 + [math]::Floor($i / 9) * $dy)) }
$hotbarY = 0.819
function Hotbar([int]$i) { return @(($x0 + $i * $dx), $hotbarY) }

function Open-Backpack([string]$shot) {
    $mark = Get-LogMark
    Click 0.5 0.5 -Relative -Right
    Wait-LogLine 'menu shown: Diamond Backpack \(54 slots\)' 10 -Since $mark | Out-Null
    Start-Sleep -Milliseconds 800
    Save-Screenshot $shot | Out-Null
}
function Get-BackpackInfo {
    $mark = Get-LogMark
    Invoke-Chat '/backpack info'
    return Wait-LogLine 'backpack info: ' 10 -Since $mark
}

# 1. Open the backpack and switch to the upgrades page.
Open-Backpack 'storage'
$p = Menu-Slot 52; Click $p[0] $p[1] -Relative
Start-Sleep -Milliseconds 800
Save-Screenshot 'upgrades-page' | Out-Null

# 2. Pick the upgrade up from the hotbar and put it into upgrade slot 0.
$p = Hotbar 1; Click $p[0] $p[1] -Relative
Start-Sleep -Milliseconds 400
$p = Menu-Slot 0; Click $p[0] $p[1] -Relative
Start-Sleep -Milliseconds 800
Save-Screenshot 'upgrade-installed' | Out-Null

# 3. Open its settings (filter + "only uncraftable" toggle). The upgrades page does not redraw its
#    toggle / settings buttons right after an install, so switch to storage and back first.
$p = Menu-Slot 45; Click $p[0] $p[1] -Relative
Start-Sleep -Milliseconds 600
$p = Menu-Slot 52; Click $p[0] $p[1] -Relative
Start-Sleep -Milliseconds 800
Save-Screenshot 'upgrades-page-installed' | Out-Null
$mark = Get-LogMark
$p = Menu-Slot 18; Click $p[0] $p[1] -Relative
Wait-LogLine 'menu shown: Advanced Compacting Upgrade \(54 slots\)' 15 -Since $mark | Out-Null
Start-Sleep -Milliseconds 1200
Save-Screenshot 'settings' | Out-Null
# Toggle "only uncraftable" (control row, 6th button), then go back to the backpack.
$p = Menu-Slot 50; Click $p[0] $p[1] -Relative
Start-Sleep -Milliseconds 800
Save-Screenshot 'settings-toggled' | Out-Null
$mark = Get-LogMark
$p = Menu-Slot 53; Click $p[0] $p[1] -Relative
Wait-LogLine 'menu shown: Diamond Backpack \(54 slots\)' 15 -Since $mark | Out-Null
Start-Sleep -Milliseconds 1000
Save-Screenshot 'back-to-upgrades' | Out-Null
Close-Screen

$info = Get-BackpackInfo
if ($info -notmatch 'upgrades=\[advanced_compacting\]') { throw "upgrade not installed: $info" }
# Only uncraftable is off now: iron ingots still compact (3x3 wins over the 2x2 trapdoor).

# 4. Shift-click 64 iron ingots into the backpack: they become 7 blocks + 1 ingot.
Open-Backpack 'storage-before'
[AbpE2E.Native]::Key(0x10, $false)
$p = Hotbar 2; Click $p[0] $p[1] -Relative
[AbpE2E.Native]::Key(0x10, $true)
Start-Sleep -Milliseconds 800
Save-Screenshot 'compacted' | Out-Null
Close-Screen
$info = Get-BackpackInfo
if ($info -notmatch ' items=8 ') { throw "expected 7 iron blocks + 1 ingot (items=8), got: $info" }
Assert-NoErrors
