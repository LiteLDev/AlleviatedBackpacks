# G2 (client companion): the native Sophisticated Backpacks screen, key bindings and the Shift
# contents tooltip, driven with real input. Works in `-Mode local` and `-Mode remote-mod` (the
# server side runs in the integrated server or on BDS; the client always draws natively).
#
# Coordinates: the mod logs the screen layout in GUI units ("companion: screen layout ...") with
# the GUI scale, so clicks are computed from SB's layout values, not hard-coded pixels.

Wait-LogLine 'companion: server ready' 90 | Out-Null
Write-Step 'companion handshake done'

Invoke-Chat '/clear @s'
$mark = Get-LogMark
Invoke-Chat '/backpack companion setup'
Wait-LogLine 'companion setup: gave netherite backpack' 15 -Since $mark | Out-Null
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
        Visible = [int]$Matches[5]; Rows = [int]$Matches[6]; Scroll = $Matches[7] -eq 'true'; Inv = [double]$Matches[8]
        InvX = [int]$Matches[9]; InvY = [int]$Matches[10]; Upgrades = [int]$Matches[11]; Tabs = [int]$Matches[12]
    }
    Write-Step ("layout: {0}x{1} at {2},{3} rows {4}/{5} scroll={6} guiScale=1/{7}" -f $L.Width, $L.Height, $L.Left, $L.Top, $L.Visible, $L.Rows, $L.Scroll, $L.Inv)
}
# GUI units relative to the backpack background -> client pixels.
function Gui-Move([double]$gx, [double]$gy) { Move-Mouse (($L.Left + $gx) / $L.Inv) (($L.Top + $gy) / $L.Inv) }
function Gui-Click([double]$gx, [double]$gy, [switch]$Right) { Click (($L.Left + $gx) / $L.Inv) (($L.Top + $gy) / $L.Inv) -Right:$Right; Start-Sleep -Milliseconds 400 }
function Storage-Pos([int]$i) { $cols = if ($L.Width -ge 230) { 12 } else { 9 }; @((8 + ($i % $cols) * 18 + 8), (18 + [math]::Floor($i / $cols) * 18 + 8)) }
function Key-Combo([int]$mod, [string]$key) {
    Assert-GameForeground
    [AbpE2E.Native]::Key([uint16]$mod, $false); Start-Sleep -Milliseconds 60
    Press-Key $key
    [AbpE2E.Native]::Key([uint16]$mod, $true); Start-Sleep -Milliseconds 300
}

# ---- B opens the carried backpack as the native SB screen ------------------------------------------
$mark = Get-LogMark
Press-Key 'B'
Wait-LogLine 'key action: .* opens the carried backpack' 10 -Since $mark | Out-Null
Wait-LogLine 'companion: drawing the native backpack screen' 10 -Since $mark | Out-Null
Read-Layout $mark
if ($L.Width -ne 230 -and $L.Width -ne 236) { throw "netherite backpack: expected the 12 column background (230/236 wide), got $($L.Width)" }
if ($L.Height -ne 114 + 18 * $L.Visible) { throw "height must be 114 + rows*18, got $($L.Height) for $($L.Visible) rows" }
Start-Sleep -Milliseconds 800
Save-Screenshot 'native-open' | Out-Null
if ((Get-LeviLog -Since $mark) -match 'menu shown: ') { throw 'a fake chest was opened for a companion player' }

# Hover a storage slot (tooltip with SB's count line).
$p = Storage-Pos 0
Gui-Move $p[0] $p[1]
Start-Sleep -Milliseconds 500
Save-Screenshot 'slot-tooltip' | Out-Null

# Scroll the storage (InventoryScrollPanel when the rows don't fit).
$p = Storage-Pos 13
Gui-Move $p[0] $p[1]
Scroll -2
Start-Sleep -Milliseconds 400
Save-Screenshot 'scrolled' | Out-Null
Scroll 5
Start-Sleep -Milliseconds 300

# Search box: animated, filters and re-flows the slots.
Gui-Click ($L.Width - 37) 10
Start-Sleep -Milliseconds 400
Type-Text 'dia'
Start-Sleep -Milliseconds 600
Save-Screenshot 'search-dia' | Out-Null
Press-Key ESC
Gui-Click ($L.Width - 37) 10 -Right
Start-Sleep -Milliseconds 400
Gui-Move 100 3
Start-Sleep -Milliseconds 400
Save-Screenshot 'search-cleared' | Out-Null

# Sort and sort-by buttons (title line, right).
$mark = Get-LogMark
Gui-Click ($L.Width - 31 + 6) 10
Wait-LogLine 'native screen action: sort by' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 500
Save-Screenshot 'sorted' | Out-Null
$mark = Get-LogMark
Gui-Click ($L.Width - 19 + 6) 10
Wait-LogLine 'native screen action: sortBy by' 10 -Since $mark | Out-Null
Gui-Move ($L.Width - 19 + 6) 10
Start-Sleep -Milliseconds 500
Save-Screenshot 'sort-by-tooltip' | Out-Null

# Transfer to inventory (Shift: everything) and back to storage.
$mark = Get-LogMark
[AbpE2E.Native]::Key(0x10, $false)
Gui-Click ($L.InvX + 149 + 6) ($L.InvY - 2 + 6)
[AbpE2E.Native]::Key(0x10, $true)
Wait-LogLine 'native screen action: transfer by .* \(ok\)' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 500
Save-Screenshot 'transferred-out' | Out-Null
$mark = Get-LogMark
[AbpE2E.Native]::Key(0x10, $false)
Gui-Click ($L.InvX + 137 + 6) ($L.InvY - 2 + 6)
[AbpE2E.Native]::Key(0x10, $true)
Wait-LogLine 'native screen action: transfer by .* \(ok\)' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 500

# Take a stack onto the cursor and put it back (server-authoritative clicks).
$p = Storage-Pos 1
Gui-Click $p[0] $p[1]
Start-Sleep -Milliseconds 300
Gui-Move ($p[0] + 30) ($p[1] + 10)
Start-Sleep -Milliseconds 400
Save-Screenshot 'cursor-item' | Out-Null
Gui-Click $p[0] $p[1]

# Alt+Z: toggle upgrade 1.
$mark = Get-LogMark
Key-Combo 0x12 'Z'
Wait-LogLine 'key action: .* switched upgrade 1' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 600
Save-Screenshot 'upgrade1-toggled' | Out-Null
Key-Combo 0x12 'Z'

# Upgrade tab (first upgrade with settings) and the settings screen.
if ($L.Tabs -gt 1) {
    Gui-Click ($L.Width + 1 + 8) (4 + 25 + 4 + 8)
    Start-Sleep -Milliseconds 700
    Save-Screenshot 'upgrade-tab' | Out-Null
    Gui-Click ($L.Width + 1 + 8) (4 + 25 + 4 + 8)
}
Gui-Click ($L.Width + 1 + 8) (4 + 4 + 8)
Start-Sleep -Milliseconds 700
Save-Screenshot 'settings' | Out-Null
Gui-Click ($L.Width + 1 + 8) (4 + 75 + 4 + 8)
Start-Sleep -Milliseconds 500
$p = Storage-Pos 2
$mark = Get-LogMark
Gui-Click $p[0] $p[1]
Wait-LogLine 'native screen action: setting by' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 500
Save-Screenshot 'settings-memory' | Out-Null
Press-Key ESC
Start-Sleep -Milliseconds 500
Save-Screenshot 'back-to-storage' | Out-Null

# Shift over the backpack in the player inventory: SB's contents tooltip.
$hot0 = @(($L.InvX + 8), ($L.InvY + 12 + 54 + 4 + 8))
Gui-Move $hot0[0] $hot0[1]
Start-Sleep -Milliseconds 400
Save-Screenshot 'press-shift-hint' | Out-Null
Assert-GameForeground
[AbpE2E.Native]::Key(0x10, $false)
Start-Sleep -Milliseconds 1500
Gui-Move ($hot0[0] + 1) $hot0[1]
Start-Sleep -Milliseconds 600
Save-Screenshot 'contents-tooltip' | Out-Null
[AbpE2E.Native]::Key(0x10, $true)

# E closes the screen.
$mark = Get-LogMark
Press-Key 'E'
Wait-LogLine 'native screen action: close by' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 800
Save-Screenshot 'closed' | Out-Null

# Vanilla inventory: Shift over the backpack shows the contents tooltip there too.
Open-Inventory
Save-Screenshot 'vanilla-inventory' | Out-Null
Close-Screen

# C on a chest: the Deposit upgrade moves the cobblestone into it.
Invoke-Chat '/tp @s ~ ~ ~ 0 29'
Invoke-Chat '/setblock ~ ~ ~2 chest'
Start-Sleep -Milliseconds 800
$mark = Get-LogMark
Press-Key 'C'
$line = Wait-LogLine 'key action: .* ran inventory interaction \((\d+) stacks deposited' 10 -Since $mark
if ($line -notmatch 'interaction \(([1-9]\d*) stacks deposited') { throw "C deposited nothing: $line" }
Save-Screenshot 'deposited' | Out-Null
Assert-NoErrors
