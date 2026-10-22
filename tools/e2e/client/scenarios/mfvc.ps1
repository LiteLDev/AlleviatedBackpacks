# P2-4: the native Magnet, Feeding, Void and Compacting tabs (SC MagnetUpgradeTab,
# FeedingUpgradeTab, VoidUpgradeTab, CompactingUpgradeTab) driven with real input: the toggle
# buttons at (x+3, y+24) / (x+21, y+24), their tooltips, and the shared filter control below
# (food-only / compactable-only filter slots). Works in `-Mode local` and `-Mode remote-mod`.
#
# Coordinates: the backpack screen logs its layout ("companion: screen layout ...") and every
# tab of this unit logs where it is ("mfvc tab: built <id> at X,Y size WxH control X,Y").

Wait-LogLine 'companion: server ready' 90 | Out-Null
Write-Step 'companion handshake done'

Invoke-Chat '/clear @s'
$mark = Get-LogMark
Invoke-Chat '/backpack mfvc setup'
Wait-LogLine 'mfvc setup: gave netherite backpack' 15 -Since $mark | Out-Null
Press-Key '1'
Start-Sleep -Milliseconds 600

# ---- layout helpers --------------------------------------------------------------------------------
$script:L = $null
function Read-Layout($mark) {
    $line = Wait-LogLine 'companion: screen layout session=' 15 -Since $mark
    if ($line -notmatch 'left=([\d.]+) top=([\d.]+) width=(\d+) height=(\d+) rows=(\d+)/(\d+) scroll=(\w+) invGuiScale=([\d.]+) .*invLabel=(\d+),(\d+) upgrades=(\d+) tabs=(\d+)') {
        throw "unexpected layout line: $line"
    }
    $script:L = [pscustomobject]@{
        Left = [double]$Matches[1]; Top = [double]$Matches[2]; Width = [int]$Matches[3]; Height = [int]$Matches[4]
        Inv = [double]$Matches[8]; InvX = [int]$Matches[9]; InvY = [int]$Matches[10]; Tabs = [int]$Matches[12]
    }
    Write-Step ("layout: {0}x{1} at {2},{3} guiScale=1/{4} tabs={5}" -f $L.Width, $L.Height, $L.Left, $L.Top, $L.Inv, $L.Tabs)
}
function Gui-Move([double]$gx, [double]$gy) { Move-Mouse (($L.Left + $gx) / $L.Inv) (($L.Top + $gy) / $L.Inv) }
function Gui-Click([double]$gx, [double]$gy) { Click (($L.Left + $gx) / $L.Inv) (($L.Top + $gy) / $L.Inv); Start-Sleep -Milliseconds 400 }
function Hotbar-Click([int]$k) { Gui-Click ($L.InvX + $k * 18 + 8) ($L.InvY + 12 + 3 * 18 + 4 + 8) }
function Tab-Click([int]$i) { Gui-Click ($L.Width + 1 + 8) (4 + 25 * $i + 4 + 8) }

# The open tab (absolute GUI units).
$script:T = $null
function Read-Tab($mark, [string]$id) {
    $line = Wait-LogLine ("mfvc tab: built $id at ") 10 -Since $mark
    if ($line -notmatch 'at ([\d.\-]+),([\d.\-]+) size ([\d.]+)x([\d.]+) control ([\d.\-]+),([\d.\-]+)') { throw "unexpected tab line: $line" }
    $script:T = [pscustomobject]@{
        X = [double]$Matches[1]; Y = [double]$Matches[2]; W = [double]$Matches[3]; H = [double]$Matches[4]
        CX = [double]$Matches[5]; CY = [double]$Matches[6]
    }
    Write-Step ("{0} tab at {1},{2} size {3}x{4}, filter control at {5},{6}" -f $id, $T.X, $T.Y, $T.W, $T.H, $T.CX, $T.CY)
}
function Abs-Move([double]$x, [double]$y) { Move-Mouse ($x / $L.Inv) ($y / $L.Inv) }
function Abs-Click([double]$x, [double]$y) { Click ($x / $L.Inv) ($y / $L.Inv); Start-Sleep -Milliseconds 450 }
# Toggle button $n (0: x+3, 1: x+21) at y+24.
function Toggle-Move([int]$n) { Abs-Move ($T.X + 3 + 18 * $n + 9) ($T.Y + 24 + 9) }
function Toggle-Click([int]$n) {
    $mark = Get-LogMark
    Abs-Click ($T.X + 3 + 18 * $n + 9) ($T.Y + 24 + 9)
    Wait-LogLine 'native screen action: tab by .* \(ok\)' 10 -Since $mark | Out-Null
    Start-Sleep -Milliseconds 300
}
# Filter control: type button (column 0) and slot $i (rows of $perRow, 21 below the buttons).
function Filter-Type { Abs-Click ($T.CX + 9) ($T.CY + 9) }
function Filter-Slot([int]$i, [int]$perRow) { Abs-Click ($T.CX + 1 + ($i % $perRow) * 18 + 8) ($T.CY + 21 + 1 + [math]::Floor($i / $perRow) * 18 + 8) }
function Shot([string]$name, [int]$delay = 400) { Start-Sleep -Milliseconds $delay; Save-Screenshot $name | Out-Null }

function Open-Backpack {
    $mark = Get-LogMark
    Press-Key 'B'
    Wait-LogLine 'companion: drawing the native backpack screen' 10 -Since $mark | Out-Null
    Read-Layout $mark
    Start-Sleep -Milliseconds 600
    if ((Get-LeviLog -Since $mark) -match 'menu shown: ') { throw 'a fake chest was opened for a companion player' }
}
function Open-Tab([int]$i, [string]$id) {
    $mark = Get-LogMark
    Tab-Click $i
    Read-Tab $mark $id
    Start-Sleep -Milliseconds 400
}

# Tabs (setup order): 1 advanced_magnet, 2 advanced_feeding, 3 advanced_void, 4 advanced_compacting,
# 5 magnet, 6 feeding, 7 void. Hotbar: 0 backpack, 1 apple, 2 stone, 3 iron ingot, 4 water bucket.
Open-Backpack
if ($L.Tabs -lt 8) { throw "expected 8 tabs (settings + 7 upgrades), got $($L.Tabs)" }

# Closed tab tooltip: SB "Advanced Magnet Settings".
Gui-Move ($L.Width + 1 + 8) (4 + 25 + 4 + 8)
Shot 'closed-tab-tooltip'

# ---- Advanced Magnet ----------------------------------------------------------------------------------
Open-Tab 1 'advanced_magnet'
if ([math]::Abs($T.CY - $T.Y - 44) -gt 0.01) { throw 'magnet filter control must be at y+44' }
Gui-Move 100 3
Shot 'magnet-tab'
Toggle-Move 0; Shot 'magnet-pickup-items-tooltip'
Toggle-Click 0
Toggle-Move 0; Shot 'magnet-do-not-pickup-items'
Toggle-Move 1; Shot 'magnet-pickup-xp-tooltip'
# Contents filter type: Block -> Match Backpack Contents.
Filter-Type
Abs-Move ($T.CX + 9) ($T.CY + 9); Shot 'magnet-match-contents'
Tab-Click 1

# ---- Advanced Feeding -----------------------------------------------------------------------------------
Open-Tab 2 'advanced_feeding'
Gui-Move 100 3
Shot 'feeding-tab'
Toggle-Move 0; Shot 'feeding-hunger-half-tooltip'
Toggle-Click 0
Toggle-Move 0; Shot 'feeding-hunger-full-tooltip'
Toggle-Move 1; Shot 'feeding-hurt-tooltip'
Toggle-Click 1
Toggle-Move 1; Shot 'feeding-do-not-consider-health'
# Food into the filter; stone is refused (SC: food-only filter slots).
Hotbar-Click 1
Filter-Slot 0 4
Hotbar-Click 1
Hotbar-Click 2
Filter-Slot 1 4
Hotbar-Click 2
Gui-Move 100 3
Shot 'feeding-filter-apple'
Tab-Click 2

# ---- Advanced Void --------------------------------------------------------------------------------------
Open-Tab 3 'advanced_void'
Gui-Move 100 3
Shot 'void-tab'
Toggle-Move 0; Shot 'void-only-automatic-tooltip'
Toggle-Move 1; Shot 'void-always-tooltip'
Toggle-Click 1
Toggle-Move 1; Shot 'void-slot-overflow-tooltip'
Toggle-Click 1
Toggle-Move 1; Shot 'void-storage-overflow-tooltip'
Toggle-Click 1
Toggle-Click 1
Toggle-Click 0
Toggle-Move 0; Shot 'void-works-in-gui'
# A water bucket in the filter stands for water (tank voiding).
Hotbar-Click 4
Filter-Slot 0 4
Hotbar-Click 4
Gui-Move 100 3
Shot 'void-filter-bucket'
Tab-Click 3

# ---- Advanced Compacting ------------------------------------------------------------------------------
Open-Tab 4 'advanced_compacting'
Gui-Move 100 3
Shot 'compacting-tab'
Toggle-Move 0; Shot 'compacting-only-uncraftable-tooltip'
Toggle-Click 0
Toggle-Move 0; Shot 'compacting-anything-tooltip'
Toggle-Click 1
Toggle-Move 1; Shot 'compacting-works-in-gui-tooltip'
# Iron ingots compact; apples don't and are refused.
Hotbar-Click 3
Filter-Slot 0 4
Hotbar-Click 3
Hotbar-Click 1
Filter-Slot 1 4
Hotbar-Click 1
Gui-Move 100 3
Shot 'compacting-filter'
Tab-Click 4

# ---- Basic tabs -----------------------------------------------------------------------------------------
Open-Tab 5 'magnet'
Gui-Move 100 3
Shot 'basic-magnet-tab'
Tab-Click 5
Open-Tab 6 'feeding'
if ([math]::Abs($T.CY - $T.Y - 24) -gt 0.01) { throw 'basic feeding filter control must be at y+24' }
Gui-Move 100 3
Shot 'basic-feeding-tab'
Tab-Click 6
Open-Tab 7 'void'
Gui-Move 100 3
Shot 'basic-void-tab'
Tab-Click 7
$mark = Get-LogMark
# ESC first (with real input E also reached the game afterwards: emote wheel); headless input may
# not deliver ESC to the screen, then E.
Press-Key ESC
try { Wait-LogLine 'native screen action: close by' 4 -Since $mark | Out-Null }
catch { Press-Key 'E'; Wait-LogLine 'native screen action: close by' 10 -Since $mark | Out-Null }
Start-Sleep -Milliseconds 2000
Save-Screenshot 'closed' | Out-Null

# ---- what the server stored ------------------------------------------------------------------------------
$mark = Get-LogMark
Invoke-Chat '/backpack mfvc dump'
Wait-LogLine 'mfvc dump: slot=6 ' 15 -Since $mark | Out-Null
$dump = @{}
foreach ($line in (Get-LeviLog -Since $mark)) {
    if ($line -match 'mfvc dump: slot=(\d+) id=(\w+) settings=(.*)$') { $dump[$Matches[2]] = $Matches[3] | ConvertFrom-Json }
}
function Check([bool]$ok, [string]$what) { if (-not $ok) { throw "check failed: $what" }; Write-Step "ok: $what" }
$m = $dump['advanced_magnet']
Check ($m.pickupItems -eq $false -and $m.filter.storage -eq $true) 'magnet: items off, Match Backpack Contents'
$f = $dump['advanced_feeding']
Check ($f.feedAtHungerLevel -eq 'full' -and $f.feedImmediatelyWhenHurt -eq $false) 'feeding: FULL, do not consider health'
Check ($f.filter.ghosts[0].id -eq 'minecraft:apple' -and -not $f.filter.ghosts[1].id) 'feeding: apple in the filter, stone refused'
$v = $dump['advanced_void']
Check ($v.voidType -eq 'slot_overflow' -and $v.workInGui -eq $true -and $v.filter.ghosts[0].id -eq 'minecraft:water_bucket') 'void: slot overflow, works in GUI, bucket filter'
$c = $dump['advanced_compacting']
Check ($c.compactNonUncraftable -eq $true -and $c.workInGui -eq $true) 'compacting: anything, works in GUI'
Check ($c.filter.ghosts[0].id -eq 'minecraft:iron_ingot' -and -not $c.filter.ghosts[1].id) 'compacting: iron ingot in the filter, apple refused'

# Server-side behaviour of the four upgrades.
foreach ($glob in 'magnet.*', 'feeding.*', 'void.*', 'compacting.*') {
    $r = Invoke-SelfTest $glob
    if ($r.Fail -gt 0 -or $r.Pass -lt 7) { throw "selftests ${glob}: pass=$($r.Pass) fail=$($r.Fail)" }
}
Assert-NoErrors
