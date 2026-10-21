# U4 Stack upgrades: selftests in a local world, then real clicks: install a Stack Upgrade Tier 1
# in the GUI, swap it for Tier 2, shift-click 192 cobblestone into one slot, check the capped display + lore, take
# one vanilla stack out of that slot.
$r = Invoke-SelfTest 'stack.*'
if ($r.Fail -gt 0 -or $r.Pass -lt 9) { throw "stack selftests: pass=$($r.Pass) fail=$($r.Fail)" }

Invoke-Chat '/clear @s'
Invoke-Chat '/replaceitem entity @s slot.hotbar 0 alleviatedbackpacks:leather_backpack'
Invoke-Chat '/replaceitem entity @s slot.hotbar 1 alleviatedbackpacks:stack_tier_1_upgrade'
Invoke-Chat '/replaceitem entity @s slot.hotbar 2 cobblestone 64'
Invoke-Chat '/replaceitem entity @s slot.hotbar 3 cobblestone 64'
Invoke-Chat '/replaceitem entity @s slot.hotbar 4 cobblestone 64'
Invoke-Chat '/replaceitem entity @s slot.hotbar 5 alleviatedbackpacks:stack_tier_2_upgrade'
Press-Key '1'
Start-Sleep -Seconds 1

function Open-Backpack([string]$shot) {
    $mark = Get-LogMark
    Click 0.5 0.5 -Relative -Right
    Wait-LogLine 'menu shown: Leather Backpack \(54 slots\)' 10 -Since $mark | Out-Null
    Start-Sleep -Milliseconds 800
    Save-Screenshot $shot | Out-Null
}
function Get-BackpackInfo {
    $mark = Get-LogMark
    Invoke-Chat '/backpack info'
    return Wait-LogLine 'backpack info: ' 10 -Since $mark
}
function Invoke-ShiftClick([double]$x, [double]$y) {
    [AbpE2E.Native]::Key(0x10, $false)
    Click $x $y -Relative
    [AbpE2E.Native]::Key(0x10, $true)
    Start-Sleep -Milliseconds 600
}

# Fractions of the 1600x900 client area (double chest layout, see foundation.ps1).
$colX  = 0.365; $colW = 0.035   # column 0 and pitch (menu and player inventory)
$rowY0 = 0.232; $rowH = 0.0622  # menu row 0 and pitch
function MenuSlot([int]$i) { return @(($colX + ($i % 9) * $colW), ($rowY0 + [math]::Floor($i / 9) * $rowH)) }
function Hotbar([int]$i) { return @(($colX + $i * $colW), 0.819) }
$invSlot = @(0.400, 0.632)

Open-Backpack 'stack-menu-open'

# 1. Upgrades page (button in menu slot 52), pick up the upgrade, put it into upgrade slot 0.
$b = MenuSlot 52; Click $b[0] $b[1] -Relative; Start-Sleep -Milliseconds 800
Save-Screenshot 'stack-upgrades-page' | Out-Null
$h = Hotbar 1; Click $h[0] $h[1] -Relative; Start-Sleep -Milliseconds 400
$u = MenuSlot 0; Click $u[0] $u[1] -Relative; Start-Sleep -Milliseconds 800
Move-Mouse $u[0] $u[1] -Relative; Start-Sleep -Milliseconds 800
Save-Screenshot 'stack-upgrade-installed' | Out-Null
Close-Screen
$info = Get-BackpackInfo
if ($info -notmatch 'upgrades=\[stack_tier_1\]') { throw "upgrade not installed: $info" }

# 1b. Swap: Tier 2 on the cursor, click the installed Tier 1 -> Tier 2 in, Tier 1 on the cursor.
Open-Backpack 'stack-menu-swap'
$b = MenuSlot 52; Click $b[0] $b[1] -Relative; Start-Sleep -Milliseconds 800
$h5 = Hotbar 5; Click $h5[0] $h5[1] -Relative; Start-Sleep -Milliseconds 400
Click $u[0] $u[1] -Relative; Start-Sleep -Milliseconds 900
Save-Screenshot 'stack-swapped-cursor-tier1' | Out-Null
Click $h5[0] $h5[1] -Relative; Start-Sleep -Milliseconds 600
Close-Screen
$info = Get-BackpackInfo
if ($info -notmatch 'upgrades=\[stack_tier_2\]') { throw "swap to tier 2 failed: $info" }

# 2. Shift-click the three cobblestone stacks in: all 192 fit into slot 0 (limit 256).
Open-Backpack 'stack-menu-reopen'
foreach ($i in 2, 3, 4) { $h = Hotbar $i; Invoke-ShiftClick $h[0] $h[1] }
$s0 = MenuSlot 0
Move-Mouse $s0[0] $s0[1] -Relative; Start-Sleep -Milliseconds 900
Save-Screenshot 'stack-192-in-one-slot' | Out-Null
Close-Screen
$info = Get-BackpackInfo
if ($info -notmatch ' items=192 ') { throw "expected 192 items: $info" }

# 3. Take from the 192 slot: one vanilla stack (64) comes out.
Open-Backpack 'stack-menu-third'
Click $s0[0] $s0[1] -Relative; Start-Sleep -Milliseconds 600
Save-Screenshot 'stack-cursor-64' | Out-Null
Click $invSlot[0] $invSlot[1] -Relative; Start-Sleep -Milliseconds 800
Move-Mouse $s0[0] $s0[1] -Relative; Start-Sleep -Milliseconds 900
Save-Screenshot 'stack-after-take' | Out-Null
Close-Screen
$info = Get-BackpackInfo
if ($info -notmatch ' items=128 ') { throw "expected 128 items after taking one stack: $info" }
Assert-NoErrors
