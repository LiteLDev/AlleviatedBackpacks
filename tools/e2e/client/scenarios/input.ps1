# P2-14: Sophisticated Backpacks' key bindings, right-click stashing and item tooltips, driven with
# (headless) input in the vanilla screens and the native backpack screen. Works in `-Mode local`
# and `-Mode remote-mod`. input.prelaunch.ps1 rebinds keys in options.txt (Tool Swap = V, Toggle
# Upgrade 3 = N, Open Backpack also on the mouse side button X1): the saved bindings must be used.
# Our keys must not also reach the game: B (Bedrock's emote key) and E never open the emote list.
# The headless input has no mouse side buttons, so the X1 binding is only checked as loaded;
# pressing it needs real hardware.
#
# Positions: the native screen layout comes from the companion's layout log line (GUI units), the
# vanilla screens' slot positions from "stash: vanilla slots ..." (logged once per screen).

function Middle-Click { [AbpE2E.Native]::MouseButton(2, $false); Start-Sleep -Milliseconds 60; [AbpE2E.Native]::MouseButton(2, $true); Start-Sleep -Milliseconds 250 }

Wait-LogLine 'companion: server ready' 90 | Out-Null
Write-Step 'companion handshake done'
$keys = (Get-LeviLog -All | Select-String -Pattern 'keys: ' | Select-Object -Last 1).Line
Write-Step "bindings: $keys"
if ($keys -notmatch 'open_backpack=66,-96' -or $keys -notmatch 'tool_swap=86' -or $keys -notmatch 'toggle_upgrade_3=78') {
    throw "the key bindings saved in options.txt were not applied: $keys"
}
function Assert-NoEmote([string]$when) {
    if ((Get-LeviLog -All) -match 'input: screen \S*emote') { throw "the emote list opened ()" }
}

function Key-Combo([int]$mod, [string]$key) {
    Assert-GameForeground
    [AbpE2E.Native]::Key([uint16]$mod, $false); Start-Sleep -Milliseconds 60
    Press-Key $key
    [AbpE2E.Native]::Key([uint16]$mod, $true); Start-Sleep -Milliseconds 300
}
function Tap-Vk([int]$vk) { Assert-GameForeground; [AbpE2E.Native]::Key([uint16]$vk, $false); Start-Sleep -Milliseconds 80; [AbpE2E.Native]::Key([uint16]$vk, $true); Start-Sleep -Milliseconds 300 }
function Hold([int]$vk) { Assert-GameForeground; [AbpE2E.Native]::Key([uint16]$vk, $false) }
function Release([int]$vk) { [AbpE2E.Native]::Key([uint16]$vk, $true); Start-Sleep -Milliseconds 100 }

# ---- native screen layout -------------------------------------------------------------------------
$script:L = $null
function Read-Layout($mark) {
    $line = Wait-LogLine 'companion: screen layout session=' 15 -Since $mark
    if ($line -notmatch 'left=([\d.]+) top=([\d.]+) width=(\d+) height=(\d+) rows=(\d+)/(\d+) scroll=(\w+) invGuiScale=([\d.]+) .*invLabel=(\d+),(\d+)') {
        throw "unexpected layout line: $line"
    }
    $script:L = [pscustomobject]@{
        Left = [double]$Matches[1]; Top = [double]$Matches[2]; Width = [int]$Matches[3]; Height = [int]$Matches[4]
        Inv = [double]$Matches[8]; InvX = [int]$Matches[9]; InvY = [int]$Matches[10]
    }
}
function Gui-Move([double]$gx, [double]$gy) { Move-Mouse (($L.Left + $gx) / $L.Inv) (($L.Top + $gy) / $L.Inv) }
function Gui-Click([double]$gx, [double]$gy, [switch]$Right) { Gui-Move $gx $gy; Start-Sleep -Milliseconds 250; Click (($L.Left + $gx) / $L.Inv) (($L.Top + $gy) / $L.Inv) -Right:$Right; Start-Sleep -Milliseconds 400 }
function Storage-Pos([int]$i) { $cols = if ($L.Width -ge 230) { 12 } else { 9 }; @((8 + ($i % $cols) * 18 + 8), (18 + [math]::Floor($i / $cols) * 18 + 8)) }
function Hotbar-Pos([int]$i) { @(($L.InvX + $i * 18 + 8), ($L.InvY + 12 + 54 + 4 + 8)) }

# ---- vanilla screen slots ---------------------------------------------------------------------------
$script:V = $null
function Read-VanillaSlots($mark) {
    Wait-LogLine 'stash: vanilla slots screen=' 15 -Since $mark | Out-Null
    Start-Sleep -Milliseconds 1500   # the positions are logged again while the screen settles: take the last
    $line = (Get-LeviLog -Since $mark | Select-String -Pattern 'stash: vanilla slots screen=' | Select-Object -Last 1).Line
    if ($line -notmatch 'screen=(\S*) invGuiScale=([\d.]+)') { throw "unexpected slots line: $line" }
    $slots = @{}
    foreach ($m in [regex]::Matches($line, '(\w+:\d+)=(-?[\d.]+),(-?[\d.]+),([\d.]+)')) {
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
function V-Move([string]$key) { $p = V-Pos $key; Move-Mouse $p[0] $p[1] }
# A slot $dx columns / $dy rows away from a seen slot (18 GUI units per slot).
function V-MoveFrom([string]$key, [int]$dx, [int]$dy) { $p = V-Pos $key; Move-Mouse ($p[0] + $dx * 18 / $V.Inv) ($p[1] + $dy * 18 / $V.Inv) }
function V-Click([string]$key, [switch]$Right) {
    $p = V-Pos $key; Move-Mouse ($p[0] + 3) ($p[1] + 3); Start-Sleep -Milliseconds 150; Move-Mouse $p[0] $p[1]; Start-Sleep -Milliseconds 250
    # Held for a few frames: the game polls the (virtual) mouse once per frame.
    $b = if ($Right) { 1 } else { 0 }
    [AbpE2E.Native]::MouseButton($b, $false); Start-Sleep -Milliseconds 200; [AbpE2E.Native]::MouseButton($b, $true); Start-Sleep -Milliseconds 400
    # Let the game finish the right-button gesture (hold / drag) before the next click elsewhere.
    if ($Right) { Move-Mouse ($p[0] + 2) ($p[1] + 2); Start-Sleep -Milliseconds 500; Write-Step ("after right click: " + ((Get-LeviLog -Since ((Get-LogMark) - 6)) -join " | ")) }
}

# ---- setup ------------------------------------------------------------------------------------------------
Invoke-Chat '/clear @s'
Invoke-Chat '/tp @s ~ ~ ~ 0 29'
$mark = Get-LogMark
Invoke-Chat '/backpack input setup'
Wait-LogLine 'input setup: A=' 15 -Since $mark | Out-Null
Press-Key '1'
Start-Sleep -Milliseconds 600

# ---- Open Backpack in game, nested backpack, back, close ---------------------------------------------------
$mark = Get-LogMark
Press-Key 'B'
Wait-LogLine 'key action: .* opens the carried backpack' 10 -Since $mark | Out-Null
Read-Layout $mark
Start-Sleep -Milliseconds 800
Save-Screenshot 'b-open' | Out-Null
$p = Storage-Pos 0
Gui-Move $p[0] $p[1]
Start-Sleep -Milliseconds 300
$mark = Get-LogMark
Press-Key 'B'
Wait-LogLine 'native screen action: openNested by' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 900
Save-Screenshot 'b-nested' | Out-Null
$mark = Get-LogMark
Gui-Move 30 5
Press-Key 'B'
Wait-LogLine 'native screen action: back by' 10 -Since $mark | Out-Null
Read-Layout $mark
Start-Sleep -Milliseconds 900
Save-Screenshot 'b-back-to-parent' | Out-Null
$mark = Get-LogMark
Gui-Move 30 5
Press-Key 'B'
Wait-LogLine 'native screen action: close by' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 800
Save-Screenshot 'b-closed' | Out-Null
Start-Sleep -Milliseconds 600
Assert-NoEmote 'B in game / B to close'

# ---- Toggle upgrades (Alt+Z / Alt+X; N = toggle 3 without modifier) ----------------------------------------
$mark = Get-LogMark
Key-Combo 0x12 'Z'
Wait-LogLine 'key action: .* switched upgrade 1 \(magnet\) off' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 300
Save-Screenshot 'toggle1-off' | Out-Null
$mark = Get-LogMark
Key-Combo 0x12 'Z'
Wait-LogLine 'key action: .* switched upgrade 1 \(magnet\) on' 10 -Since $mark | Out-Null
$mark = Get-LogMark
Press-Key 'Z'
Start-Sleep -Milliseconds 600
if ((Get-LeviLog -Since $mark) -match 'switched upgrade') { throw 'Z without Alt toggled an upgrade' }
$mark = Get-LogMark
Key-Combo 0x12 'X'
Wait-LogLine 'key action: .* switched upgrade 2 \(deposit\) off' 10 -Since $mark | Out-Null
Key-Combo 0x12 'X'
$mark = Get-LogMark
Press-Key 'N'                               # rebound Toggle Upgrade 3 (no Alt): inception can't be switched
Wait-LogLine 'key action: .* toggle upgrade 3: nothing that can be switched' 10 -Since $mark | Out-Null
$mark = Get-LogMark
Press-Key 'V'                               # rebound Tool Swap (backpack in hand: SB refuses)
Wait-LogLine 'key action: .* tool swap -> ' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 300
Save-Screenshot 'toolswap-backpack-in-hand' | Out-Null

# ---- Inventory interaction (C) on a chest -------------------------------------------------------------------
Invoke-Chat '/setblock ~ ~ ~2 chest'
Invoke-Chat '/replaceitem block ~ ~ ~2 slot.container 0 apple 10'
Invoke-Chat '/replaceitem block ~ ~ ~2 slot.container 1 alleviatedbackpacks:leather_backpack 1'
Start-Sleep -Milliseconds 800
$mark = Get-LogMark
Press-Key 'C'
$line = Wait-LogLine 'key action: .* ran inventory interaction \((\d+) stacks deposited' 10 -Since $mark
if ($line -notmatch 'interaction \(([1-9]\d*) stacks deposited') { throw "C deposited nothing: $line" }

# ---- Chest screen: stashing with the backpack on the cursor, B over a backpack ------------------------------
Press-Key '9'                               # empty hand: the right click opens the chest
Start-Sleep -Milliseconds 400
for ($try = 1; ; $try++) {
    $mark = Get-LogMark
    Click 0.5 0.5 -Relative -Right
    Start-Sleep -Milliseconds 1500
    try { Read-VanillaSlots $mark; break } catch { if ($try -ge 3) { throw }; Write-Step "chest not open yet ($_); retrying" }
}
$mark = Get-LogMark
V-Click 'inventory:0'                       # backpack A onto the cursor
V-MoveFrom 'container:0' 6 1                # an empty chest slot (not over a sign)
Wait-LogLine 'stash: [1-9]\d* of \d+ slots stashable' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 400
Save-Screenshot 'chest-minus-signs' | Out-Null
V-Move 'container:0'
Start-Sleep -Milliseconds 700
Save-Screenshot 'chest-right-click-to-add' | Out-Null
$mark = Get-LogMark
V-Click 'container:0' -Right
Wait-LogLine 'key action: .* stash at container:0: 10 moved' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 600
Save-Screenshot 'chest-stashed' | Out-Null
Start-Sleep -Milliseconds 300
# Cursor item onto the backpack in the chest (never opened: SB assumes space).
V-Click 'inventory:4'                       # diamonds
V-MoveFrom 'container:0' 6 1
Start-Sleep -Milliseconds 900
Save-Screenshot 'chest-plus-sign' | Out-Null
$mark = Get-LogMark
V-Click 'container:1' -Right
Wait-LogLine 'key action: .* stash at container:1: 5 moved' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 500
# B over backpack B in the player inventory part: the chest closes, the backpack opens.
V-Move 'inventory:1'
Start-Sleep -Milliseconds 300
$mark = Get-LogMark
Press-Key 'B'
Wait-LogLine 'key: open backpack in .* \(screen ' 10 -Since $mark | Out-Null
Wait-LogLine 'key action: .* opens the backpack at inventory 1' 10 -Since $mark | Out-Null
Read-Layout $mark
Start-Sleep -Milliseconds 800
Save-Screenshot 'chest-b-opened-backpack' | Out-Null
$mark = Get-LogMark
Gui-Move 30 5
Press-Key 'B'
Wait-LogLine 'native screen action: close by' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 900
Save-Screenshot 'chest-b-closed-to-game' | Out-Null

# Fresh items for the inventory part (the chest part moved some around).
Invoke-Chat '/clear @s'
$mark = Get-LogMark
Invoke-Chat '/backpack input setup'
Wait-LogLine 'input setup: A=' 15 -Since $mark | Out-Null
Press-Key '9'
Start-Sleep -Milliseconds 500
# ---- Vanilla inventory: tooltips, stashing, B ---------------------------------
$mark = Get-LogMark
Open-Inventory
Read-VanillaSlots $mark
V-Move 'inventory:0'
Start-Sleep -Milliseconds 600
Save-Screenshot 'inv-press-shift-hint' | Out-Null
Hold 0x10
Start-Sleep -Milliseconds 1500
$p = V-Pos 'inventory:0'; Move-Mouse ($p[0] + 1) $p[1]
Start-Sleep -Milliseconds 600
Save-Screenshot 'inv-contents-tooltip' | Out-Null
Release 0x10
V-Move 'inventory:5'
Start-Sleep -Milliseconds 600
Save-Screenshot 'inv-upgrade-tooltip' | Out-Null
V-Move 'inventory:6'
Start-Sleep -Milliseconds 600
Save-Screenshot 'inv-advanced-tool-swapper-tooltip' | Out-Null
# Cobblestone on the cursor: "+" on the backpacks (green on B: it already holds cobblestone).
$mark = Get-LogMark
V-Click 'inventory:2'
V-MoveFrom 'inventory:0' 8 -1               # an empty inventory slot
Wait-LogLine 'stash: [1-9]\d* of \d+ slots stashable' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 400
Save-Screenshot 'inv-plus-signs' | Out-Null
V-Move 'inventory:1'
Start-Sleep -Milliseconds 700
Save-Screenshot 'inv-right-click-to-add' | Out-Null
$mark = Get-LogMark
V-Click 'inventory:1' -Right
Wait-LogLine 'key action: .* stash at inventory:1: 32 moved' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 500
Save-Screenshot 'inv-stashed-into-slot-backpack' | Out-Null
# Backpack A on the cursor: "-" on the items, right click the dirt.
V-Click 'inventory:0'
V-MoveFrom 'inventory:0' 8 -1
Start-Sleep -Milliseconds 900
Save-Screenshot 'inv-minus-signs' | Out-Null
$mark = Get-LogMark
V-Click 'inventory:3' -Right
Wait-LogLine 'key action: .* stash at inventory:3: 16 moved' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 400
# Fresh items again (placing a wearable backpack back by click equips it in this setup; not ours).
Press-Key 'E'
Start-Sleep -Milliseconds 800
Invoke-Chat '/clear @s'
$mark = Get-LogMark
Invoke-Chat '/backpack input setup'
Wait-LogLine 'input setup: A=' 15 -Since $mark | Out-Null
Press-Key '9'
$mark = Get-LogMark
Open-Inventory
Read-VanillaSlots $mark
# B over A: opens it from the inventory; B again goes back to the inventory screen.
V-Move 'inventory:0'
Start-Sleep -Milliseconds 300
$mark = Get-LogMark
Press-Key 'B'
Wait-LogLine 'key action: .* opens the backpack at inventory 0' 10 -Since $mark | Out-Null
Read-Layout $mark
Start-Sleep -Milliseconds 800
$mark = Get-LogMark
Gui-Move 30 5
Press-Key 'B'
Wait-LogLine 'native screen action: close by' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 1000
Save-Screenshot 'inv-b-back-to-inventory' | Out-Null
Close-Screen

# ---- Native screen: stashing, sort (middle mouse), transfer keys ------------------------------------
$mark = Get-LogMark
Press-Key 'B'
Wait-LogLine 'key action: .* opens the carried backpack' 10 -Since $mark | Out-Null
Read-Layout $mark
Start-Sleep -Milliseconds 800
$h = Hotbar-Pos 1
Gui-Click $h[0] $h[1]                       # backpack B onto the cursor
$p = Storage-Pos 10
Gui-Move $p[0] $p[1]
Start-Sleep -Milliseconds 1000
Save-Screenshot 'native-minus-signs' | Out-Null
$p = Storage-Pos 3
Gui-Move $p[0] $p[1]
Start-Sleep -Milliseconds 600
Save-Screenshot 'native-right-click-to-add' | Out-Null
$mark = Get-LogMark
Gui-Click $p[0] $p[1] -Right
Wait-LogLine 'key action: .* stash at storage:3: [1-9]\d* moved' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 500
Save-Screenshot 'native-stashed' | Out-Null
Gui-Click $h[0] $h[1]                       # B back
# Item on the cursor over the nested backpack in storage slot 0: "+".
$h4 = Hotbar-Pos 4
Gui-Click $h4[0] $h4[1]
$p = Storage-Pos 10
Gui-Move $p[0] $p[1]
Start-Sleep -Milliseconds 1000
Save-Screenshot 'native-plus-sign' | Out-Null
Gui-Click $h4[0] $h4[1]
# Middle mouse sorts (not over the player inventory).
$p = Storage-Pos 5
Gui-Move $p[0] $p[1]
$mark = Get-LogMark
Middle-Click
Wait-LogLine 'native screen action: sort by' 10 -Since $mark | Out-Null
$mark = Get-LogMark
$h = Hotbar-Pos 2
Gui-Move $h[0] $h[1]
Middle-Click
Start-Sleep -Milliseconds 700
if ((Get-LeviLog -Since $mark) -match 'native screen action: sort by') { throw 'middle click over the player inventory sorted' }
# [ / ] transfer (Shift: everything).
$mark = Get-LogMark
Tap-Vk 0xDB                                 # [
Wait-LogLine 'native screen action: transfer by' 10 -Since $mark | Out-Null
$mark = Get-LogMark
Hold 0x10
Tap-Vk 0xDD                                 # ]
Release 0x10
Wait-LogLine 'native screen action: transfer by' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 500
Save-Screenshot 'native-transferred' | Out-Null
$mark = Get-LogMark
Press-Key 'E'
Wait-LogLine 'native screen action: close by' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 1200
Assert-NoEmote 'E to close the native screen'
if ((Get-LeviLog -Since 0) -match 'menu shown: ') { throw 'a fake chest was opened for a companion player' }
Assert-NoErrors
