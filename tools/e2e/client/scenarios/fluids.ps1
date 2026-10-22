# P2-6 (Tank, Battery, Pump, XP Pump) on the native SB screen, driven with real input in
# `-Mode local` and `-Mode remote-mod`:
# - two Tank bars and a Battery bar replace 6 storage columns of a diamond backpack;
# - bar tooltips (mB / xp levels / FE), a bucket filled by clicking the water tank's bar;
# - the Tank tab (container slots), Battery tab, Advanced Pump tab (toggles, fluid filter) and the
#   Experience Pump tab (direction, level selector, take / store buttons).

Wait-LogLine 'companion: server ready' 90 | Out-Null
Write-Step 'companion handshake done'

Invoke-Chat '/clear @s'
$mark = Get-LogMark
Invoke-Chat '/backpack fluids setup'
$line = Wait-LogLine 'fluids setup: gave diamond backpack .* \((\d+) slots, (\d+) columns taken\)' 15 -Since $mark
if ($line -notmatch '\(54 slots, 6 columns taken\)') { throw "expected 54 slots with 6 columns taken: $line" }
Press-Key '1'
Start-Sleep -Milliseconds 600

# ---- layout helpers (as in companion.ps1) ----------------------------------------------------------
$script:L = $null
function Read-Layout($mark) {
    $line = Wait-LogLine 'companion: screen layout session=' 15 -Since $mark
    if ($line -notmatch 'left=([\d.]+) top=([\d.]+) width=(\d+) height=(\d+) rows=(\d+)/(\d+) scroll=(\w+) invGuiScale=([\d.]+) .*invLabel=(\d+),(\d+) upgrades=(\d+) tabs=(\d+)') {
        throw "unexpected layout line: $line"
    }
    $script:L = [pscustomobject]@{
        Left = [double]$Matches[1]; Top = [double]$Matches[2]; Width = [int]$Matches[3]; Height = [int]$Matches[4]
        Visible = [int]$Matches[5]; Rows = [int]$Matches[6]; Scroll = $Matches[7] -eq 'true'; Inv = [double]$Matches[8]
        InvX = [int]$Matches[9]; InvY = [int]$Matches[10]; Upgrades = [int]$Matches[11]; Tabs = [int]$Matches[12]
    }
    Write-Step ("layout: {0}x{1} at {2},{3} rows {4}/{5} scroll={6} guiScale=1/{7} tabs={8}" -f $L.Width, $L.Height, $L.Left, $L.Top, $L.Visible, $L.Rows, $L.Scroll, $L.Inv, $L.Tabs)
}
function Gui-Move([double]$gx, [double]$gy) { Move-Mouse (($L.Left + $gx) / $L.Inv) (($L.Top + $gy) / $L.Inv) }
function Gui-Click([double]$gx, [double]$gy, [switch]$Right) { Click (($L.Left + $gx) / $L.Inv) (($L.Top + $gy) / $L.Inv) -Right:$Right; Start-Sleep -Milliseconds 400 }
function Hotbar-Pos([int]$i) { @(($L.InvX + $i * 18 + 8), ($L.InvY + 12 + 54 + 4 + 8)) }
# The upgrade inventory parts start right of the 6 remaining storage columns (and the scroll bar).
function Part-X([int]$i) { 7 + 6 * 18 + $(if ($L.Scroll) { 6 } else { 0 }) + 36 * $i + 9 + 9 }
function Part-Y { 17 + ($L.Visible * 18) / 2 }
# Tab i (0 = backpack settings), icon centre; `Tab-Area` = top-left of the open tab.
function Tab-Icon([int]$i) { @(($L.Width + 1 + 8), (4 + 25 * $i + 4 + 8)) }
function Tab-Area([int]$i) { @($L.Width, (4 + 25 * $i)) }
function Toggle-Tab([int]$i) { $p = Tab-Icon $i; Gui-Click $p[0] $p[1]; Start-Sleep -Milliseconds 500 }

# ---- open ------------------------------------------------------------------------------------------
$mark = Get-LogMark
Press-Key 'B'
Wait-LogLine 'companion: drawing the native backpack screen' 10 -Since $mark | Out-Null
Read-Layout $mark
if ($L.Width -ne 230 -and $L.Width -ne 236) { throw "diamond backpack: expected the 12 column background (230/236 wide), got $($L.Width)" }
if ($L.Rows -ne 9) { throw "diamond backpack keeps its 9 base rows with columns taken, got $($L.Rows)" }
if ($L.Tabs -ne 6) { throw "expected the settings tab plus 5 upgrade tabs, got $($L.Tabs)" }
Start-Sleep -Milliseconds 1200
Save-Screenshot 'fluids-open' | Out-Null
if ((Get-LeviLog -Since $mark) -match 'menu shown: ') { throw 'a fake chest was opened for a companion player' }

# ---- bar tooltips ------------------------------------------------------------------------------------
Gui-Move (Part-X 0) (Part-Y)
Start-Sleep -Milliseconds 600
Save-Screenshot 'tank-water-tooltip' | Out-Null
Gui-Move (Part-X 1) (Part-Y)
Start-Sleep -Milliseconds 600
Save-Screenshot 'tank-xp-tooltip' | Out-Null
Gui-Move (Part-X 2) (Part-Y)
Start-Sleep -Milliseconds 600
Save-Screenshot 'battery-tooltip' | Out-Null

# ---- click the water tank with an empty bucket on the cursor (SB TankClickPayload) -----------------
$bucket = Hotbar-Pos 2
Gui-Click $bucket[0] $bucket[1]
Start-Sleep -Milliseconds 400
$mark = Get-LogMark
Gui-Click (Part-X 0) (Part-Y)
Wait-LogLine 'native screen action: tab by .* \(ok\)' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 700
Gui-Move ((Part-X 0) - 40) (Part-Y)
Start-Sleep -Milliseconds 400
Save-Screenshot 'bucket-filled-from-bar' | Out-Null
Gui-Click $bucket[0] $bucket[1] # put the (now water) bucket back
Start-Sleep -Milliseconds 400

# ---- Tank tab: pour bottles through the output slot ------------------------------------------------
Toggle-Tab 1
Save-Screenshot 'tank-tab' | Out-Null
$area    = Tab-Area 1
$bottles = Hotbar-Pos 4
Gui-Click $bottles[0] $bottles[1]          # glass bottles to the cursor
$mark = Get-LogMark
Gui-Click ($area[0] + 24 + 9) ($area[1] + 24 + 9)  # into the Output slot
Wait-LogLine 'native screen action: tab by' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 2500
Gui-Move ($area[0] + 3 + 9) ($area[1] + 24 + 9)    # hover the empty Input slot
Start-Sleep -Milliseconds 500
Save-Screenshot 'tank-tab-output' | Out-Null
Toggle-Tab 1

# ---- Battery tab ---------------------------------------------------------------------------------
Toggle-Tab 3
$area = Tab-Area 3
Gui-Move ($area[0] + 3 + 9) ($area[1] + 24 + 9)
Start-Sleep -Milliseconds 500
Save-Screenshot 'battery-tab' | Out-Null
Toggle-Tab 3

# ---- Advanced Pump tab ----------------------------------------------------------------------------
Toggle-Tab 4
$area = Tab-Area 4
Save-Screenshot 'pump-tab' | Out-Null
$mark = Get-LogMark
Gui-Click ($area[0] + 39 + 9) ($area[1] + 24 + 9)  # world interaction on
Wait-LogLine 'native screen action: tab by .* \(ok\)' 10 -Since $mark | Out-Null
Gui-Move ($area[0] + 57 + 9) ($area[1] + 24 + 9)   # hover the hand toggle (two-line tooltip)
Start-Sleep -Milliseconds 600
Save-Screenshot 'pump-tab-hand-tooltip' | Out-Null
# Fluid filter: the lava bucket on the cursor sets slot 1.
$lava = Hotbar-Pos 3
Gui-Click $lava[0] $lava[1]
$mark = Get-LogMark
Gui-Click ($area[0] + 3 + 18 + 9) ($area[1] + 44 + 9)
Wait-LogLine 'native screen action: tab by .* \(ok\)' 10 -Since $mark | Out-Null
Gui-Click $lava[0] $lava[1]
Start-Sleep -Milliseconds 600
Gui-Move ($area[0] + 3 + 18 + 9) ($area[1] + 44 + 9)
Start-Sleep -Milliseconds 500
Save-Screenshot 'pump-tab-filter' | Out-Null
Toggle-Tab 4

# ---- Experience Pump tab ----------------------------------------------------------------------------
Toggle-Tab 5
$area = Tab-Area 5
Save-Screenshot 'xppump-tab' | Out-Null
Gui-Move ($area[0] + 21 + 27) ($area[1] + 24 + 9)  # level selector: scroll up twice
Scroll 1
Start-Sleep -Milliseconds 300
Scroll 1
Start-Sleep -Milliseconds 800
Save-Screenshot 'xppump-level' | Out-Null
$mark = Get-LogMark
Gui-Click ($area[0] + 3 + 9) ($area[1] + 24 + 9)   # direction: input -> output
Wait-LogLine 'native screen action: tab by .* \(ok\)' 10 -Since $mark | Out-Null
Gui-Move ($area[0] + 39 + 9) ($area[1] + 64 + 9)   # hover "Store N Levels"
Start-Sleep -Milliseconds 600
Save-Screenshot 'xppump-store-tooltip' | Out-Null
$mark = Get-LogMark
Gui-Click ($area[0] + 21 + 9) ($area[1] + 64 + 9)  # take 1 level
Wait-LogLine 'native screen action: tab by .* \(ok\)' 10 -Since $mark | Out-Null
Toggle-Tab 5

# ---- close -----------------------------------------------------------------------------------------
$mark = Get-LogMark
Press-Key 'E'
Wait-LogLine 'native screen action: close by' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 800
Save-Screenshot 'closed' | Out-Null
Assert-NoErrors
