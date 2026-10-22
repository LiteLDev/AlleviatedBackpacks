# P2-7 Alchemy & Advanced Alchemy upgrades on the real client (`-Mode local` and `-Mode remote-mod`):
# the native SB alchemy tabs driven with real input (filter slots, condition toggles + tooltips,
# HURT threshold scrolling, the advanced match toggles), then ON_FIRE, UNDER_WATER and HURT
# triggered for real in the world while the backpack is carried.

Wait-LogLine 'companion: server ready' 90 | Out-Null
Write-Step 'companion handshake done'

Invoke-Chat '/clear @s'
Invoke-Chat '/gamemode survival @s'
Invoke-Chat '/effect @s clear'
$mark = Get-LogMark
Invoke-Chat '/backpack alchemy setup'
Wait-LogLine 'alchemy setup: gave iron backpack' 15 -Since $mark | Out-Null
Press-Key '1'
Start-Sleep -Milliseconds 600

# ---- layout helpers (see companion.ps1) ---------------------------------------------------------------
$script:L = $null
function Read-Layout($mark) {
    $line = Wait-LogLine 'companion: screen layout session=' 15 -Since $mark
    if ($line -notmatch 'left=([\d.]+) top=([\d.]+) width=(\d+) height=(\d+) rows=(\d+)/(\d+) scroll=(\w+) invGuiScale=([\d.]+) .*invLabel=(\d+),(\d+) upgrades=(\d+) tabs=(\d+)') {
        throw "unexpected layout line: $line"
    }
    $script:L = [pscustomobject]@{
        Left = [double]$Matches[1]; Top = [double]$Matches[2]; Width = [int]$Matches[3]; Height = [int]$Matches[4]
        Inv = [double]$Matches[8]; Upgrades = [int]$Matches[11]; Tabs = [int]$Matches[12]
    }
    Write-Step ("layout: {0}x{1} at {2},{3} guiScale=1/{4} tabs={5}" -f $L.Width, $L.Height, $L.Left, $L.Top, $L.Inv, $L.Tabs)
}
function Gui-Move([double]$gx, [double]$gy) { Move-Mouse (($L.Left + $gx) / $L.Inv) (($L.Top + $gy) / $L.Inv) }
function Gui-Click([double]$gx, [double]$gy, [switch]$Right) { Click (($L.Left + $gx) / $L.Inv) (($L.Top + $gy) / $L.Inv) -Right:$Right; Start-Sleep -Milliseconds 400 }
function Storage-Pos([int]$i) { @((8 + ($i % 9) * 18 + 8), (18 + [math]::Floor($i / 9) * 18 + 8)) }
# Tabs: 21x24, 25 apart from (imageWidth, 4); the icon is at +1,+4.
function Tab-Icon([int]$i) { @(($L.Width + 1 + 8), (4 + $i * 25 + 4 + 8)) }
# SC AlchemyUpgradeTab (TOP_POS 24, 2 columns 36 apart, rows 20 apart; advanced offset 20).
function Filter-Pos([int]$tab, [int]$i, [int]$off) { @(($L.Width + 3 + ($i % 2) * 36 + 9), (4 + $tab * 25 + 24 + $off + [math]::Floor($i / 2) * 20 + 9)) }
function Cond-Pos([int]$tab, [int]$i, [int]$off) { @(($L.Width + 21 + ($i % 2) * 36 + 9), (4 + $tab * 25 + 24 + $off + [math]::Floor($i / 2) * 20 + 9)) }
function Top-Pos([int]$tab, [int]$k) { @(($L.Width + 3 + $k * 18 + 9), (4 + $tab * 25 + 24 + 9)) }
function Tab-Action([scriptblock]$do, [string]$what) {
    $m = Get-LogMark
    & $do
    Wait-LogLine "native screen action: tab by .* \(ok\)" 10 -Since $m | Out-Null
    Start-Sleep -Milliseconds 400
    Write-Step $what
}
function Status-Line {
    $m = Get-LogMark
    Invoke-Chat '/backpack alchemy status'
    return Wait-LogLine 'alchemy status: effects=' 15 -Since $m
}

# ---- the native tabs -------------------------------------------------------------------------------
$screenMark = Get-LogMark
Press-Key 'B'
Wait-LogLine 'companion: drawing the native backpack screen' 10 -Since $screenMark | Out-Null
Read-Layout $screenMark
if ($L.Tabs -lt 3) { throw "expected the settings tab plus two alchemy tabs, got $($L.Tabs)" }
Start-Sleep -Milliseconds 600

# Alchemy tab (upgrade slot 0 -> tab 1).
$t = Tab-Icon 1
Gui-Move $t[0] $t[1]
Start-Sleep -Milliseconds 400
Save-Screenshot 'alchemy-tab-closed-tooltip' | Out-Null
Gui-Click $t[0] $t[1]
Start-Sleep -Milliseconds 600
Save-Screenshot 'alchemy-tab' | Out-Null

# Condition tooltips: Fire Resistance = "When on Fire", Healing = "When Hurt (Below 75% Health)".
$c = Cond-Pos 1 0 0
Gui-Move $c[0] $c[1]
Start-Sleep -Milliseconds 500
Save-Screenshot 'condition-on-fire' | Out-Null
$c = Cond-Pos 1 1 0
Gui-Move $c[0] $c[1]
Start-Sleep -Milliseconds 500
Save-Screenshot 'condition-under-water' | Out-Null
$c = Cond-Pos 1 2 0
Gui-Move $c[0] $c[1]
Start-Sleep -Milliseconds 500
Save-Screenshot 'condition-hurt-75' | Out-Null
Tab-Action { Scroll 1 } 'scrolled the HURT threshold up'
Gui-Move ($c[0] + 1) $c[1]
Start-Sleep -Milliseconds 500
Save-Screenshot 'condition-hurt-80' | Out-Null
Tab-Action { Scroll -1 } 'scrolled the HURT threshold back down'

# Filter slot tooltip (the potion).
$f = Filter-Pos 1 0 0
Gui-Move $f[0] $f[1]
Start-Sleep -Milliseconds 500
Save-Screenshot 'filter-tooltip' | Out-Null

# Put the night vision potion (storage slot 9) into the empty filter slot 3: the cursor item is
# copied into the filter (it stays on the cursor), then goes back into its slot.
$s = Storage-Pos 9
Gui-Click $s[0] $s[1]
$f = Filter-Pos 1 3 0
Tab-Action { Gui-Click $f[0] $f[1] } 'night vision set as a filter'
$c = Cond-Pos 1 3 0
Gui-Move $c[0] $c[1]
Start-Sleep -Milliseconds 500
Save-Screenshot 'filter-set-keep-active' | Out-Null
$back = Storage-Pos 20 # an empty slot (clicking slot 9 again would be a double click)
Gui-Click $back[0] $back[1]
Tab-Action { Gui-Click $c[0] $c[1] } 'condition toggled'
Gui-Move ($c[0] + 1) $c[1]
Start-Sleep -Milliseconds 500
Save-Screenshot 'condition-toggled' | Out-Null
# Clear it again (empty cursor) so it doesn't drink night vision during the world part.
Tab-Action { Gui-Click $f[0] $f[1] } 'filter cleared'
Start-Sleep -Milliseconds 300
Save-Screenshot 'filter-cleared' | Out-Null

# Close the alchemy tab, open the advanced one (upgrade slot 1 -> tab 2).
Gui-Click $t[0] $t[1]
$t2 = Tab-Icon 2
Gui-Click $t2[0] $t2[1]
Start-Sleep -Milliseconds 600
Save-Screenshot 'advanced-tab' | Out-Null
foreach ($k in 0..2) {
    $p = Top-Pos 2 $k
    Gui-Move $p[0] $p[1]
    Start-Sleep -Milliseconds 500
    Save-Screenshot "advanced-toggle-$k" | Out-Null
}
$p = Top-Pos 2 0
Tab-Action { Gui-Click $p[0] $p[1] } 'All Effects Missing'
Gui-Move ($p[0] + 1) $p[1]
Start-Sleep -Milliseconds 500
Save-Screenshot 'advanced-all-effects-missing' | Out-Null
Tab-Action { Gui-Click $p[0] $p[1] } 'Any Effect Missing again'
$c = Cond-Pos 2 0 20
Gui-Move $c[0] $c[1]
Start-Sleep -Milliseconds 500
Save-Screenshot 'advanced-milk-condition' | Out-Null
Gui-Click $t2[0] $t2[1]

if ((Get-LeviLog -Since $screenMark) -match 'menu shown: ') { throw 'a fake chest was opened for a companion player' }
$mark = Get-LogMark
Press-Key 'E'
Wait-LogLine 'native screen action: close by' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 800

# ---- the conditions for real -------------------------------------------------------------------------
Invoke-Chat '/tp @s ~ ~ ~ 0 30'
Start-Sleep -Milliseconds 500
$st = Status-Line
Write-Step "before: $st"

# ON_FIRE: stand in fire.
Invoke-Chat '/setblock ~ ~ ~ fire'
$ok = $false
for ($i = 0; $i -lt 10 -and -not $ok; $i++) {
    Start-Sleep -Seconds 1
    $st = Status-Line
    $ok = $st -match 'effects=\[[^\]]*\b12:'
}
Save-Screenshot 'on-fire-resistance' | Out-Null
Invoke-Chat '/setblock ~ ~ ~ air'
if (-not $ok) { throw "ON_FIRE: no fire resistance: $st" }
if ($st -notmatch 'fire_resistance=2 ') { throw "ON_FIRE: expected one potion used: $st" }
Write-Step "on fire -> fire resistance: $st"

# UNDER_WATER: a pool around and above the player.
Invoke-Chat '/fill ~-1 ~ ~-1 ~1 ~2 ~1 water'
$ok = $false
for ($i = 0; $i -lt 10 -and -not $ok; $i++) {
    Start-Sleep -Seconds 1
    $st = Status-Line
    $ok = $st -match 'effects=\[[^\]]*\b13:'
}
Save-Screenshot 'under-water-breathing' | Out-Null
Invoke-Chat '/fill ~-1 ~ ~-1 ~1 ~2 ~1 air'
if (-not $ok) { throw "UNDER_WATER: no water breathing: $st" }
Write-Step "under water -> water breathing: $st"
Start-Sleep -Seconds 2

# HURT: 12 damage (20 -> 8, below 75%): healing potions are drunk until it is no longer below 75%.
Invoke-Chat '/effect @s clear'
Invoke-Chat '/damage @s 12'
$ok = $false
for ($i = 0; $i -lt 10 -and -not $ok; $i++) {
    Start-Sleep -Seconds 1
    $st = Status-Line
    $ok = $st -match 'healing=[012] '
}
Save-Screenshot 'hurt-healed' | Out-Null
if (-not $ok) { throw "HURT: no healing potion used: $st" }
if ($st -notmatch 'health=(\d+)/' -or [int]$Matches[1] -le 8) { throw "HURT: not healed: $st" }
Write-Step "hurt -> healed: $st"

Assert-NoErrors
