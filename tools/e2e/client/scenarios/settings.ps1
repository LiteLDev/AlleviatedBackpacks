# P2-2: Sophisticated Backpacks' settings screen, driven with real input in the native client UI:
# tabs (Back To Backpack, Backpack, Item Display, No Sort, Memory), the context button and main
# toggles, slot selection by click and drag, colour / rotate / depth buttons, the rotatable preview,
# and the template Save / Load / Export controls. Works in `-Mode local` and `-Mode remote-mod`.
#
# Coordinates come from the mod's layout log lines (GUI units + GUI scale).

Wait-LogLine 'companion: server ready' 90 | Out-Null
Write-Step 'companion handshake done'

Invoke-Chat '/clear @s'
$mark = Get-LogMark
Invoke-Chat '/backpack companion setup'
Wait-LogLine 'companion setup: gave netherite backpack' 15 -Since $mark | Out-Null
Press-Key '1'
Start-Sleep -Milliseconds 600

$script:S = $null
function Read-SettingsLayout($mark) {
    $line = Wait-LogLine 'settings screen: layout left=' 15 -Since $mark
    if ($line -notmatch 'left=([\d.]+) top=([\d.]+) width=(\d+) height=(\d+) invLabel=(\d+),(\d+) tabs=(\d+) scroll=(\w+)') { throw "unexpected settings layout line: $line" }
    $script:S = [pscustomobject]@{
        Left = [double]$Matches[1]; Top = [double]$Matches[2]; Width = [int]$Matches[3]; Height = [int]$Matches[4]
        InvX = [int]$Matches[5]; InvY = [int]$Matches[6]; Tabs = [int]$Matches[7]; Inv = 1.0
    }
    $l = Wait-LogLine 'companion: screen layout session=' 15
    if ($l -match 'invGuiScale=([\d.]+)') { $script:S.Inv = [double]$Matches[1] }
    Write-Step ("settings layout: {0}x{1} at {2},{3} tabs={4} guiScale=1/{5}" -f $S.Width, $S.Height, $S.Left, $S.Top, $S.Tabs, $S.Inv)
}
function Px([double]$gx) { ($S.Left + $gx) / $S.Inv }
function Py([double]$gy) { ($S.Top + $gy) / $S.Inv }
function S-Move([double]$gx, [double]$gy) { Move-Mouse (Px $gx) (Py $gy) }
function S-Click([double]$gx, [double]$gy, [switch]$Right) { Click (Px $gx) (Py $gy) -Right:$Right; Start-Sleep -Milliseconds 350 }
function Slot-Pos([int]$i) { $cols = if ($S.Width -ge 230) { 12 } else { 9 }; @((8 + ($i % $cols) * 18 + 8), (18 + [math]::Floor($i / $cols) * 18 + 8)) }
# Tab i (0 = Back To Backpack): icon centre.
function Tab-Icon([int]$i) { @(($S.Width + 1 + 8), (4 + 25 * $i + 4 + 8)) }
# An open tab covers the tab icons below it (SettingsTabControl): close it first, like a player would.
$script:OpenTab = -1
function Tab-Click([int]$i) {
    if ($script:OpenTab -ge 0 -and $script:OpenTab -ne $i) { $q = Tab-Icon $script:OpenTab; S-Click $q[0] $q[1]; $script:OpenTab = -1 }
    $p = Tab-Icon $i; S-Click $p[0] $p[1]; Start-Sleep -Milliseconds 400
    if ($i -eq 0) { $script:OpenTab = -1 } elseif ($script:OpenTab -eq $i) { $script:OpenTab = -1 } else { $script:OpenTab = $i }
}
# Open tab widgets are relative to the tab's top-left (left + width, top + 4 + 25 * i).
function TabW-Pos([int]$i, [double]$dx, [double]$dy) { @(($S.Width + $dx), (4 + 25 * $i + $dy)) }
function Expect-Setting([int]$mark, [string]$what) { Wait-LogLine 'native screen action: setting by .* \(ok\)' 10 -Since $mark | Out-Null }
function Drag-Slots([int[]]$slots, [switch]$Right) {
    $b = if ($Right) { 1 } else { 0 }
    $p = Slot-Pos $slots[0]
    S-Move $p[0] $p[1]
    [AbpE2E.Native]::MouseButton($b, $false); Start-Sleep -Milliseconds 120
    foreach ($s in $slots) { $q = Slot-Pos $s; S-Move $q[0] $q[1]; Start-Sleep -Milliseconds 150 }
    [AbpE2E.Native]::MouseButton($b, $true); Start-Sleep -Milliseconds 400
}
# Template control buttons (left of the background, below the inventory label).
function Tpl-Pos([int]$row) { @(($S.InvX - 29 + 8), ($S.InvY + 29 + 18 * $row + 8)) }

# ---- open the backpack, then its settings screen -----------------------------------------------------
$mark = Get-LogMark
Press-Key 'B'
Wait-LogLine 'companion: drawing the native backpack screen|companion: screen layout session=' 10 -Since $mark | Out-Null
$line = Wait-LogLine 'companion: screen layout session=' 15 -Since $mark
if ($line -notmatch 'left=([\d.]+) top=([\d.]+) width=(\d+) .*invGuiScale=([\d.]+)') { throw "layout: $line" }
$bw = [int]$Matches[3]; $bl = [double]$Matches[1]; $bt = [double]$Matches[2]; $binv = [double]$Matches[4]
Start-Sleep -Milliseconds 700
$mark = Get-LogMark
Click (($bl + $bw + 1 + 8) / $binv) (($bt + 4 + 4 + 8) / $binv)
Wait-LogLine 'settings screen: opened' 10 -Since $mark | Out-Null
Wait-LogLine 'native screen action: settings by' 10 -Since $mark | Out-Null
Read-SettingsLayout $mark
if ($S.Tabs -ne 5) { throw "expected 5 settings tabs (Back, Backpack, Item Display, No Sort, Memory), got $($S.Tabs)" }
if ($S.Height -ne 114 + 18 * [math]::Floor(($S.Height - 114) / 18)) { throw "height must be 114 + rows*18" }
S-Move ($S.Width / 2) -20
Start-Sleep -Milliseconds 600
Save-Screenshot 'settings-open' | Out-Null
if ((Get-LeviLog -Since $mark) -match 'menu shown: ') { throw 'a fake chest was opened' }

# Closed tab tooltips.
$p = Tab-Icon 0; S-Move $p[0] $p[1]; Start-Sleep -Milliseconds 400; Save-Screenshot 'tab-back-tooltip' | Out-Null
$p = Tab-Icon 3; S-Move $p[0] $p[1]; Start-Sleep -Milliseconds 400; Save-Screenshot 'tab-nosort-tooltip' | Out-Null

# ---- Backpack (main) tab: context button + toggles ------------------------------------------------------
Tab-Click 1
$p = TabW-Pos 1 (3 + 31) (24 + 9); S-Move $p[0] $p[1]; Start-Sleep -Milliseconds 400
Save-Screenshot 'main-tab-context-player' | Out-Null
$mark = Get-LogMark
S-Click $p[0] $p[1]
Expect-Setting $mark 'context'
Start-Sleep -Milliseconds 400
Save-Screenshot 'main-tab-context-backpack' | Out-Null
foreach ($k in 0..3) {
    $q = TabW-Pos 1 (3 + 18 * $k + 8) (46 + 8)
    S-Move $q[0] $q[1]; Start-Sleep -Milliseconds 350
    Save-Screenshot ("main-toggle-{0}" -f $k) | Out-Null
}
$mark = Get-LogMark
$q = TabW-Pos 1 (3 + 18 + 8) (46 + 8)
S-Click $q[0] $q[1]
Expect-Setting $mark 'keepTabOpen'
Start-Sleep -Milliseconds 400
Save-Screenshot 'main-keep-tab-toggled' | Out-Null
S-Click $q[0] $q[1]
S-Click $p[0] $p[1] # back to the Player context

# ---- Item Display tab: select a slot, colour, rotate, depth, preview drag -------------------------------
Tab-Click 2
Start-Sleep -Milliseconds 1200 # preview turns to its target rotation
Save-Screenshot 'item-display-tab' | Out-Null
$mark = Get-LogMark
$s0 = Slot-Pos 0
S-Click $s0[0] $s0[1]
Expect-Setting $mark 'itemDisplay select'
Start-Sleep -Milliseconds 400
Save-Screenshot 'item-display-selected' | Out-Null
$rot = TabW-Pos 2 (3 + 18 + 8) (24 + 72 + 1 + 8)
$mark = Get-LogMark
S-Click $rot[0] $rot[1]
S-Click $rot[0] $rot[1]
Expect-Setting $mark 'rotate'
$depth = TabW-Pos 2 (3 + 36 + 8) (24 + 72 + 1 + 8)
S-Click $depth[0] $depth[1]
$col = TabW-Pos 2 (3 + 9) (24 + 72 + 1 + 9)
S-Click $col[0] $col[1]
Start-Sleep -Milliseconds 500
S-Move $rot[0] $rot[1]; Start-Sleep -Milliseconds 300
Save-Screenshot 'item-display-rotated' | Out-Null
# Drag on the preview turns the backpack.
$pv = TabW-Pos 2 (3 + 36) (24 + 36)
S-Move $pv[0] $pv[1]
[AbpE2E.Native]::MouseButton(0, $false); Start-Sleep -Milliseconds 100
foreach ($k in 1..8) { S-Move ($pv[0] + 3 * $k) ($pv[1] + $k); Start-Sleep -Milliseconds 60 }
[AbpE2E.Native]::MouseButton(0, $true)
Start-Sleep -Milliseconds 400
Save-Screenshot 'item-display-preview-dragged' | Out-Null
$s1 = Slot-Pos 1
$mark = Get-LogMark
S-Click $s1[0] $s1[1]
Wait-LogLine 'native screen action: setting by .* \(nothing to do\)' 10 -Since $mark | Out-Null # SB: one slot only
S-Click $s0[0] $s0[1] -Right
Start-Sleep -Milliseconds 400
Save-Screenshot 'item-display-unselected' | Out-Null
S-Click $s0[0] $s0[1]

# ---- No Sort tab: click, drag, colour -------------------------------------------------------------------
Tab-Click 3
$mark = Get-LogMark
$p = Slot-Pos 2; S-Click $p[0] $p[1]
Expect-Setting $mark 'noSort select'
Drag-Slots @(3, 4, 5, 6)
$c = TabW-Pos 3 (39 + 9) (24 + 9)
S-Click $c[0] $c[1]
S-Click $c[0] $c[1] -Right
S-Click $c[0] $c[1]
Start-Sleep -Milliseconds 400
S-Move $c[0] $c[1]; Start-Sleep -Milliseconds 300
Save-Screenshot 'nosort-tab' | Out-Null
Drag-Slots @(5, 6) -Right

# ---- Memory tab: click, match NBT -------------------------------------------------------------------------
Tab-Click 4
$mark = Get-LogMark
$p = Slot-Pos 8; S-Click $p[0] $p[1]
Expect-Setting $mark 'memory select'
$n = TabW-Pos 4 (39 + 8) (24 + 8)
S-Click $n[0] $n[1]
Start-Sleep -Milliseconds 400
S-Move $n[0] $n[1]; Start-Sleep -Milliseconds 300
Save-Screenshot 'memory-tab-match-nbt' | Out-Null
S-Click $n[0] $n[1]
$p = Tab-Icon 4; S-Move ($p[0] + 2) ($p[1] - 2); Start-Sleep -Milliseconds 400
Save-Screenshot 'memory-tab-open-tooltip' | Out-Null

# ---- templates: save, save named, export, load ------------------------------------------------------------
$sv = Tpl-Pos 0; $ld = Tpl-Pos 1; $ex = Tpl-Pos 2
S-Move $ld[0] $ld[1]; Start-Sleep -Milliseconds 400
Save-Screenshot 'template-load-none' | Out-Null
S-Move $sv[0] $sv[1]; Start-Sleep -Milliseconds 400
Save-Screenshot 'template-save-tooltip' | Out-Null
$mark = Get-LogMark
S-Click $sv[0] $sv[1]
Wait-LogLine "settings: .* saved template slot '1'" 10 -Since $mark | Out-Null
# Scroll the save slot to "Enter Slot Name", type a name, Enter.
S-Move $sv[0] $sv[1]
Scroll -1
Start-Sleep -Milliseconds 300
Scroll -1
Start-Sleep -Milliseconds 300
S-Move $sv[0] ($sv[1] + 1); Start-Sleep -Milliseconds 400
Save-Screenshot 'template-save-name-box' | Out-Null
$mark = Get-LogMark
Type-Text 'Ores'
Press-Key ENTER
Wait-LogLine "settings: .* saved template slot 'Ores'" 10 -Since $mark | Out-Null
# Export with a file name.
S-Move $ex[0] $ex[1]; Start-Sleep -Milliseconds 400
$mark = Get-LogMark
Type-Text 'E2E Ores'
Save-Screenshot 'template-export-box' | Out-Null
Press-Key ENTER
Wait-LogLine 'settings: .* exported template to .*e2e_ores.json' 10 -Since $mark | Out-Null
# Clear the no-sort / memory slots, then hover Load (template preview) and load slot 1.
Tab-Click 3
$a = TabW-Pos 3 (21 + 8) (24 + 8); S-Click $a[0] $a[1]
Tab-Click 4
$a = TabW-Pos 4 (21 + 8) (24 + 8); S-Click $a[0] $a[1]
Tab-Click 4
S-Move $ld[0] $ld[1]; Start-Sleep -Milliseconds 500
Save-Screenshot 'template-load-hover-preview' | Out-Null
# Scroll through the load slots to the exported pack template (shows its source).
foreach ($k in 1..2) { Scroll -1; Start-Sleep -Milliseconds 300 }
S-Move $ld[0] ($ld[1] + 1); Start-Sleep -Milliseconds 400
Save-Screenshot 'template-load-pack-source' | Out-Null
foreach ($k in 1..2) { Scroll 1; Start-Sleep -Milliseconds 300 }
$mark = Get-LogMark
S-Click $ld[0] $ld[1]
Wait-LogLine "settings: .* loaded template slot '1'" 10 -Since $mark | Out-Null
S-Move ($S.Width / 2) -20; Start-Sleep -Milliseconds 500
Save-Screenshot 'template-loaded' | Out-Null

# ---- back to the backpack: ESC, the stripes show in the storage view --------------------------------------
Press-Key ESC
Start-Sleep -Milliseconds 700
Save-Screenshot 'back-to-storage' | Out-Null
# Back To Backpack tab from a reopened settings screen.
$mark = Get-LogMark
Click (($bl + $bw + 1 + 8) / $binv) (($bt + 4 + 4 + 8) / $binv)
Wait-LogLine 'settings screen: opened' 10 -Since $mark | Out-Null
$script:OpenTab = -1
Start-Sleep -Milliseconds 500
Save-Screenshot 'settings-reopened-player-context' | Out-Null
Tab-Click 0
Start-Sleep -Milliseconds 500
Save-Screenshot 'back-tab-to-storage' | Out-Null
$mark = Get-LogMark
Press-Key 'E'
Wait-LogLine 'native screen action: close by' 10 -Since $mark | Out-Null
Assert-NoErrors
