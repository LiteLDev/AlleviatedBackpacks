# P2-9: Jukebox, Refill, Tool Swapper, Inception and Everlasting with real input on the native SB
# screen (works in `-Mode local` and `-Mode remote-mod`).
#
# `/backpack misc setup` gives a netherite backpack (hotbar 0) with, in upgrade slots 1..5: Advanced
# Jukebox (cat / blocks / 13), Advanced Refill, Advanced Tool Swapper, Inception, Everlasting; a
# leather backpack in storage slot 0 and a diamond shovel, iron sword, shears, torches and bread;
# an iron pickaxe in hotbar 5. Tabs (25 apart from y=4): 0 settings, 1 jukebox, 2 refill,
# 3 tool swapper, 4 inception (Everlasting has no tab).

Wait-LogLine 'companion: server ready' 90 | Out-Null
Write-Step 'companion handshake done'

Invoke-Chat '/gamemode survival @s'                                    # the tool swapper never swaps in creative
$mark = Get-LogMark
Invoke-Chat '/backpack misc setup'
Wait-LogLine 'misc setup: gave netherite backpack' 15 -Since $mark | Out-Null
Invoke-Chat '/tp @s ~ ~ ~ 0 20'
$by = 24 + 54 + 3                                                      # jukebox buttons row (12 slots, 4 per row)

# ---- layout helpers (GUI units, from the mod's layout log line; see companion.ps1) -----------------
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
function Gui-Click([double]$gx, [double]$gy, [switch]$Right) { Click (($L.Left + $gx) / $L.Inv) (($L.Top + $gy) / $L.Inv) -Right:$Right; Start-Sleep -Milliseconds 450 }
function Storage-Pos([int]$i) { $cols = if ($L.Width -ge 230) { 12 } else { 9 }; @((8 + ($i % $cols) * 18 + 8), (18 + [math]::Floor($i / $cols) * 18 + 8)) }
# Tab i: top-left (Width, 4 + 25 i); its icon at +1,+4.
function Tab-Y([int]$i) { 4 + 25 * $i }
function Tab-Toggle([int]$i) { Gui-Click ($L.Width + 1 + 8) ((Tab-Y $i) + 4 + 8); Start-Sleep -Milliseconds 300 }
# A widget at (dx, dy) of tab i (SB positions are relative to the tab's top-left), centre of an 18x18 button.
function Tab-Button([int]$i, [double]$dx, [double]$dy) { @(($L.Width + $dx + 9), ((Tab-Y $i) + $dy + 9)) }

# Opens the backpack in hotbar slot 0 by using it (right click). B also opens it, but B is the game's
# emote key too: its hotkey list then takes the number keys and chat for a while.
function Open-Backpack {
    Press-Key '1'
    Start-Sleep -Milliseconds 400
    $m = Get-LogMark
    Click 0.5 0.5 -Relative -Right
    Wait-LogLine 'companion: drawing the native backpack screen' 10 -Since $m | Out-Null
    Read-Layout $m
    Start-Sleep -Milliseconds 600
    if ((Get-LeviLog -Since $m) -match 'menu shown: ') { throw 'a fake chest was opened for a companion player' }
}

# ---- Jukebox in the world: the record follows the carrier; dropping the backpack stops it ------------
Open-Backpack
Tab-Toggle 1
$mark = Get-LogMark
$p = Tab-Button 1 39 $by; Gui-Click $p[0] $p[1]
Wait-LogLine 'jukebox: playing abp.jukebox3d.' 10 -Since $mark | Out-Null
Press-Key 'E'
Wait-LogLine 'jukebox: sound of backpack .* active=true' 15 -Since $mark | Out-Null
Save-Screenshot 'jukebox-in-world' | Out-Null                          # note particles above the player
$mark = Get-LogMark
Press-Key 'Q'                                                          # drop the (everlasting) backpack
Wait-LogLine 'jukebox: stopped in backpack .*no longer playable' 10 -Since $mark | Out-Null
Wait-LogLine 'jukebox: stopping the sound of backpack' 10 -Since $mark | Out-Null
Start-Sleep -Seconds 3
Save-Screenshot 'everlasting-dropped' | Out-Null                       # happy villager particles

# A fresh backpack for the rest (the first one was dropped).
$mark = Get-LogMark
Invoke-Chat '/backpack misc setup'
Wait-LogLine 'misc setup: gave netherite backpack' 15 -Since $mark | Out-Null
Start-Sleep -Milliseconds 800

# ---- Tool Swapper in the world: mining grass with the pickaxe takes the shovel ----------------------
Invoke-Chat '/tp @s ~ ~ ~ 0 90'
Start-Sleep -Milliseconds 600
Press-Key '5'                                                          # iron pickaxe (hotbar 5)
Start-Sleep -Milliseconds 400
$mark = Get-LogMark
Click 0.5 0.5 -Relative
Wait-LogLine 'tool swapper: .* swapped minecraft:iron_pickaxe for minecraft:diamond_shovel' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 600
Save-Screenshot 'toolswapper-mined' | Out-Null
# The tool swap key (G, bound by the prelaunch script): the shovel flattens grass, the backpack has
# no other tool for it.
$mark = Get-LogMark
Press-Key 'G'
Start-Sleep -Milliseconds 800
$key = (Get-LeviLog -Since $mark) | Select-String 'key action: .* tool swap -> (\d)' | Select-Object -Last 1
if ($key) { Write-Step "tool swap key: $($key.Line)" } else { Write-Step 'tool swap key: not bound in this client (skipped)' }
Save-Screenshot 'toolswapper-key' | Out-Null

# ---- the backpack screen with its tabs -------------------------------------------------------------
Open-Backpack
if ($L.Tabs -ne 5) { throw "expected 5 tabs (settings, jukebox, refill, tool swapper, inception; no Everlasting tab), got $($L.Tabs)" }
Save-Screenshot 'misc-open' | Out-Null
Gui-Move ($L.Width + 1 + 8) ((Tab-Y 1) + 12)
Start-Sleep -Milliseconds 500
Save-Screenshot 'jukebox-tab-closed-tooltip' | Out-Null

# ---- Jukebox (advanced: 12 slots, 4 per row -> buttons at y = 24 + 54 + 3) ---------------------------
Tab-Toggle 1
Save-Screenshot 'jukebox-tab' | Out-Null
$by = 24 + 54 + 3
$mark = Get-LogMark
$p = Tab-Button 1 39 $by; Gui-Click $p[0] $p[1]                       # Play
Wait-LogLine 'jukebox: track abp.jukebox3d.cat \(disc slot 0\)' 10 -Since $mark | Out-Null
Wait-LogLine 'jukebox: playing abp.jukebox3d.cat for backpack .* attached to entity' 10 -Since $mark | Out-Null
$active = Wait-LogLine 'jukebox: sound of backpack .* active=' 15 -Since $mark
if ($active -notmatch 'active=true') { throw "the attached record is not playing: $active" }
Start-Sleep -Seconds 2
Save-Screenshot 'jukebox-playing' | Out-Null                          # green remaining-time overlay on disc 1
$mark = Get-LogMark
$p = Tab-Button 1 57 $by; Gui-Click $p[0] $p[1]                       # Next
Wait-LogLine 'jukebox: track abp.jukebox3d.blocks \(disc slot 1\)' 10 -Since $mark | Out-Null
$mark = Get-LogMark
$p = Tab-Button 1 3 $by; Gui-Click $p[0] $p[1]                        # Previous
Wait-LogLine 'jukebox: track abp.jukebox3d.cat \(disc slot 0\)' 10 -Since $mark | Out-Null
$p = Tab-Button 1 12 ($by + 20); Gui-Click $p[0] $p[1]                # Shuffle
$p = Tab-Button 1 48 ($by + 20); Gui-Click $p[0] $p[1]                # Repeat: Disabled -> All
Gui-Move $p[0] $p[1]
Start-Sleep -Milliseconds 600
Save-Screenshot 'jukebox-repeat-tooltip' | Out-Null
$p = Tab-Button 1 12 ($by + 20); Gui-Move $p[0] $p[1]
Start-Sleep -Milliseconds 600
Save-Screenshot 'jukebox-shuffle-tooltip' | Out-Null
$mark = Get-LogMark
$p = Tab-Button 1 21 $by; Gui-Click $p[0] $p[1]                       # Stop
Wait-LogLine 'jukebox: stopped in backpack' 10 -Since $mark | Out-Null
Wait-LogLine 'jukebox: stopping the sound of backpack' 10 -Since $mark | Out-Null
Tab-Toggle 1

# ---- Refill (advanced: 12 slots, 4 per row at x+3, y+24; slot 1 = torch -> hotbar 3) ---------------
Tab-Toggle 2
Save-Screenshot 'refill-tab' | Out-Null                               # acronyms 3 / M
$slot0 = @(($L.Width + 3 + 1 + 8), ((Tab-Y 2) + 24 + 1 + 8))
Gui-Move $slot0[0] $slot0[1]
Start-Sleep -Milliseconds 500
$mark = Get-LogMark
Scroll 1
Wait-LogLine 'refill: .* set filter slot 1 target to [46]' 10 -Since $mark | Out-Null   # hotbar 3 -> hotbar 4 (or 2)
Gui-Move ($slot0[0] + 1) $slot0[1]
Start-Sleep -Milliseconds 700
Save-Screenshot 'refill-scroll-tooltip' | Out-Null                    # "Refill Hotbar slot 4" + "Scroll to change target slot"
$mark = Get-LogMark
Scroll -1
Wait-LogLine 'refill: .* set filter slot 1 target to 5' 10 -Since $mark | Out-Null
Tab-Toggle 2

# ---- Tool Swapper (Swap Weapon at x+3,y+24; mode at x+21; filter buttons at y+44, slots at y+65) -----
Tab-Toggle 3
Save-Screenshot 'toolswapper-tab' | Out-Null
$p = Tab-Button 3 3 24; Gui-Move $p[0] $p[1]
Start-Sleep -Milliseconds 600
Save-Screenshot 'toolswapper-weapon-tooltip' | Out-Null
$mark = Get-LogMark
$p = Tab-Button 3 21 24; Gui-Click $p[0] $p[1]
Wait-LogLine 'tool swapper: .* tab action mode' 10 -Since $mark | Out-Null
Gui-Move $p[0] $p[1]
Start-Sleep -Milliseconds 600
Save-Screenshot 'toolswapper-mode-tooltip' | Out-Null                 # Only Swap For Other Tools
Gui-Click $p[0] $p[1]; Gui-Click $p[0] $p[1]                          # back to Swap Tools
$p = Tab-Button 3 3 44; Gui-Move $p[0] $p[1]
Start-Sleep -Milliseconds 600
Save-Screenshot 'toolswapper-filter-tooltip' | Out-Null
Tab-Toggle 3

# ---- Inception (one toggle at x+3, y+24) -------------------------------------------------------------
Tab-Toggle 4
$p = Tab-Button 4 3 24; Gui-Move $p[0] $p[1]
Start-Sleep -Milliseconds 600
Save-Screenshot 'inception-tab' | Out-Null
$mark = Get-LogMark
Gui-Click $p[0] $p[1]
Wait-LogLine 'inception: .* inventory order to incepted_first' 10 -Since $mark | Out-Null
Gui-Move $p[0] ($p[1] + 1)
Start-Sleep -Milliseconds 600
Save-Screenshot 'inception-incepted-first' | Out-Null
Gui-Click $p[0] $p[1]
Tab-Toggle 4
$mark = Get-LogMark
Press-Key 'E'
Wait-LogLine 'native screen action: close by' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 600

# ---- B over the backpack stored inside opens it; ESC goes back -------------------------------------
# (Last: B is also the game's emote key.)
Invoke-Chat '/tp @s ~ ~ ~ 0 20'
Open-Backpack
$s0 = Storage-Pos 0
Gui-Move $s0[0] $s0[1]
Start-Sleep -Milliseconds 400
$mark = Get-LogMark
Press-Key 'B'
Wait-LogLine 'native screen action: openNested by' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 900
Save-Screenshot 'nested-open' | Out-Null                               # only the settings tab
$mark = Get-LogMark
Press-Key 'ESC'
Wait-LogLine 'native screen action: back by' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 900
Save-Screenshot 'nested-back' | Out-Null
$mark = Get-LogMark
Press-Key 'E'
Wait-LogLine 'native screen action: close by' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 600
Assert-NoErrors
