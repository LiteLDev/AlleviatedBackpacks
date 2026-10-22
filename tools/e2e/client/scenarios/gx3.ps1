# GX-3: GUI, settings and input gaps (client). Works in `-Mode local` and `-Mode remote-mod`.
#   - vanilla inventory: long upgrade tooltip lines wrap (NeoForge rule), a backpack clicked back
#     into its slot is placed there (not equipped), right-click stashing in the 2x2 crafting grid,
#     the dye craft's output preview carries the computed colours and the source backpack;
#   - crafting table: stashing in the 3x3 grid, the tier upgrade preview (contents tooltip) and
#     the crafted backpack keeping its storage;
#   - furnace: stashing from the result and fuel slots;
#   - smithing table: Diamond -> Netherite backpack without durability, with the preview;
#   - native screen: the carried item is drawn below the sort buttons, sort / transfer keys play
#     the click sound, the open tab is kept on the storage (server side).
# Positions: vanilla slots from "stash: vanilla slots ..." (station slots are "vanilla.<container>:<i>",
# result previews "out.<collection>:<i>"), the native screen from its layout line.

$ContainerCraftingInput = 13; $ContainerFurnaceFuel = 24; $ContainerFurnaceResult = 26
$ContainerSmithingInput = 3; $ContainerSmithingMaterial = 4; $ContainerSmithingTemplate = 61

function Middle-Click { [AbpE2E.Native]::MouseButton(2, $false); Start-Sleep -Milliseconds 60; [AbpE2E.Native]::MouseButton(2, $true); Start-Sleep -Milliseconds 250 }
function Tap-Vk([int]$vk) { Assert-GameForeground; [AbpE2E.Native]::Key([uint16]$vk, $false); Start-Sleep -Milliseconds 80; [AbpE2E.Native]::Key([uint16]$vk, $true); Start-Sleep -Milliseconds 300 }
function Hold([int]$vk) { Assert-GameForeground; [AbpE2E.Native]::Key([uint16]$vk, $false) }
function Release([int]$vk) { [AbpE2E.Native]::Key([uint16]$vk, $true); Start-Sleep -Milliseconds 100 }

Wait-LogLine 'companion: server ready' 90 | Out-Null

# ---- native screen layout ----------------------------------------------------------------------------
$script:L = $null
function Read-Layout($mark) {
    $line = Wait-LogLine 'companion: screen layout session=' 15 -Since $mark
    if ($line -notmatch 'left=([\d.]+) top=([\d.]+) width=(\d+) height=(\d+) rows=(\d+)/(\d+) scroll=(\w+) invGuiScale=([\d.]+) screen=([\d.]+)x([\d.]+) invLabel=(\d+),(\d+) upgrades=(\d+) tabs=(\d+) slots=(\d+) cols=(\d+) sort=(-?\d+),(-?\d+)') {
        throw "unexpected layout line: $line"
    }
    $script:L = [pscustomobject]@{
        Left = [double]$Matches[1]; Top = [double]$Matches[2]; Width = [int]$Matches[3]; Inv = [double]$Matches[8]
        InvX = [int]$Matches[11]; InvY = [int]$Matches[12]; Tabs = [int]$Matches[14]; Cols = [int]$Matches[16]
        SortX = [int]$Matches[17]; SortY = [int]$Matches[18]
    }
}
function Gui-Move([double]$gx, [double]$gy) { Move-Mouse (($L.Left + $gx) / $L.Inv) (($L.Top + $gy) / $L.Inv) }
function Gui-Click([double]$gx, [double]$gy) { Gui-Move $gx $gy; Start-Sleep -Milliseconds 200; Click (($L.Left + $gx) / $L.Inv) (($L.Top + $gy) / $L.Inv); Start-Sleep -Milliseconds 400 }
function Storage-Pos([int]$i) { @((8 + ($i % $L.Cols) * 18 + 8), (18 + [math]::Floor($i / $L.Cols) * 18 + 8)) }
function Hotbar-Pos([int]$i) { @(($L.InvX + $i * 18 + 8), ($L.InvY + 12 + 54 + 4 + 8)) }
function Tab-Pos([int]$i) { @(($L.Width + 1 + 8), (4 + $i * 25 + 4 + 8)) }

# ---- vanilla screen slots ---------------------------------------------------------------------------
$script:V = $null
function Read-VanillaSlots($mark) {
    Wait-LogLine 'stash: vanilla slots screen=' 15 -Since $mark | Out-Null
    Start-Sleep -Milliseconds 1500   # logged again while the screen settles: take the last
    $line = (Get-LeviLog -Since $mark | Select-String -Pattern 'stash: vanilla slots screen=' | Select-Object -Last 1).Line
    if ($line -notmatch 'screen=(\S*) invGuiScale=([\d.]+)') { throw "unexpected slots line: $line" }
    $slots = @{}
    foreach ($m in [regex]::Matches($line, '([\w.]+:\d+)=(-?[\d.]+),(-?[\d.]+),([\d.]+)')) {
        $slots[$m.Groups[1].Value] = @([double]$m.Groups[2].Value, [double]$m.Groups[3].Value, [double]$m.Groups[4].Value)
    }
    $script:V = [pscustomobject]@{ Screen = $Matches[1]; Inv = [double]$Matches[2]; Slots = $slots }
    Write-Step ("vanilla screen {0}: {1} slots ({2})" -f $V.Screen, $slots.Count, (($slots.Keys | Sort-Object) -join ' '))
}
function V-Pos([string]$key) {
    if (-not $V.Slots.ContainsKey($key)) { throw "vanilla slot $key not seen on $($V.Screen)" }
    $r = $V.Slots[$key]
    @((($r[0] + 8 * $r[2]) / $V.Inv), (($r[1] + 8 * $r[2]) / $V.Inv))
}
function V-Out { $k = $V.Slots.Keys | Where-Object { $_ -like 'out.*' } | Select-Object -First 1; if (-not $k) { throw "no result slot seen on $($V.Screen)" }; $k }
function V-Move([string]$key) { $p = V-Pos $key; Move-Mouse $p[0] $p[1] }
function V-MoveFrom([string]$key, [int]$dx, [int]$dy) { $p = V-Pos $key; Move-Mouse ($p[0] + $dx * 18 / $V.Inv) ($p[1] + $dy * 18 / $V.Inv) }
function V-Click([string]$key, [switch]$Right) {
    $p = V-Pos $key; Move-Mouse ($p[0] + 3) ($p[1] + 3); Start-Sleep -Milliseconds 150; Move-Mouse $p[0] $p[1]; Start-Sleep -Milliseconds 250
    $b = if ($Right) { 1 } else { 0 }
    [AbpE2E.Native]::MouseButton($b, $false); Start-Sleep -Milliseconds 200; [AbpE2E.Native]::MouseButton($b, $true); Start-Sleep -Milliseconds 400
    if ($Right) { Move-Mouse ($p[0] + 2) ($p[1] + 2); Start-Sleep -Milliseconds 500 }
}
function Station([int]$container, [int]$i) { "vanilla.${container}:$i" }
# A click $dx columns / $dy rows away from a seen slot (an empty slot has no logged position).
function V-ClickFrom([string]$key, [int]$dx, [int]$dy) {
    $p = V-Pos $key; $x = $p[0] + $dx * 18 / $V.Inv; $y = $p[1] + $dy * 18 / $V.Inv
    Move-Mouse ($x + 3) ($y + 3); Start-Sleep -Milliseconds 150; Move-Mouse $x $y; Start-Sleep -Milliseconds 250
    [AbpE2E.Native]::MouseButton(0, $false); Start-Sleep -Milliseconds 200; [AbpE2E.Native]::MouseButton(0, $true); Start-Sleep -Milliseconds 400
}

function Where-Backpacks {
    $m = Get-LogMark
    Invoke-Chat '/backpack gx3 where'
    Wait-LogLine 'gx3 where: \d+ backpacks' 10 -Since $m | Out-Null
    $lines = (Get-LeviLog -Since $m | Select-String -Pattern 'gx3 where: ' | ForEach-Object { $_.Line }) -join "`n"
    Write-Step "where: $lines"
    $lines
}
function Open-Block {
    Press-Key '9'
    Start-Sleep -Milliseconds 400
    for ($try = 1; ; $try++) {
        $mark = Get-LogMark
        Click 0.5 0.5 -Relative -Right
        Start-Sleep -Milliseconds 1500
        try { Read-VanillaSlots $mark; break } catch { if ($try -ge 3) { throw }; Write-Step "screen not open yet ($_); retrying" }
    }
}

# ---- setup ------------------------------------------------------------------------------------------------
Invoke-Chat '/clear @s'
Invoke-Chat '/tp @s ~ ~ ~ 0 29'
$mark = Get-LogMark
Invoke-Chat '/backpack gx3 setup'
$line = Wait-LogLine 'gx3 setup: A=' 15 -Since $mark
if ($line -notmatch 'A=(\S+) D=(\S+) E=(\S+)') { throw "unexpected setup line: $line" }
$A = $Matches[1]; $D = $Matches[2]; $E = $Matches[3]
Press-Key '9'
Start-Sleep -Milliseconds 500

# Sections to run: from $env:ABP_GX3_FROM (default 1, all).
$from = if ($env:ABP_GX3_FROM) { [int]$env:ABP_GX3_FROM } else { 1 }
# ---- 1. vanilla inventory ---------------------------------------------------------------------------------
if ($from -le 1) {
$mark = Get-LogMark
Invoke-Chat '/backpack gx3 grid2'            # 2x2 grid: apples, red dye
Wait-LogLine 'gx3 grid2 ready' 10 -Since $mark | Out-Null
$mark = Get-LogMark
Open-Inventory
Read-VanillaSlots $mark
# Item 9: the Advanced Tool Swapper's long lines wrap (they fit on neither side of the cursor).
$mark = Get-LogMark
V-Move 'inventory:9'
Start-Sleep -Milliseconds 900
Save-Screenshot 'tooltip-wrapped' | Out-Null
Wait-LogLine 'tooltip: alleviatedbackpacks:advanced_tool_swapper_upgrade wrapped at \d+' 5 -Since $mark | Out-Null

# Item 8: A on the cursor, stash the cobblestone into it, then click A back into its slot.
$mark = Get-LogMark
V-Click 'inventory:0'
V-Click 'inventory:6' -Right
Wait-LogLine 'key action: .* stash at inventory:6: 16 moved' 10 -Since $mark | Out-Null
V-Click 'inventory:0'
Start-Sleep -Milliseconds 800
Save-Screenshot 'inv-placed-back' | Out-Null

# Item 5: stash the apples from the 2x2 crafting grid.
$grid0 = Station $ContainerCraftingInput 0
$mark = Get-LogMark
V-Click 'inventory:0'
V-MoveFrom 'inventory:9' 4 1
Wait-LogLine 'stash: [1-9]\d* of \d+ slots stashable' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 500
Save-Screenshot 'inv-grid-minus-sign' | Out-Null
V-Click $grid0 -Right
Wait-LogLine "key action: .* stash at vanilla\.${ContainerCraftingInput}:0: 6 moved" 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 500
Save-Screenshot 'inv-grid-stashed' | Out-Null
V-Click 'inventory:0'
Start-Sleep -Milliseconds 500

# Item 7: dyeing D in the 2x2 grid (the red dye right of it: accent colour); the preview is dyed and is D.
$mark = Get-LogMark
V-Click 'inventory:1'
V-Click $grid0
Wait-LogLine 'craft preview: alleviatedbackpacks:diamond_backpack from the source backpack \(dyed\)' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 600
Read-VanillaSlots $mark
$out = V-Out
V-Move $out
Start-Sleep -Milliseconds 700
Save-Screenshot 'inv-dye-preview' | Out-Null
Hold 0x10
Start-Sleep -Milliseconds 1200
$p = V-Pos $out; Move-Mouse ($p[0] + 1) $p[1]
Start-Sleep -Milliseconds 800
Save-Screenshot 'inv-dye-preview-contents' | Out-Null
Release 0x10
V-Click $grid0                               # D back to its slot (the dye goes back on close)
V-Click 'inventory:1'
Close-Screen
Start-Sleep -Milliseconds 500
$w = Where-Backpacks
if ($w -notmatch "gold@$A at inventory:0") { throw "A was not placed back into inventory slot 0 (item 8): $w" }
if ($w -match "at armor:") { throw "a backpack was equipped instead of placed (item 8): $w" }

}
# ---- 2. crafting table ------------------------------------------------------------------------------------------
if ($from -le 2) {
Invoke-Chat '/setblock ~ ~ ~2 crafting_table'
$mark = Get-LogMark
Invoke-Chat '/backpack gx3 table'            # E in the middle, iron around, blue dye in the corner
Wait-LogLine 'gx3 table ready' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 600
Open-Block
# Stash the blue dye from the 3x3 grid; then the grid is the Copper -> Iron recipe (" I /IBI/ I ").
$g8 = Station $ContainerCraftingInput 8
$mark = Get-LogMark
V-Click 'inventory:0'
V-Click $g8 -Right
Wait-LogLine "key action: .* stash at vanilla\.${ContainerCraftingInput}:8: 1 moved" 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 400
Save-Screenshot 'table-stashed' | Out-Null
V-Click 'inventory:0'
Wait-LogLine 'craft preview: alleviatedbackpacks:iron_backpack from the source backpack' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 600
Read-VanillaSlots $mark
$out = V-Out
V-Move $out
Hold 0x10
Start-Sleep -Milliseconds 1500
$p = V-Pos $out; Move-Mouse ($p[0] + 1) $p[1]
Start-Sleep -Milliseconds 800
Save-Screenshot 'table-tier-preview-contents' | Out-Null
Release 0x10
$mark = Get-LogMark
V-Click $out
V-ClickFrom 'inventory:5' 2 0                # into the empty hotbar slot 7
Wait-LogLine "backpack craft: $E carried into the crafted backpack" 10 -Since $mark | Out-Null
Close-Screen
Start-Sleep -Milliseconds 500
$w = Where-Backpacks
if ($w -notmatch "iron@$E at inventory:7") { throw "the crafted iron backpack is not E: $w" }

}
# ---- 3. furnace ---------------------------------------------------------------------------------------------------
if ($from -le 3) {
Invoke-Chat '/setblock ~ ~ ~2 furnace'
Invoke-Chat '/replaceitem block ~ ~ ~2 slot.container 2 iron_ingot 5'
Invoke-Chat '/replaceitem block ~ ~ ~2 slot.container 1 coal 3'
Start-Sleep -Milliseconds 600
Open-Block
$result = Station $ContainerFurnaceResult 0
$fuel   = Station $ContainerFurnaceFuel 0
$mark = Get-LogMark
V-Click 'inventory:0'
V-MoveFrom 'inventory:9' 4 1
Start-Sleep -Milliseconds 900
Save-Screenshot 'furnace-minus-signs' | Out-Null
V-Click $result -Right
Wait-LogLine "key action: .* stash at vanilla\.${ContainerFurnaceResult}:0: 5 moved" 10 -Since $mark | Out-Null
V-Click $fuel -Right
Wait-LogLine "key action: .* stash at vanilla\.${ContainerFurnaceFuel}:0: 3 moved" 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 500
Save-Screenshot 'furnace-stashed' | Out-Null
V-Click 'inventory:0'
Close-Screen
Start-Sleep -Milliseconds 500

}
# ---- 4. smithing table: Diamond -> Netherite without durability --------------------------------------------------
if ($from -le 4) {
Invoke-Chat '/setblock ~ ~ ~2 smithing_table'
$mark = Get-LogMark
Invoke-Chat '/backpack gx3 smithing'         # template, D, netherite ingot in the smithing slots
Wait-LogLine 'gx3 smithing ready' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 600
$mark = Get-LogMark
Open-Block
Wait-LogLine 'craft preview: alleviatedbackpacks:netherite_backpack from the source backpack' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 600
Read-VanillaSlots $mark
$out = V-Out
V-Move $out
Start-Sleep -Milliseconds 800
Save-Screenshot 'smithing-preview' | Out-Null
$mark = Get-LogMark
V-Click $out
V-ClickFrom 'inventory:0' 1 0                # into the empty hotbar slot 1
Wait-LogLine "backpack craft: $D carried into the crafted backpack" 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 500
Save-Screenshot 'smithing-taken' | Out-Null
Close-Screen
Start-Sleep -Milliseconds 500
$w = Where-Backpacks
if ($w -notmatch "netherite@$D at inventory:1") { throw "the smithed netherite backpack is not D: $w" }

}
# ---- 5. native screen ----------------------------------------------------------------------------------------------
if ($from -le 5) {
Press-Key '1'
Start-Sleep -Milliseconds 300
$mark = Get-LogMark
Press-Key 'B'
Wait-LogLine 'key action: .* opens the carried backpack' 10 -Since $mark | Out-Null
Read-Layout $mark
Start-Sleep -Milliseconds 800
# Item 4: the carried item goes below the sort buttons (SC draws them after renderSuper).
$h = Hotbar-Pos 7
Gui-Click $h[0] $h[1]
Gui-Move ($L.SortX + 6) ($L.SortY + 6)
Start-Sleep -Milliseconds 600
Save-Screenshot 'cursor-below-sort-button' | Out-Null
Gui-Move ($L.SortX + 14 + 6) ($L.SortY + 6)
Start-Sleep -Milliseconds 400
Save-Screenshot 'cursor-below-sort-by-button' | Out-Null
Gui-Click $h[0] $h[1]
# Item 6: sort / transfer keys play SB's click sound.
$p = Storage-Pos 5
Gui-Move $p[0] $p[1]
$mark = Get-LogMark
Middle-Click
Wait-LogLine 'key sound: sort \(played\)' 10 -Since $mark | Out-Null
$mark = Get-LogMark
Tap-Vk 0xDB
Wait-LogLine 'key sound: transfer_to_storage \(played\)' 10 -Since $mark | Out-Null
$mark = Get-LogMark
Tap-Vk 0xDD
Wait-LogLine 'key sound: transfer_to_inventory \(played\)' 10 -Since $mark | Out-Null
# Item 3: open the pickup tab; the storage keeps it (server side) and the next screen reopens it.
if ($L.Tabs -lt 2) { throw "expected the settings and pickup tabs, got $($L.Tabs)" }
$t1 = Tab-Pos 1
$mark = Get-LogMark
Gui-Click $t1[0] $t1[1]
Wait-LogLine "native screen: open tab 0 kept on backpack $A" 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 500
$mark = Get-LogMark
Press-Key 'ESC'
Wait-LogLine 'native screen action: close by' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 800
$mark = Get-LogMark
Press-Key 'B'
Wait-LogLine 'key action: .* opens the carried backpack' 10 -Since $mark | Out-Null
Wait-LogLine 'companion: reopened the kept upgrade tab 0' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 800
Save-Screenshot 'tab-kept-open' | Out-Null
Read-Layout $mark
$t1 = Tab-Pos 1
$mark = Get-LogMark
Gui-Click $t1[0] $t1[1]
Wait-LogLine "native screen: open tab -1 kept on backpack $A" 10 -Since $mark | Out-Null
$mark = Get-LogMark
Press-Key 'ESC'
Wait-LogLine 'native screen action: close by' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 800
}
Assert-NoErrors
