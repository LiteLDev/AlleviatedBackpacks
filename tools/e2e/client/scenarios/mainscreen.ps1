# P2-1 (main backpack screen): Sophisticated Backpacks' storage screen, driven with real input in
# every tier (leather -> netherite): layout (9/12 columns, wider backgrounds with the scroll bar),
# scrolling (wheel + bar), the search box (animation, @mod, AND terms, no results, right-click
# clear, kept phrase), sort + every sort-by mode, both transfer modes (+ Shift tooltips), big counts,
# the drag-split preview (yellow cap), memory / no-sort overlays, the "Count" tooltip line, the
# error overlay, upgrade switches, the tab column (kept open tab), Shift + double-click and
# Ctrl + Shift + click. Works in `-Mode local` and `-Mode remote-mod`; the sort button position
# variants (client config + /backpack reload) run in local mode only.
#
# Coordinates come from the layout the mod logs ("companion: screen layout ...") in GUI units.

Wait-LogLine 'companion: server ready' 90 | Out-Null
Write-Step 'companion handshake done'
$remote = $script:Mode -and $script:Mode -ne 'local'

$script:L = $null
function Read-Layout($mark) {
    $line = Wait-LogLine 'companion: screen layout session=' 15 -Since $mark
    if ($line -notmatch 'left=([\d.]+) top=([\d.]+) width=(\d+) height=(\d+) rows=(\d+)/(\d+) scroll=(\w+) invGuiScale=([\d.]+) screen=([\d.]+)x([\d.]+) invLabel=(\d+),(\d+) upgrades=(\d+) tabs=(\d+) slots=(\d+) cols=(\d+) sort=(-?\d+),(-?\d+)') {
        throw "unexpected layout line: $line"
    }
    $script:L = [pscustomobject]@{
        Left = [double]$Matches[1]; Top = [double]$Matches[2]; Width = [int]$Matches[3]; Height = [int]$Matches[4]
        Visible = [int]$Matches[5]; Rows = [int]$Matches[6]; Scroll = $Matches[7] -eq 'true'; Inv = [double]$Matches[8]
        ScreenW = [double]$Matches[9]; ScreenH = [double]$Matches[10]
        InvX = [int]$Matches[11]; InvY = [int]$Matches[12]; Upgrades = [int]$Matches[13]; Tabs = [int]$Matches[14]
        Slots = [int]$Matches[15]; Cols = [int]$Matches[16]; SortX = [int]$Matches[17]; SortY = [int]$Matches[18]
    }
    Write-Step ("layout: {0}x{1} at {2},{3} rows {4}/{5} scroll={6} cols={7} screen={8}x{9} sort={10},{11}" -f $L.Width, $L.Height, $L.Left, $L.Top, $L.Visible, $L.Rows, $L.Scroll, $L.Cols, $L.ScreenW, $L.ScreenH, $L.SortX, $L.SortY)
}
function Gui-Move([double]$gx, [double]$gy) { Move-Mouse (($L.Left + $gx) / $L.Inv) (($L.Top + $gy) / $L.Inv) }
function Gui-Click([double]$gx, [double]$gy, [switch]$Right) { Click (($L.Left + $gx) / $L.Inv) (($L.Top + $gy) / $L.Inv) -Right:$Right; Start-Sleep -Milliseconds 350 }
function Gui-Down([int]$b = 0) { Assert-GameForeground; [AbpE2E.Native]::MouseButton($b, $false); Start-Sleep -Milliseconds 120 }
function Gui-Up([int]$b = 0) { Assert-GameForeground; [AbpE2E.Native]::MouseButton($b, $true); Start-Sleep -Milliseconds 250 }
# Storage slot centre (first visible row = row 0).
function Storage-Pos([int]$i) { @((8 + ($i % $L.Cols) * 18 + 8), (18 + [math]::Floor($i / $L.Cols) * 18 + 8)) }
function Hotbar-Pos([int]$i) { @(($L.InvX + $i * 18 + 8), ($L.InvY + 12 + 54 + 4 + 8)) }
function Upgrade-Pos([int]$i) { @(-7, (6 + $i * 16 + 8)) }
function Tab-Pos([int]$i) { @(($L.Width + 1 + 8), (4 + $i * 25 + 4 + 8)) }
function With-Key([int]$vk, [scriptblock]$body) {
    Assert-GameForeground
    [AbpE2E.Native]::Key([uint16]$vk, $false); Start-Sleep -Milliseconds 80
    try { & $body } finally { [AbpE2E.Native]::Key([uint16]$vk, $true); Start-Sleep -Milliseconds 150 }
}
# $slots: the tier's slots; $taken: storage columns taken by tank / battery bars (2 each).
function Expect-Layout([int]$slots, [int]$taken = 0) {
    $cols = if ($slots -le 81) { 9 } else { 12 }
    $rows = [math]::Ceiling($slots / $cols)
    $fit  = [math]::Floor(($L.ScreenH - 114) / 18)
    $vis  = [math]::Min($rows, [math]::Max(1, $fit))
    $w    = $cols * 18 + 14 + $(if ($vis -lt $rows) { 6 } else { 0 })
    $shown = $slots - $taken * $rows
    if ($L.Width -ne $w -or $L.Height -ne 114 + 18 * $vis -or $L.Rows -ne $rows -or $L.Visible -ne $vis -or $L.Cols -ne $cols - $taken -or $L.Slots -ne $shown) {
        throw "layout for $slots slots ($taken taken): expected ${w}x$(114 + 18 * $vis) rows $vis/$rows cols $($cols - $taken) slots $shown, got $($L.Width)x$($L.Height) rows $($L.Visible)/$($L.Rows) cols $($L.Cols) slots $($L.Slots)"
    }
}
$takenOf = @{ leather = 0; copper = 0; iron = 0; gold = 0; diamond = 4; netherite = 4 }
function Open-Backpack {
    $mark = Get-LogMark
    Press-Key 'B'
    Wait-LogLine 'companion: drawing the native backpack screen|key action: .* opens the carried backpack' 10 -Since $mark | Out-Null
    Read-Layout $mark
    Start-Sleep -Milliseconds 700
}
function Close-Backpack {
    $mark = Get-LogMark
    Press-Key 'E'
    Wait-LogLine 'native screen action: close by' 10 -Since $mark | Out-Null
    # The game drops a T pressed right after the screen closed (remote servers need longer).
    Start-Sleep -Milliseconds $(if ($remote) { 3500 } else { 1500 })
}
# A fresh backpack of `tier` (plus a spare magnet upgrade); retried when the chat did not open (the
# game sometimes drops the T right after a screen closed).
function Setup-Tier([string]$tier) {
    for ($attempt = 1; $attempt -le 3; $attempt++) {
        $mark = Get-LogMark
        Invoke-Chat '/clear @s'
        Start-Sleep -Milliseconds 500
        Invoke-Chat "/backpack mainscreen setup $tier"
        try {
            Wait-LogLine "mainscreen setup: gave $tier backpack" 8 -Since $mark | Out-Null
            Start-Sleep -Milliseconds 500
            return
        } catch {
            Write-Step "setup ${tier}: no answer (attempt $attempt), retrying"
            Save-Screenshot "setup-$tier-retry-$attempt" | Out-Null
            Press-Key ESC # e.g. the emote wheel ('b' typed outside the chat)
            Start-Sleep -Milliseconds 1500
        }
    }
    throw "mainscreen setup $tier failed"
}

$slotsOf = @{ leather = 27; copper = 45; iron = 54; gold = 81; diamond = 108; netherite = 120 }

# ---- every tier: layout, counts, overlays ----------------------------------------------------------
foreach ($tier in 'leather', 'copper', 'iron', 'gold', 'diamond', 'netherite') {
    Setup-Tier $tier
    Open-Backpack
    Expect-Layout $slotsOf[$tier] $takenOf[$tier]
    Gui-Move ($L.Width / 2) -12
    Start-Sleep -Milliseconds 300
    Save-Screenshot "$tier-open" | Out-Null
    if ((Get-LeviLog) -match 'menu shown: ') { throw 'a fake chest was opened for a companion player' }
    # The big count of slot 0 and SC's "Count: n / max" line (shown when the slot limit is not the
    # item's own max stack size, e.g. ender pearls).
    $p = Storage-Pos 0
    Gui-Move $p[0] $p[1]
    Start-Sleep -Milliseconds 400
    Save-Screenshot "$tier-count-tooltip" | Out-Null
    if ($takenOf[$tier] -gt 0) {
        # Tank and battery bars right of the slots (36 units each): hover the tank.
        $partX = 7 + $L.Cols * 18 + $(if ($L.Scroll) { 6 } else { 0 })
        Gui-Move ($partX + 18) (17 + 20)
        Start-Sleep -Milliseconds 400
        Save-Screenshot "$tier-tank-bar" | Out-Null
    }
    if ($tier -eq 'netherite' -or $tier -eq 'leather') {
        $p = Storage-Pos 10 # ender pearls
        Gui-Move $p[0] $p[1]
        Start-Sleep -Milliseconds 400
        Save-Screenshot "$tier-pearl-tooltip" | Out-Null
        $p = Storage-Pos 1 # memory slot
        Gui-Move $p[0] $p[1]
        Start-Sleep -Milliseconds 400
        Save-Screenshot "$tier-memory-tooltip" | Out-Null
    }
    Close-Backpack
}

# ---- leather: drag split preview (yellow capped count), quick move all, Ctrl+Shift ------------------
Setup-Tier 'leather'
Open-Backpack
# Pick up the 29 cobblestone of slot 12, drag over slot 6 (empty), 13 (empty) and 0 (64 cobblestone).
$p12 = Storage-Pos 12; $p6 = Storage-Pos 6; $p13 = Storage-Pos 13; $p0 = Storage-Pos 0
Gui-Click $p12[0] $p12[1]
Start-Sleep -Milliseconds 400
Gui-Move $p6[0] $p6[1]
Gui-Down 0
Gui-Move ($p6[0] + 2) ($p6[1] + 1)
Gui-Move $p13[0] $p13[1]
Gui-Move $p0[0] $p0[1]
Start-Sleep -Milliseconds 400
Save-Screenshot 'leather-drag-preview' | Out-Null
$mark = Get-LogMark
Gui-Up 0
Start-Sleep -Milliseconds 600
Save-Screenshot 'leather-drag-done' | Out-Null
# Cursor holds the rest (29 - 9 - 9 = 11): put it into slot 12 again.
Gui-Click $p12[0] $p12[1]

# Shift + double-click: the first click moves slot 24's cobblestone, the second every other
# cobblestone stack of the storage (vanilla lastQuickMoved).
$p24 = Storage-Pos 24
$mark = Get-LogMark
With-Key 0x10 {
    # Two clicks within vanilla's 250 ms double-click window.
    Gui-Move $p24[0] $p24[1]
    [AbpE2E.Native]::MouseButton(0, $false); Start-Sleep -Milliseconds 40; [AbpE2E.Native]::MouseButton(0, $true)
    Start-Sleep -Milliseconds 80
    [AbpE2E.Native]::MouseButton(0, $false); Start-Sleep -Milliseconds 40; [AbpE2E.Native]::MouseButton(0, $true)
    Start-Sleep -Milliseconds 400
}
Wait-LogLine 'native screen action: quickMoveAll by .* \(ok\)' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 500
Save-Screenshot 'leather-quick-move-all' | Out-Null
Close-Backpack

# ---- netherite: transfer, sort-by modes, search, error overlay, switches, tabs --------------------
Setup-Tier 'netherite'
Open-Backpack
# Transfer buttons: tooltips without / with Shift, then both modes.
$toInv = @(($L.InvX + 149 + 6), ($L.InvY - 2 + 6)); $toSto = @(($L.InvX + 137 + 6), ($L.InvY - 2 + 6))
Gui-Move $toSto[0] $toSto[1]
Start-Sleep -Milliseconds 400
Save-Screenshot 'transfer-tooltip' | Out-Null
With-Key 0x10 {
    Gui-Move ($toSto[0] + 1) $toSto[1]
    Start-Sleep -Milliseconds 400
    Save-Screenshot 'transfer-tooltip-shift' | Out-Null
}
$mark = Get-LogMark
With-Key 0x10 { Gui-Click $toInv[0] $toInv[1] }
Wait-LogLine 'native screen action: transfer by .* \(ok\)' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 500
Save-Screenshot 'transferred-all-out' | Out-Null
$mark = Get-LogMark
Gui-Click $toSto[0] $toSto[1]       # matching only: everything that came out goes back
Wait-LogLine 'native screen action: transfer by .* \(ok\)' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 500
Save-Screenshot 'transferred-matching-in' | Out-Null

# Ctrl + Shift + click: the whole 1,024 stack of slot 0 to the inventory, then back with Shift.
With-Key 0x11 { With-Key 0x10 { Gui-Click $p0[0] $p0[1] } }
Start-Sleep -Milliseconds 500
Save-Screenshot 'ctrl-shift-full-slot' | Out-Null
With-Key 0x10 { Gui-Click $toSto[0] $toSto[1] }
Start-Sleep -Milliseconds 400

# Sort-by: Name -> Mod -> Count -> Tags -> Name; sort in each mode.
$sortBy = @(($L.SortX + 12 + 6), ($L.SortY + 6)); $sort = @(($L.SortX + 6), ($L.SortY + 6))
Gui-Move $sort[0] $sort[1]
Start-Sleep -Milliseconds 400
Save-Screenshot 'sort-tooltip' | Out-Null
foreach ($sortMode in 'mod', 'count', 'tags', 'name') {
    $mark = Get-LogMark
    Gui-Click $sortBy[0] $sortBy[1]
    Wait-LogLine 'native screen action: sortBy by' 10 -Since $mark | Out-Null
    Gui-Move $sortBy[0] ($sortBy[1] + 1)
    Start-Sleep -Milliseconds 400
    Save-Screenshot "sort-by-$sortMode" | Out-Null
    $mark = Get-LogMark
    Gui-Click $sort[0] $sort[1]
    Wait-LogLine 'native screen action: sort by' 10 -Since $mark | Out-Null
    Gui-Move ($L.Width / 2) -12
    Start-Sleep -Milliseconds 500
    Save-Screenshot "sorted-$sortMode" | Out-Null
}

# Search: hover tooltip, focus (animation), name + @mod AND terms, no results, right-click clear.
$search = @(($L.SortX - 6), 10)
Gui-Move $search[0] $search[1]
Start-Sleep -Milliseconds 400
Save-Screenshot 'search-tooltip' | Out-Null
Gui-Click $search[0] $search[1]
Start-Sleep -Milliseconds 80
Save-Screenshot 'search-expanding' | Out-Null
Start-Sleep -Milliseconds 400
Type-Text 'dia'
Start-Sleep -Milliseconds 400
Save-Screenshot 'search-dia' | Out-Null
Type-Text ' @mine'
Start-Sleep -Milliseconds 400
Save-Screenshot 'search-dia-mod' | Out-Null
Type-Text 'zzz'
Start-Sleep -Milliseconds 400
Save-Screenshot 'search-no-results' | Out-Null
Press-Key ESC
Gui-Click $search[0] $search[1] -Right
Start-Sleep -Milliseconds 500
Gui-Move ($L.Width / 2) -12
Start-Sleep -Milliseconds 400
Save-Screenshot 'search-cleared' | Out-Null
# Kept search phrase (SC default on): type, close, reopen.
Gui-Click $search[0] $search[1]
Start-Sleep -Milliseconds 300
Type-Text 'ingot'
Start-Sleep -Milliseconds 300
Press-Key ESC
Close-Backpack
Open-Backpack
Save-Screenshot 'search-kept' | Out-Null
$search = @(($L.SortX - 6), 10)
Gui-Click $search[0] $search[1] -Right
Start-Sleep -Milliseconds 400

# Error overlay: a second magnet upgrade into a free upgrade slot.
$h1 = Hotbar-Pos 1
Gui-Click $h1[0] $h1[1]
$u = Upgrade-Pos 5
Gui-Click $u[0] $u[1]
Start-Sleep -Milliseconds 300
Save-Screenshot 'error-overlay' | Out-Null
Start-Sleep -Milliseconds 3500
Save-Screenshot 'error-expired' | Out-Null
Gui-Click $h1[0] $h1[1]

# Upgrade switch: ON tooltip, switch off (OFF tooltip), back on.
$sw = @(-19, (8 + 6))
Gui-Move $sw[0] $sw[1]
Start-Sleep -Milliseconds 400
Save-Screenshot 'switch-on' | Out-Null
$mark = Get-LogMark
Gui-Click $sw[0] $sw[1]
Wait-LogLine 'native screen action: toggleUpgrade by' 10 -Since $mark | Out-Null
Gui-Move $sw[0] ($sw[1] + 1)
Start-Sleep -Milliseconds 400
Save-Screenshot 'switch-off' | Out-Null
Gui-Click $sw[0] $sw[1]

# Tabs: the settings tab tooltip, open the magnet tab, close and reopen the screen (kept tab).
$t0 = Tab-Pos 0
Gui-Move $t0[0] $t0[1]
Start-Sleep -Milliseconds 400
Save-Screenshot 'tab-settings-tooltip' | Out-Null
if ($L.Tabs -gt 1) {
    $t1 = Tab-Pos 1
    $mark = Get-LogMark
    Gui-Click $t1[0] $t1[1]
    Wait-LogLine 'native screen action: openTab by' 10 -Since $mark | Out-Null
    Start-Sleep -Milliseconds 500
    Save-Screenshot 'tab-open' | Out-Null
    Close-Backpack
    Open-Backpack
    Save-Screenshot 'tab-kept-open' | Out-Null
    $t1 = Tab-Pos 1
    Gui-Click $t1[0] $t1[1]
    Start-Sleep -Milliseconds 400
}
Close-Backpack

# ---- scrolling: a shorter window (fewer GUI rows) --------------------------------------------------
foreach ($h in 640, 600, 560) {
    Set-GameWindowSize 1600 $h
    Focus-Game
    Open-Backpack
    if ($L.Scroll) { break }
    Close-Backpack
}
if (-not $L.Scroll) { throw "no scroll bar at any window height (rows $($L.Visible)/$($L.Rows))" }
Expect-Layout 120 4
Save-Screenshot 'scroll-top' | Out-Null
$p = Storage-Pos 13
Gui-Move $p[0] $p[1]
Scroll -2
Start-Sleep -Milliseconds 400
Save-Screenshot 'scroll-wheel' | Out-Null
# Drag the bar to the bottom.
$barX = 7 + $L.Cols * 18 + 3
Gui-Move $barX 22
Gui-Down 0
Gui-Move $barX 60
Gui-Move $barX ($L.Height - 100)
Gui-Up 0
Start-Sleep -Milliseconds 400
Save-Screenshot 'scroll-bar-dragged' | Out-Null
Close-Backpack
Set-GameWindowSize

# ---- sort button positions (client config; local mode: /backpack reload reaches the client) ---------
if (-not $remote) {
    $cfg = Join-Path (Join-Path (Join-Path $script:ModsDir $script:ModName) 'config') 'client_gui.json'
    foreach ($pos in 'below_upgrades', 'below_upgrade_tabs', 'hidden', 'title_line_right') {
        "{ `"version`": 1, `"sortButtonsPosition`": `"$pos`", `"playButtonSound`": true, `"restrictScrollToScrollbar`": false }" | Set-Content -Path $cfg -Encoding UTF8
        Invoke-Chat '/backpack reload'
        Start-Sleep -Milliseconds 500
        Open-Backpack
        Save-Screenshot "sort-position-$pos" | Out-Null
        Close-Backpack
    }
}
Assert-NoErrors
