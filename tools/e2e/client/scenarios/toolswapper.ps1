# U12 Tool Swapper: selftests in a local world, then the real thing driven by real input:
# install an Advanced Tool Swapper through the backpack GUI, open its settings menu, and mine a
# stone block while holding cobblestone -> the diamond pickaxe from the backpack is swapped in.
$r = Invoke-SelfTest 'toolswapper.*'
if ($r.Fail -gt 0 -or $r.Pass -lt 10) { throw "toolswapper selftests: pass=$($r.Pass) fail=$($r.Fail)" }

Invoke-Chat '/gamemode survival @s'
Invoke-Chat '/clear @s'
Invoke-Chat '/replaceitem entity @s slot.hotbar 0 alleviatedbackpacks:diamond_backpack'
Invoke-Chat '/replaceitem entity @s slot.hotbar 1 alleviatedbackpacks:advanced_tool_swapper_upgrade'
Invoke-Chat '/replaceitem entity @s slot.hotbar 2 diamond_pickaxe'
Invoke-Chat '/replaceitem entity @s slot.hotbar 3 cobblestone 10'
Press-Key '1'
Start-Sleep -Seconds 1

# Fractions of the 1600x900 client area (double chest layout, see foundation.ps1).
$col0 = 0.365; $colW = 0.035      # x of column 0 / column pitch
$row0 = 0.232; $rowH = 0.0622     # y of chest row 0 / row pitch
$hotY = 0.819                      # y of the hotbar row in the chest screen
function MenuSlot([int]$i) { @(($col0 + ($i % 9) * $colW), ($row0 + [Math]::Floor($i / 9) * $rowH)) }
function HotbarSlot([int]$i) { @(($col0 + $i * $colW), $hotY) }

# 1. Open the backpack, shift-click the pickaxe in.
$mark = Get-LogMark
Click 0.5 0.5 -Relative -Right
Wait-LogLine 'menu shown: Diamond Backpack \(54 slots\)' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 800
$p = HotbarSlot 2
[AbpE2E.Native]::Key(0x10, $false); Click $p[0] $p[1] -Relative; [AbpE2E.Native]::Key(0x10, $true)
Start-Sleep -Milliseconds 600

# 2. Upgrades page; put the upgrade into the first upgrade slot.
$p = MenuSlot 52; Click $p[0] $p[1] -Relative
Start-Sleep -Milliseconds 800
$p = HotbarSlot 1; Click $p[0] $p[1] -Relative
$p = MenuSlot 0;   Click $p[0] $p[1] -Relative
Start-Sleep -Milliseconds 800
# Leave and re-enter the upgrades page so its toggle / settings buttons are rebuilt.
$p = MenuSlot 52; Click $p[0] $p[1] -Relative
Start-Sleep -Milliseconds 500
Click $p[0] $p[1] -Relative
Start-Sleep -Milliseconds 800
Save-Screenshot 'upgrade-installed' | Out-Null

# 3. Its settings menu (the repeater below the upgrade).
$mark = Get-LogMark
$p = MenuSlot 18; Click $p[0] $p[1] -Relative
Wait-LogLine 'menu shown: Advanced Tool Swapper Upgrade \(27 slots\)' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 900
Save-Screenshot 'settings-menu' | Out-Null
Close-Screen
Start-Sleep -Milliseconds 800

# The backpack now holds the pickaxe and an advanced tool swapper.
$mark = Get-LogMark
Invoke-Chat '/backpack info'
$line = Wait-LogLine 'backpack info: ' 10 -Since $mark
if ($line -notmatch ' items=1 ' -or $line -notmatch 'advanced_tool_swapper') { throw "backpack not set up: $line" }

# 4. Hold cobblestone, look straight down at a stone block and mine it.
Invoke-Chat '/setblock ~ ~-1 ~ stone'
Invoke-Chat '/tp @s ~ ~ ~ 0 90'
Press-Key '4'
Start-Sleep -Milliseconds 600
Save-Screenshot 'before-mining' | Out-Null
Assert-GameForeground
[AbpE2E.Native]::MouseButton(0, $false); Start-Sleep -Milliseconds 2500; [AbpE2E.Native]::MouseButton(0, $true)
Start-Sleep -Milliseconds 800
Save-Screenshot 'after-mining' | Out-Null

# The pickaxe left the backpack and the cobblestone took its slot.
Press-Key '1'
Start-Sleep -Milliseconds 400
$mark = Get-LogMark
Invoke-Chat '/backpack info'
$line = Wait-LogLine 'backpack info: ' 10 -Since $mark
if ($line -notmatch ' items=10 ') { throw "expected the 10 cobblestone in the backpack after the swap, got: $line" }
Assert-NoErrors
