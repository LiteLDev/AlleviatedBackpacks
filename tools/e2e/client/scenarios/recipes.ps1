# P2-12 (client): Sophisticated Backpacks' recipes with real input, in `-Mode local` and
# `-Mode remote-mod`:
#   1. dye a backpack in a crafting table with dyes in different columns (SB `backpack_dye`):
#      red (left column) -> main, blue (right column) -> accent, yellow (backpack column) -> both;
#   2. upgrade it to Copper through the recipe book: uuid, contents, colours and upgrades are kept;
#   3. convert its installed Stack Upgrade Starter Tier to Tier 1 by right-clicking the upgrade slot
#      of the native backpack screen with the conversion item.
# Screenshots: crafting grid/output (colours), lore, native screen before/after the conversion.

if ($env:ABP_E2E_HOST_DIR) { Wait-LogLine 'companion: server ready' 90 | Out-Null }

function Get-TiersInfo {
    # Remote mode: the line comes from the host log; a chat line typed while a screen is still
    # closing can get lost, so retry once.
    $line = $null
    for ($try = 0; $try -lt 2 -and -not $line; $try++) {
        $mark = Get-LogMark
        Invoke-Chat '/backpack tiers info'
        try { $line = Wait-LogLine 'tiers info: ' 20 -Since $mark } catch { if ($try -gt 0) { throw }; Start-Sleep -Seconds 1 }
    }
    if ($line -notmatch 'uuid=(\S+) tier=(\S+) items=(\d+) main=(#\w+) accent=(#\w+) upgrades=\[([^\]]*)\]') { throw "bad info line: $line" }
    return [pscustomobject]@{ Uuid = $Matches[1]; Tier = $Matches[2]; Items = [int]$Matches[3]; Main = $Matches[4]; Accent = $Matches[5]; Upgrades = $Matches[6] }
}

# Positions in the crafting table screen (fractions of the 1280x720 screenshot).
$gridX = 0.547, 0.581, 0.615
$gridY = 0.306, 0.367, 0.426
$hotY = 0.729
function HotX([int]$k) { (648 + 43.1 * $k) / 1280 }
function Move-ToGrid([int]$hot, [int]$row, [int]$col) {
    Click (HotX $hot) $hotY -Relative; Start-Sleep -Milliseconds 400
    Click $gridX[$col] $gridY[$row] -Relative; Start-Sleep -Milliseconds 400
}

Invoke-Chat '/clear @s'
$mark = Get-LogMark
Invoke-Chat '/backpack tiers setup'
Wait-LogLine 'tiers setup: gave leather backpack' 15 -Since $mark | Out-Null
Invoke-Chat '/replaceitem entity @s slot.hotbar 1 red_dye 1'
Invoke-Chat '/replaceitem entity @s slot.hotbar 2 yellow_dye 1'
Invoke-Chat '/replaceitem entity @s slot.hotbar 3 blue_dye 1'
Invoke-Chat '/replaceitem entity @s slot.hotbar 4 copper_ingot 8'
Invoke-Chat '/replaceitem entity @s slot.hotbar 5 alleviatedbackpacks:stack_conversion_starter_to_tier_1 1'
Invoke-Chat '/setblock ~ ~-1 ~2 crafting_table'
Invoke-Chat '/tp @s ~ ~ ~ 0 40'
Press-Key '1'
Start-Sleep -Seconds 1
$before = Get-TiersInfo
if ($before.Items -ne 20 -or $before.Tier -ne 'leather' -or $before.Upgrades -notmatch 'stack_starter_upgrade') { throw "setup: $($before | Out-String)" }
if ($before.Main -ne '#CC613A' -or $before.Accent -ne '#622E1A') { throw "SB default colours expected, got $($before.Main) / $($before.Accent)" }

# ---- 1. dye by columns in a crafting table ----------------------------------------------------------
Press-Key '9'
Click 0.5 0.5 -Relative -Right
Start-Sleep -Seconds 2
Save-Screenshot 'table-open' | Out-Null
Move-ToGrid 0 1 1   # backpack: centre
Move-ToGrid 1 0 0   # red: left column -> main
Move-ToGrid 3 2 2   # blue: right column -> accent
Move-ToGrid 2 0 1   # yellow: backpack column -> main and accent
Move-Mouse 0.80 0.60 -Relative
Start-Sleep -Milliseconds 800
Save-Screenshot 'dye-grid' | Out-Null
$mark = Get-LogMark
Click 0.731 0.367 -Relative; Start-Sleep -Milliseconds 600
Click (HotX 0) $hotY -Relative; Start-Sleep -Milliseconds 600
$line = Wait-LogLine 'backpack craft: .* carried into the crafted backpack \(dyed' 10 -Since $mark
# SC calculateColor: main = red + yellow, accent = blue + yellow (defaults are not mixed in).
if ($line -notmatch 'main=#D78331 accent=#D4BF9B') { throw "unexpected colours: $line" }
Move-Mouse (HotX 0) $hotY -Relative
Start-Sleep -Milliseconds 800
Save-Screenshot 'dye-crafted' | Out-Null
Close-Screen
Press-Key '1'
Start-Sleep -Seconds 1
$dyed = Get-TiersInfo
if ($dyed.Uuid -ne $before.Uuid -or $dyed.Items -ne 20 -or $dyed.Main -ne '#D78331' -or $dyed.Accent -ne '#D4BF9B') { throw "dyeing: $($dyed | Out-String)" }
Save-Screenshot 'hud-dyed' | Out-Null

# ---- 2. tier upgrade through the recipe book ----------------------------------------------------------
Press-Key '9'
Click 0.5 0.5 -Relative -Right
Start-Sleep -Seconds 2
$mark = Get-LogMark
Click 0.30 0.31 -Relative; Start-Sleep -Milliseconds 500
Type-Text 'copper backpack'
Start-Sleep -Milliseconds 800
Click 0.230 0.378 -Relative; Start-Sleep -Milliseconds 800
Save-Screenshot 'table-grid' | Out-Null
Click 0.731 0.367 -Relative; Start-Sleep -Milliseconds 800
Click (HotX 0) $hotY -Relative; Start-Sleep -Milliseconds 800
Save-Screenshot 'table-crafted' | Out-Null
Close-Screen
Wait-LogLine 'backpack craft: .* carried into the crafted backpack' 10 -Since $mark | Out-Null
Press-Key '1'
Start-Sleep -Seconds 1
$copper = Get-TiersInfo
if ($copper.Uuid -ne $before.Uuid -or $copper.Items -ne 20 -or $copper.Tier -ne 'copper' -or $copper.Main -ne '#D78331' -or $copper.Accent -ne '#D4BF9B' -or $copper.Upgrades -notmatch 'stack_starter_upgrade') {
    throw "tier upgrade did not keep the backpack: $($copper | Out-String)"
}

# ---- 3. stack upgrade conversion in the native screen ------------------------------------------------
$script:L = $null
function Read-Layout($mark) {
    $line = Wait-LogLine 'companion: screen layout session=' 15 -Since $mark
    if ($line -notmatch 'left=([\d.]+) top=([\d.]+) width=(\d+) height=(\d+) .*invGuiScale=([\d.]+) .*invLabel=(\d+),(\d+)') { throw "unexpected layout line: $line" }
    $script:L = [pscustomobject]@{ Left = [double]$Matches[1]; Top = [double]$Matches[2]; Width = [int]$Matches[3]; Inv = [double]$Matches[5]; InvX = [int]$Matches[6]; InvY = [int]$Matches[7] }
}
function Gui-Move([double]$gx, [double]$gy) { Move-Mouse (($L.Left + $gx) / $L.Inv) (($L.Top + $gy) / $L.Inv) }
function Gui-Click([double]$gx, [double]$gy, [switch]$Right) { Click (($L.Left + $gx) / $L.Inv) (($L.Top + $gy) / $L.Inv) -Right:$Right; Start-Sleep -Milliseconds 400 }

$mark = Get-LogMark
Press-Key 'B'
Wait-LogLine 'companion: drawing the native backpack screen' 10 -Since $mark | Out-Null
Read-Layout $mark
if ((Get-LeviLog -Since $mark) -match 'menu shown: ') { throw 'a fake chest was opened' }
Gui-Move -7 14
Start-Sleep -Milliseconds 700
Save-Screenshot 'upgrade-starter' | Out-Null
$hot = @(($L.InvX + 8), ($L.InvY + 12 + 54 + 4 + 8))
Gui-Click ($hot[0] + 18 * 5) $hot[1]           # conversion item onto the cursor
Gui-Click -7 14                                # left click: SB does nothing
Start-Sleep -Milliseconds 300
if ((Get-LeviLog -Since $mark) -match 'stack upgrade converted') { throw 'a left click converted the upgrade' }
$mark = Get-LogMark
Gui-Click -7 14 -Right                         # right click: converts in place
Wait-LogLine 'stack upgrade converted: alleviatedbackpacks:stack_starter_upgrade -> alleviatedbackpacks:stack_tier_1_upgrade' 10 -Since $mark | Out-Null
Gui-Move -7 14
Start-Sleep -Milliseconds 700
Save-Screenshot 'upgrade-tier1' | Out-Null
$mark = Get-LogMark
Press-Key 'E'
Wait-LogLine 'native screen action: close by' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 1500
# Lore / tint of the result in the vanilla inventory (this also gives the game back its normal
# key handling before chat is opened again).
Open-Inventory
Start-Sleep -Milliseconds 500
Save-Screenshot 'inventory' | Out-Null
Close-Screen
Start-Sleep -Seconds 1
$after = Get-TiersInfo
if ($after.Upgrades -notmatch 'stack_tier_1_upgrade' -or $after.Items -ne 20) { throw "conversion: $($after | Out-String)" }

Assert-NoErrors
