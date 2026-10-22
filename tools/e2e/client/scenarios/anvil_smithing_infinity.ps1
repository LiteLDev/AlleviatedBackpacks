# P2-10: Anvil, Smithing and (Survival) Infinity upgrades in the native SB screen, driven with
# real input. Works in `-Mode local` and `-Mode remote-mod`.
#   1. anvil tab: sword + diamonds, rename it inline with the keyboard, take the result
#   2. smithing tab: netherite upgrade of a chestplate with the armor stand preview, shift-click
#      the result into the backpack
#   3. survival infinity backpack: slots show "∞" and taking stacks never depletes them
# Coordinates come from the logged screen layout (GUI units) like the companion scenario.

Wait-LogLine 'companion: server ready' 90 | Out-Null
Write-Step 'companion handshake done'

Invoke-Chat '/clear @s'
$mark = Get-LogMark
Invoke-Chat '/backpack station setup'
Wait-LogLine 'station setup: station' 15 -Since $mark | Out-Null
Press-Key '1'
Start-Sleep -Milliseconds 600

# ---- layout helpers (as in companion.ps1) ---------------------------------------------------------
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
    Write-Step ("layout: {0}x{1} at {2},{3} guiScale=1/{4} tabs={5}" -f $L.Width, $L.Height, $L.Left, $L.Top, $L.Inv, $L.Tabs)
}
function Gui-Move([double]$gx, [double]$gy) { Move-Mouse (($L.Left + $gx) / $L.Inv) (($L.Top + $gy) / $L.Inv) }
function Gui-Click([double]$gx, [double]$gy, [switch]$Right) { Click (($L.Left + $gx) / $L.Inv) (($L.Top + $gy) / $L.Inv) -Right:$Right; Start-Sleep -Milliseconds 450 }
function Shift-Click([double]$gx, [double]$gy) {
    Assert-GameForeground
    [AbpE2E.Native]::Key(0x10, $false); Start-Sleep -Milliseconds 80
    Gui-Click $gx $gy
    [AbpE2E.Native]::Key(0x10, $true); Start-Sleep -Milliseconds 200
}
function Key-Combo([int]$mod, [string]$key) {
    Assert-GameForeground
    [AbpE2E.Native]::Key([uint16]$mod, $false); Start-Sleep -Milliseconds 60
    Press-Key $key
    [AbpE2E.Native]::Key([uint16]$mod, $true); Start-Sleep -Milliseconds 300
}
# Player inventory: main slot i (9..35) / hotbar slot i (0..8) centres.
function Inv-Pos([int]$i) {
    if ($i -lt 9) { return @(($L.InvX + 8 + $i * 18), ($L.InvY + 12 + 58 + 8)) }
    $r = [math]::Floor(($i - 9) / 9); $c = ($i - 9) % 9
    return @(($L.InvX + 8 + $c * 18), ($L.InvY + 12 + $r * 18 + 8))
}
function Storage-Pos([int]$i) { $cols = if ($L.Width -ge 230) { 12 } else { 9 }; @((8 + ($i % $cols) * 18 + 8), (18 + [math]::Floor($i / $cols) * 18 + 8)) }
# Upgrade tab n (0 = backpack settings): the tab's top-left and its icon centre.
function Tab-Origin([int]$n) { @($L.Width, (4 + 25 * $n)) }
function Wait-TabAction($mark) { Wait-LogLine 'native screen action: tab by' 10 -Since $mark | Out-Null; Start-Sleep -Milliseconds 400 }
function Close-Backpack {
    $m = Get-LogMark
    Press-Key 'E'
    Wait-LogLine 'native screen action: close by' 10 -Since $m | Out-Null
    Start-Sleep -Milliseconds 700
}
function Open-Backpack {
    $m = Get-LogMark
    Press-Key 'B'
    Wait-LogLine 'companion: drawing the native backpack screen' 10 -Since $m | Out-Null
    Read-Layout $m
    Start-Sleep -Milliseconds 500
}
# Chat only opens without a screen: closes the backpack, probes and leaves it closed.
function Probe {
    Close-Backpack
    Start-Sleep -Milliseconds 1000
    for ($try = 1; ; $try++) {
        $m = Get-LogMark
        Invoke-Chat '/backpack station probe'
        try {
            Wait-LogLine 'station probe: level' 8 -Since $m | Out-Null
            return (Get-LeviLog -Since $m) -join "`n"
        } catch {
            # The chat did not open in time (the typed letters went to the game): back out, retry.
            if ($try -ge 3) { throw }
            Write-Step "probe: chat did not open, retrying ($try)"
            Press-Key ESC; Start-Sleep -Milliseconds 1200
        }
    }
}

# ---- open the station backpack ---------------------------------------------------------------------
$mark = Get-LogMark
Press-Key 'B'
Wait-LogLine 'companion: drawing the native backpack screen' 10 -Since $mark | Out-Null
Read-Layout $mark
if ($L.Tabs -lt 3) { throw "expected the settings, anvil and smithing tabs, got $($L.Tabs)" }
if ((Get-LeviLog -Since $mark) -match 'menu shown: ') { throw 'a fake chest was opened for a companion player' }

# ---- 1. anvil tab ------------------------------------------------------------------------------------
$t = Tab-Origin 1
Gui-Click ($t[0] + 1 + 8) ($t[1] + 4 + 8)
Start-Sleep -Milliseconds 600
Save-Screenshot 'anvil-tab' | Out-Null

$left = @(($t[0] + 4 + 8), ($t[1] + 43 + 8)); $right = @(($t[0] + 42 + 8), ($t[1] + 43 + 8)); $result = @(($t[0] + 80 + 8), ($t[1] + 43 + 8))
$p = Inv-Pos 9; Gui-Click $p[0] $p[1]                  # damaged sword on the cursor
$mark = Get-LogMark; Gui-Click $left[0] $left[1]; Wait-TabAction $mark
$p = Inv-Pos 10; Gui-Click $p[0] $p[1]                 # diamonds
$mark = Get-LogMark; Gui-Click $right[0] $right[1]; Wait-TabAction $mark
Gui-Move ($t[0] + 50) ($t[1] + 85)
Start-Sleep -Milliseconds 700
Save-Screenshot 'anvil-inputs' | Out-Null

# Inline rename with the keyboard: focus the name box, select all, type, Enter.
Gui-Click ($t[0] + 40) ($t[1] + 32)
Start-Sleep -Milliseconds 300
Key-Combo 0x11 'A'
$mark = Get-LogMark
Type-Text 'Excalibur'
Start-Sleep -Milliseconds 800
Save-Screenshot 'anvil-typing' | Out-Null
Press-Key 'ENTER'
Wait-LogLine 'native screen action: tab by' 10 -Since $mark | Out-Null
Gui-Move $result[0] $result[1]
Start-Sleep -Milliseconds 900
Save-Screenshot 'anvil-renamed-result' | Out-Null

$mark = Get-LogMark
Gui-Click $result[0] $result[1]                        # take it (cursor)
Wait-TabAction $mark
$p = Inv-Pos 14; Gui-Click $p[0] $p[1]                 # put it down
Start-Sleep -Milliseconds 500
Save-Screenshot 'anvil-taken' | Out-Null
Gui-Click ($t[0] + 1 + 8) ($t[1] + 4 + 8)              # close the anvil tab
$probe = Probe
if ($probe -notmatch "inv 14 minecraft:diamond_sword x1 name='Excalibur'") { throw "the renamed sword is not in inventory slot 14:`n$probe" }
if ($probe -notmatch 'station probe: level 26') { throw "4 levels (repair 3 + rename 1) should have been paid:`n$probe" }
Write-Step 'anvil: repaired and renamed inline'

# ---- 2. smithing tab -----------------------------------------------------------------------------------
Open-Backpack
$t = Tab-Origin 2
Gui-Click ($t[0] + 1 + 8) ($t[1] + 4 + 8)
Start-Sleep -Milliseconds 600
Save-Screenshot 'smithing-tab' | Out-Null
$templ = @(($t[0] + 4 + 8), ($t[1] + 25 + 8)); $base = @(($t[0] + 22 + 8), ($t[1] + 25 + 8)); $add = @(($t[0] + 40 + 8), ($t[1] + 25 + 8)); $res = @(($t[0] + 80 + 8), ($t[1] + 25 + 8))
$p = Inv-Pos 11; Gui-Click $p[0] $p[1]
$mark = Get-LogMark; Gui-Click $templ[0] $templ[1]; Wait-TabAction $mark
$p = Inv-Pos 13; Gui-Click $p[0] $p[1]
$mark = Get-LogMark; Gui-Click $base[0] $base[1]; Wait-TabAction $mark
Gui-Move ($t[0] + 30) ($t[1] + 120)
Start-Sleep -Milliseconds 600
Save-Screenshot 'smithing-two-inputs' | Out-Null
$p = Inv-Pos 12; Gui-Click $p[0] $p[1]
$mark = Get-LogMark; Gui-Click $add[0] $add[1]; Wait-TabAction $mark
Gui-Move ($t[0] + 30) ($t[1] + 120)
Start-Sleep -Milliseconds 800
Save-Screenshot 'smithing-preview' | Out-Null
Gui-Move $res[0] $res[1]
Start-Sleep -Milliseconds 700
Save-Screenshot 'smithing-result-hover' | Out-Null
$mark = Get-LogMark
Shift-Click $res[0] $res[1]
Wait-TabAction $mark
Start-Sleep -Milliseconds 500
Save-Screenshot 'smithing-taken' | Out-Null
$probe = Probe
if ($probe -notmatch 'slot \d+ minecraft:netherite_chestplate x1') { throw "the netherite chestplate did not go into the backpack:`n$probe" }
Write-Step 'smithing: netherite chestplate shift-clicked into the backpack'

# ---- 3. survival infinity backpack ----------------------------------------------------------------------
Press-Key '2'
Start-Sleep -Milliseconds 500
Open-Backpack
Save-Screenshot 'infinity-open' | Out-Null
$s0 = Storage-Pos 0
Gui-Move $s0[0] $s0[1]
Start-Sleep -Milliseconds 600
Save-Screenshot 'infinity-hover' | Out-Null
foreach ($target in 20, 21) {
    Gui-Click $s0[0] $s0[1]                # a full stack onto the cursor
    $p = Inv-Pos $target; Gui-Click $p[0] $p[1]
}
Gui-Move ($s0[0] + 40) ($s0[1] + 4)
Start-Sleep -Milliseconds 600
Save-Screenshot 'infinity-after-taking' | Out-Null
$probe = Probe
if ($probe -notmatch 'inv 20 minecraft:diamond x64' -or $probe -notmatch 'inv 21 minecraft:diamond x64') { throw "two full stacks of diamonds expected:`n$probe" }
if ($probe -notmatch 'slot 0 minecraft:diamond x1 ') { throw "the infinite slot must still hold its diamond:`n$probe" }
Write-Step 'infinity: slot shows the infinity sign and never depletes'
Assert-NoErrors
