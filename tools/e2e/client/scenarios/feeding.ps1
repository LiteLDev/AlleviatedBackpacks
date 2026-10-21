# U7 Feeding upgrade in a real local world: the selftests, then a real player installs an Advanced
# Feeding upgrade through the GUI, edits its settings with real clicks and gets fed while hungry.
$r = Invoke-SelfTest 'feeding.*'
if ($r.Fail -gt 0 -or $r.Pass -lt 16) { throw "feeding selftests: pass=$($r.Pass) fail=$($r.Fail)" }

Invoke-Chat '/gamemode survival'
Invoke-Chat '/difficulty easy'
Invoke-Chat '/clear @s'
Invoke-Chat '/replaceitem entity @s slot.hotbar 0 alleviatedbackpacks:diamond_backpack'
Invoke-Chat '/replaceitem entity @s slot.hotbar 1 alleviatedbackpacks:advanced_feeding_upgrade'
Invoke-Chat '/replaceitem entity @s slot.hotbar 2 cooked_beef 16'
Press-Key '1'
Start-Sleep -Seconds 1

# Double chest (backpack GUI) geometry, fractions of the 1600x900 client area.
$x0 = 0.365; $dx = 0.035; $y0 = 0.232; $dy = 0.0622
function Big([int]$slot) { @(($x0 + ($slot % 9) * $dx), ($y0 + [math]::Floor($slot / 9) * $dy)) }
$hotbarY = 0.819
function Hotbar([int]$i) { @(($x0 + $i * $dx), $hotbarY) }

function Assert-BackpackItems([scriptblock]$check, [string]$what) {
    $mark = Get-LogMark
    Invoke-Chat '/backpack info'
    $line = Wait-LogLine 'backpack info: ' 10 -Since $mark
    if ($line -notmatch ' items=(\d+)') { throw "no item count in: $line" }
    if (-not (& $check ([int]$Matches[1]))) { throw "$what (got: $line)" }
    return $line
}

# 1. Open the backpack, shift-click the steak in.
$mark = Get-LogMark
Click 0.5 0.5 -Relative -Right
Wait-LogLine 'menu shown: Diamond Backpack \(54 slots\)' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 800
$h = Hotbar 2
[AbpE2E.Native]::Key(0x10, $false)
Click $h[0] $h[1] -Relative
[AbpE2E.Native]::Key(0x10, $true)
Start-Sleep -Milliseconds 600

# 2. Upgrades view: put the upgrade into the first upgrade slot.
$b = Big 52
Click $b[0] $b[1] -Relative
Start-Sleep -Milliseconds 800
$h = Hotbar 1
Click $h[0] $h[1] -Relative
Start-Sleep -Milliseconds 400
$s = Big 0
Click $s[0] $s[1] -Relative
Start-Sleep -Milliseconds 800
Save-Screenshot 'upgrades-installed' | Out-Null

# 3. Open its settings (button under the upgrade): the feeding filter/settings menu.
$mark = Get-LogMark
$s = Big 18
Click $s[0] $s[1] -Relative
Wait-LogLine 'menu shown: Advanced Feeding Upgrade \(27 slots\)' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 800
Save-Screenshot 'settings' | Out-Null

# Single chest geometry: same columns, rows shifted down by half of the 3 missing rows.
$sy0 = $y0 + 1.5 * $dy
function Small([int]$slot) { @(($x0 + ($slot % 9) * $dx), ($sy0 + [math]::Floor($slot / 9) * $dy)) }
# Hunger mode button (control row, slot 5): Half -> Full -> Always.
$m = Small 23
Click $m[0] $m[1] -Relative
Start-Sleep -Milliseconds 500
Click $m[0] $m[1] -Relative
Start-Sleep -Milliseconds 700
Move-Mouse $m[0] $m[1] -Relative
Start-Sleep -Milliseconds 500
Save-Screenshot 'settings-mode-always' | Out-Null
Close-Screen
Start-Sleep -Milliseconds 500

$line = Assert-BackpackItems { param($n) $n -eq 16 } 'expected 16 steak in the backpack'
if ($line -notmatch 'advanced_feeding') { throw "upgrade not installed: $line" }

# 4. Get hungry; the backpack feeds the player.
Invoke-Chat '/effect @s hunger 8 250'
Start-Sleep -Seconds 10
Save-Screenshot 'fed' | Out-Null
Assert-BackpackItems { param($n) $n -lt 16 } 'expected the feeding upgrade to eat steak' | Out-Null
Assert-NoErrors
