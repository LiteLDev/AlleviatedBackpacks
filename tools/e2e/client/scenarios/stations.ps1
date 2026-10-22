# P2-5 stations: the native SB tabs of the Crafting, Stonecutter, Smelting and Auto-smelting
# upgrades, driven with real input in `-Mode local` and `-Mode remote-mod`.
#
# `/backpack stations setup` gives a diamond backpack (Crafting, Stonecutter, Smelting) in hotbar
# slot 1, a gold backpack (Auto-smelting, Tank) in slot 2, and ingredients in the inventory. Tab
# content positions are SC's (CraftingUpgradeTab, BlockConverterRecipeControl, CookingLogicControl,
# AutoCookingUpgradeTab); the backpack layout comes from the mod's "screen layout" log line.

Wait-LogLine 'companion: server ready' 90 | Out-Null
Write-Step 'companion handshake done'

Invoke-Chat '/clear @s'
$mark = Get-LogMark
Invoke-Chat '/backpack stations setup'
Wait-LogLine 'stations setup: gave backpacks' 15 -Since $mark | Out-Null

# ---- helpers ---------------------------------------------------------------------------------------
$script:L = $null
function Read-Layout($mark) {
    $line = Wait-LogLine 'companion: screen layout session=' 15 -Since $mark
    if ($line -notmatch 'left=([\d.]+) top=([\d.]+) width=(\d+) height=(\d+) rows=(\d+)/(\d+) scroll=(\w+) invGuiScale=([\d.]+) screen=([\d.]+)x([\d.]+) .*invLabel=(\d+),(\d+) upgrades=(\d+) tabs=(\d+)') {
        throw "unexpected layout line: $line"
    }
    $script:L = [pscustomobject]@{
        Left = [double]$Matches[1]; Top = [double]$Matches[2]; Width = [int]$Matches[3]; Height = [int]$Matches[4]
        Inv = [double]$Matches[8]; ScreenW = [double]$Matches[9]; ScreenH = [double]$Matches[10]
        InvX = [int]$Matches[11]; InvY = [int]$Matches[12]; Upgrades = [int]$Matches[13]; Tabs = [int]$Matches[14]
    }
    Write-Step ("layout: {0}x{1} at {2},{3} guiScale=1/{4} screen={5}x{6} tabs={7}" -f $L.Width, $L.Height, $L.Left, $L.Top, $L.Inv, $L.ScreenW, $L.ScreenH, $L.Tabs)
}
function Gui-Move([double]$gx, [double]$gy) { Move-Mouse (($L.Left + $gx) / $L.Inv) (($L.Top + $gy) / $L.Inv) }
function Gui-Click([double]$gx, [double]$gy, [switch]$Right) { Click (($L.Left + $gx) / $L.Inv) (($L.Top + $gy) / $L.Inv) -Right:$Right; Start-Sleep -Milliseconds 450 }
function Abs-Click([double]$gx, [double]$gy) { Click ($gx / $L.Inv) ($gy / $L.Inv); Start-Sleep -Milliseconds 450 }
function Abs-Move([double]$gx, [double]$gy) { Move-Mouse ($gx / $L.Inv) ($gy / $L.Inv) }
function With-Shift([scriptblock]$action) {
    Assert-GameForeground
    [AbpE2E.Native]::Key(0x10, $false); Start-Sleep -Milliseconds 80
    try { & $action } finally { [AbpE2E.Native]::Key(0x10, $true); Start-Sleep -Milliseconds 300 }
}
# Player inventory slot k (0-8 hotbar, 9-35 main): centre in GUI units relative to the background.
function Inv-Pos([int]$k) {
    if ($k -lt 9) { return @(($L.InvX + $k * 18 + 8), ($L.InvY + 70 + 8)) }
    $r = [math]::Floor(($k - 9) / 9); $c = ($k - 9) % 9
    @(($L.InvX + $c * 18 + 8), ($L.InvY + 12 + $r * 18 + 8))
}
function Inv-Click([int]$k, [switch]$Right) { $p = Inv-Pos $k; Gui-Click $p[0] $p[1] -Right:$Right }
# Tab i (0 = Backpack Settings): its icon, and points inside it when open.
function Tab-Icon([int]$i) { Gui-Click ($L.Width + 1 + 8) (4 + $i * 25 + 4 + 8) }
function Tab-Click([int]$i, [double]$dx, [double]$dy, [switch]$Right) { Gui-Click ($L.Width + $dx) (4 + $i * 25 + $dy) -Right:$Right }
function Tab-Move([int]$i, [double]$dx, [double]$dy) { Gui-Move ($L.Width + $dx) (4 + $i * 25 + $dy) }
function Expect-TabAction($mark, [string]$what) {
    Wait-LogLine 'native screen action: tab by ' 10 -Since $mark | Out-Null
    Write-Step "tab action: $what"
}
function Open-Backpack([string]$hotbarKey, [string]$title) {
    Start-Sleep -Milliseconds 800
    Press-Key $hotbarKey
    Start-Sleep -Milliseconds 800
    Press-Key $hotbarKey
    Start-Sleep -Milliseconds 600
    $m = Get-LogMark
    Press-Key 'B'
    Wait-LogLine "companion: native screen \d+ for '$title'" 10 -Since $m | Out-Null
    Wait-LogLine 'companion: drawing the native backpack screen' 10 -Since $m | Out-Null
    Read-Layout $m
    Start-Sleep -Milliseconds 700
    if ((Get-LeviLog -Since $m) -match 'menu shown: ') { throw 'a fake chest was opened for a companion player' }
}
function Close-Backpack {
    $m = Get-LogMark
    Press-Key 'E'
    Wait-LogLine 'native screen action: close by' 10 -Since $m | Out-Null
    Start-Sleep -Milliseconds 600
}

# ==== diamond backpack: Crafting (tab 1), Stonecutter (tab 2), Smelting (tab 3) =====================
Open-Backpack '1' 'Diamond Backpack'
if ($L.Tabs -ne 4) { throw "expected the settings tab + 3 upgrade tabs, got $($L.Tabs)" }
Save-Screenshot 'diamond-closed-tabs' | Out-Null
Tab-Move 1 10 12
Start-Sleep -Milliseconds 500
Save-Screenshot 'crafting-closed-tooltip' | Out-Null

# ---- Crafting -------------------------------------------------------------------------------------
$mark = Get-LogMark
Tab-Icon 1
Expect-TabAction $mark 'crafting tab open reported'
Save-Screenshot 'crafting-open' | Out-Null
# Planks on the cursor, two into cells 0, 1, 3, 4 (right clicks), the rest back.
Inv-Click 9
foreach ($cell in 0, 1, 3, 4) {
    $c = $cell % 3; $r = [math]::Floor($cell / 3)
    Tab-Click 1 (12 + $c * 18) (53 + $r * 18) -Right
    Tab-Click 1 (12 + $c * 18) (53 + $r * 18) -Right
}
Inv-Click 9
Start-Sleep -Milliseconds 500
Tab-Move 1 30 124
Start-Sleep -Milliseconds 500
Save-Screenshot 'crafting-result' | Out-Null
# Take one crafting table onto the cursor, then put it into the inventory.
$mark = Get-LogMark
Tab-Click 1 30 124
Expect-TabAction $mark 'result taken'
Tab-Move 1 45 70
Start-Sleep -Milliseconds 400
Save-Screenshot 'crafting-cursor-table' | Out-Null
Inv-Click 20
# Toggle buttons and their SB tooltips.
Tab-Move 1 12 33
Start-Sleep -Milliseconds 500
Save-Screenshot 'crafting-shift-target-tooltip' | Out-Null
$mark = Get-LogMark
Tab-Click 1 30 33
Expect-TabAction $mark 'refill toggled'
Tab-Move 1 30 33
Start-Sleep -Milliseconds 500
Save-Screenshot 'crafting-refill-on' | Out-Null
Tab-Click 1 30 33
# Shift-click the result: the rest is crafted into the backpack.
$mark = Get-LogMark
With-Shift { Tab-Click 1 30 124 }
Expect-TabAction $mark 'shift-click result'
Save-Screenshot 'crafting-shift-crafted' | Out-Null
# Shift-click planks from the inventory: with the tab open they go into the grid.
With-Shift { Inv-Click 9 }   # slot clicks are not logged (silent actions)
Start-Sleep -Milliseconds 700
Save-Screenshot 'crafting-shift-into-grid' | Out-Null
# Shift-click the grid cell back out (into the backpack).
With-Shift { Tab-Click 1 12 53 }
Tab-Icon 1   # close the crafting tab
Start-Sleep -Milliseconds 400

# ---- Stonecutter ----------------------------------------------------------------------------------
$mark = Get-LogMark
Tab-Icon 2
Expect-TabAction $mark 'stonecutter tab open reported'
Save-Screenshot 'stonecutter-open' | Out-Null
# Shift-click cobbled deepslate from the inventory: into the input (open tab first).
With-Shift { Inv-Click 10 }
Start-Sleep -Milliseconds 600
Save-Screenshot 'stonecutter-recipes' | Out-Null
# Pick the second recipe, hover it.
$mark = Get-LogMark
Tab-Click 2 (12 + 16) 78
Expect-TabAction $mark 'recipe selected'
Tab-Move 2 (12 + 16) 78
Start-Sleep -Milliseconds 500
Save-Screenshot 'stonecutter-selected' | Out-Null
# Scroll the list.
Tab-Move 2 40 90
Scroll -2
Start-Sleep -Milliseconds 500
Save-Screenshot 'stonecutter-scrolled' | Out-Null
Scroll 2
# Take one result onto the cursor and drop it into the inventory.
$mark = Get-LogMark
Tab-Click 2 36 141
Expect-TabAction $mark 'one cut onto the cursor'
Inv-Click 21
# Expanded output list with search (cobbled deepslate has more than 12 results).
Tab-Move 2 77 75
Start-Sleep -Milliseconds 500
Save-Screenshot 'stonecutter-browse-tooltip' | Out-Null
$mark = Get-LogMark
Tab-Click 2 77 75
Wait-LogLine 'stonecutter expanded output list opened' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 400
Save-Screenshot 'stonecutter-expanded' | Out-Null
Type-Text 'tile'
Start-Sleep -Milliseconds 600
Save-Screenshot 'stonecutter-expanded-search' | Out-Null
$mx = [math]::Floor(($L.ScreenW - 176) / 2); $my = [math]::Floor(($L.ScreenH - 122) / 2)
$mark = Get-LogMark
Abs-Click ($mx + 8 + 8) ($my + 24 + 1 + 8)
Expect-TabAction $mark 'recipe picked in the expanded list'
Start-Sleep -Milliseconds 400
Save-Screenshot 'stonecutter-picked' | Out-Null
# Shift-click the output: the rest of the input is cut into the backpack.
$mark = Get-LogMark
With-Shift { Tab-Click 2 36 141 }
Expect-TabAction $mark 'shift-cut'
Save-Screenshot 'stonecutter-after-shift' | Out-Null
Tab-Icon 2

# ---- Smelting -------------------------------------------------------------------------------------
$mark = Get-LogMark
Tab-Icon 3
Expect-TabAction $mark 'smelting tab open reported'
With-Shift { Inv-Click 11 }   # raw iron -> input
With-Shift { Inv-Click 12 }   # coal -> fuel
Start-Sleep -Milliseconds 800
Save-Screenshot 'smelting-loaded' | Out-Null
Start-Sleep -Seconds 4
Save-Screenshot 'smelting-progress' | Out-Null
Start-Sleep -Seconds 8
Tab-Move 3 58 51
Start-Sleep -Milliseconds 500
Save-Screenshot 'smelting-output' | Out-Null
$mark = Get-LogMark
Tab-Click 3 58 51
Expect-TabAction $mark 'output taken'
Inv-Click 22

# ==== gold backpack: Auto-smelting (tab 1) =============================================================
# B over the gold backpack (hotbar slot 2) opens it from the diamond backpack's screen.
$p = Inv-Pos 1
Gui-Move $p[0] $p[1]
Start-Sleep -Milliseconds 400
$m = Get-LogMark
Press-Key 'B'
Wait-LogLine "companion: native screen \d+ for 'Gold Backpack'" 10 -Since $m | Out-Null
Read-Layout $m
Start-Sleep -Milliseconds 700
$mark = Get-LogMark
Tab-Icon 1
Expect-TabAction $mark 'auto-smelting tab open reported'
Start-Sleep -Milliseconds 500
Save-Screenshot 'auto-smelting-open' | Out-Null
Start-Sleep -Seconds 6
Save-Screenshot 'auto-smelting-running' | Out-Null
# Allow / Block and the primary match buttons; filter slot from the cursor.
Tab-Move 1 12 33
Start-Sleep -Milliseconds 400
Save-Screenshot 'auto-allow-tooltip' | Out-Null
$mark = Get-LogMark
Tab-Click 1 30 33
Expect-TabAction $mark 'primary match -> mod'
Tab-Click 1 30 33
Start-Sleep -Milliseconds 500
Save-Screenshot 'auto-match-tags' | Out-Null
Tab-Click 1 30 33   # back to Match Item
Inv-Click 13
$mark = Get-LogMark
Tab-Click 1 (12 + 18) 54
Expect-TabAction $mark 'input filter slot set'
Inv-Click 13
Tab-Move 1 (12 + 18) 54
Start-Sleep -Milliseconds 500
Save-Screenshot 'auto-filter-set' | Out-Null
Close-Backpack

Assert-NoErrors
