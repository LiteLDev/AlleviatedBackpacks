# P2-3: the shared native filter control (SC FilterLogicControl / ContentsFilterControl /
# DepositFilterLogicControl) in the Pickup, Filter, Restock and Deposit upgrade tabs, driven with
# real input, plus the Deposit / Restock triggers (C key, sneak + use on a container).
# Works in `-Mode local` and `-Mode remote-mod`.
#
# Coordinates: the backpack screen logs its layout ("companion: screen layout ...") and every
# filter tab logs its control's position in GUI units ("filter control: tab built ... at X,Y"),
# so clicks follow SC's layout values (buttons +0/+18/+36/+54, slots 21 below), not pixels.

Wait-LogLine 'companion: server ready' 90 | Out-Null
Write-Step 'companion handshake done'

Invoke-Chat '/clear @s'
$mark = Get-LogMark
Invoke-Chat '/backpack filters setup'
Wait-LogLine 'filters setup: gave netherite backpack' 15 -Since $mark | Out-Null
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
function Gui-Click([double]$gx, [double]$gy, [switch]$Right) { Click (($L.Left + $gx) / $L.Inv) (($L.Top + $gy) / $L.Inv) -Right:$Right; Start-Sleep -Milliseconds 400 }
# Hotbar slot k (0..8) of the player inventory part.
function Hotbar-Click([int]$k) { Gui-Click ($L.InvX + $k * 18 + 8) ($L.InvY + 12 + 3 * 18 + 4 + 8) }

# The open filter control (absolute GUI units).
$script:C = $null
function Read-Control($mark, [string]$extra = '') {
    $line = Wait-LogLine ("filter control: tab built key=filter at .*$extra") 10 -Since $mark
    if ($line -notmatch 'at ([\d.\-]+),([\d.\-]+) size ([\d.]+)x([\d.]+) slots=(\d+) perRow=(\d+) type=(\d+) primary=(\d+) tags=(\d+)') {
        throw "unexpected control line: $line"
    }
    $script:C = [pscustomobject]@{
        X = [double]$Matches[1]; Y = [double]$Matches[2]; W = [double]$Matches[3]; H = [double]$Matches[4]
        Slots = [int]$Matches[5]; PerRow = [int]$Matches[6]; Type = [int]$Matches[7]; Primary = [int]$Matches[8]; Tags = [int]$Matches[9]
    }
    Write-Step ("filter control at {0},{1} size {2}x{3} slots={4}/{5} primary={6}" -f $C.X, $C.Y, $C.W, $C.H, $C.Slots, $C.PerRow, $C.Primary)
}
function Ctl-Move([double]$dx, [double]$dy) { Move-Mouse (($C.X + $dx) / $L.Inv) (($C.Y + $dy) / $L.Inv) }
function Ctl-Click([double]$dx, [double]$dy) { Click (($C.X + $dx) / $L.Inv) (($C.Y + $dy) / $L.Inv); Start-Sleep -Milliseconds 450 }
function Button-Click([int]$col, [double]$dy = 0) { Ctl-Click ($col * 18 + 9) ($dy + 9) }
function Button-Move([int]$col, [double]$dy = 0) { Ctl-Move ($col * 18 + 9) ($dy + 9) }
function Slot-Click([int]$i) { Ctl-Click (1 + ($i % $C.PerRow) * 18 + 8) (21 + 1 + [math]::Floor($i / $C.PerRow) * 18 + 8) }
function Tab-Click([int]$i) { Gui-Click ($L.Width + 1 + 8) (4 + 25 * $i + 4 + 8) }

# Dumps the carried backpacks' filters; returns the line of upgrade slot $slot.
function Get-Filter([int]$slot) {
    $mark = Get-LogMark
    Invoke-Chat '/backpack filters dump'
    $line = Wait-LogLine ("filters dump: slot=$slot ") 15 -Since $mark
    if ($line -notmatch 'filter=(\{.*\}) direction=(\d+)') { throw "unexpected dump line: $line" }
    $f = $Matches[1] | ConvertFrom-Json
    $f | Add-Member -NotePropertyName direction -NotePropertyValue ([int]$Matches[2])
    return $f
}
function Assert-Filter([int]$slot, [scriptblock]$check, [string]$what) {
    # The dump goes through chat, which closes the backpack screen: callers reopen it.
    $f = Get-Filter $slot
    if (-not (& $check $f)) { throw "filter check failed ($what): $($f | ConvertTo-Json -Compress -Depth 5)" }
    Write-Step "ok: $what"
}

function Open-Backpack {
    $mark = Get-LogMark
    Press-Key 'B'
    Wait-LogLine 'companion: drawing the native backpack screen' 10 -Since $mark | Out-Null
    Read-Layout $mark
    Start-Sleep -Milliseconds 600
    if ((Get-LeviLog -Since $mark) -match 'menu shown: ') { throw 'a fake chest was opened for a companion player' }
}
function Open-FilterTab([int]$tab, [string]$extra = '') {
    $mark = Get-LogMark
    Tab-Click $tab
    Read-Control $mark $extra
    Start-Sleep -Milliseconds 500
}

# Upgrade slots / tabs (setup order): 1 advanced_pickup, 2 advanced_filter, 3 advanced_deposit,
# 4 advanced_restock, 5 pickup, 6 filter, 7 deposit. Hotbar: 0 backpack, 1 apple, 2 oak log,
# 3 iron sword, 4 torch.

# ---- Advanced Pickup: SC ContentsFilterControl.Advanced ---------------------------------------------
Open-Backpack
if ($L.Tabs -lt 8) { throw "expected 8 tabs (settings + 7 upgrades), got $($L.Tabs)" }
Open-FilterTab 1
if ($C.Slots -ne 16 -or $C.PerRow -ne 4) { throw "advanced pickup: expected 16 slots in rows of 4, got $($C.Slots)/$($C.PerRow)" }
if ($C.W -ne 72 -or $C.H -ne 21 + 4 * 18) { throw "advanced control must be 72x93, got $($C.W)x$($C.H)" }
Gui-Move 100 3
Start-Sleep -Milliseconds 300
Save-Screenshot 'adv-pickup-tab' | Out-Null
Button-Move 0
Start-Sleep -Milliseconds 400
Save-Screenshot 'tooltip-block' | Out-Null
Button-Move 1
Start-Sleep -Milliseconds 400
Save-Screenshot 'tooltip-match-item' | Out-Null
Button-Move 2
Start-Sleep -Milliseconds 400
Save-Screenshot 'tooltip-durability' | Out-Null
Button-Move 3
Start-Sleep -Milliseconds 400
Save-Screenshot 'tooltip-nbt' | Out-Null

# Apple into filter slot 0 (ghost: the apple goes back to the inventory), then a duplicate into 1.
Hotbar-Click 1
Slot-Click 0
Slot-Click 1
Hotbar-Click 1
Gui-Move 100 3
Start-Sleep -Milliseconds 400
Save-Screenshot 'apple-in-filter' | Out-Null
# Type (SC ContentsFilterType.next): Block -> Match Backpack Contents (slots hidden) -> Allow.
Button-Click 0
Button-Move 0
Start-Sleep -Milliseconds 400
Save-Screenshot 'type-storage' | Out-Null
Button-Click 0
Button-Move 0
Start-Sleep -Milliseconds 400
Save-Screenshot 'type-allow' | Out-Null
# Durability and NBT on.
Button-Click 2
Button-Click 3
Gui-Move 100 3
Start-Sleep -Milliseconds 300
Save-Screenshot 'allow-dur-nbt' | Out-Null
Press-Key ESC
Start-Sleep -Milliseconds 500
Assert-Filter 0 { param($f) $f.items[0].id -eq 'minecraft:apple' -and -not $f.items[1] -and $f.typeState -eq 0 -and $f.dur -and $f.nbt } 'apple ghost, no duplicate, allow, durability + NBT'

# Tags mode: Match Mod -> Match Tags (the control re-measures: tag list + tag selection slot).
Open-Backpack
Open-FilterTab 1
Button-Click 1
Button-Move 1
Start-Sleep -Milliseconds 400
Save-Screenshot 'match-mod' | Out-Null
$mark = Get-LogMark
Button-Click 1
Read-Control $mark 'primary=2'
if ($C.H -ne 21 + 3 * 18 + 18) { throw "tags mode height must be 21 + (rows-1)*18 + 18, got $($C.H)" }
$tagY = 21 + 3 * 18
Gui-Move 100 3
Start-Sleep -Milliseconds 300
Save-Screenshot 'tags-mode' | Out-Null
Button-Move 1 $tagY
Start-Sleep -Milliseconds 400
Save-Screenshot 'add-tag-no-item' | Out-Null
Button-Move 2 $tagY
Start-Sleep -Milliseconds 400
Save-Screenshot 'remove-tag-empty' | Out-Null
Ctl-Move 20 30
Start-Sleep -Milliseconds 400
Save-Screenshot 'tag-list-empty' | Out-Null
# Oak log into the tag selection slot (client-side ghost), back to the inventory.
Hotbar-Click 2
Ctl-Click 9 ($tagY + 9)
Hotbar-Click 2
Button-Move 1 $tagY
Start-Sleep -Milliseconds 400
Save-Screenshot 'add-tag-list' | Out-Null
Scroll -1
Start-Sleep -Milliseconds 300
Save-Screenshot 'add-tag-scrolled' | Out-Null
Button-Click 1 $tagY
Button-Click 1 $tagY
Start-Sleep -Milliseconds 500
Gui-Move 100 3
Start-Sleep -Milliseconds 300
Save-Screenshot 'tags-added' | Out-Null
Ctl-Move 20 30
Start-Sleep -Milliseconds 400
Save-Screenshot 'tag-list' | Out-Null
Press-Key ESC
Start-Sleep -Milliseconds 500
$added = @((Get-Filter 0).tags).Count
if ($added -lt 1) { throw 'Add Tag added nothing' }
Write-Step "ok: $added tag(s) added"
Open-Backpack
Open-FilterTab 1 'primary=2'
Button-Move 2 $tagY
Start-Sleep -Milliseconds 400
Save-Screenshot 'remove-tag-list' | Out-Null
Button-Click 2 $tagY
Button-Click 3 $tagY
Button-Move 3 $tagY
Start-Sleep -Milliseconds 400
Save-Screenshot 'match-all-tags' | Out-Null
Press-Key ESC
Start-Sleep -Milliseconds 500
Assert-Filter 0 { param($f) $f.primary -eq 2 -and @($f.tags).Count -eq $added - 1 -and -not $f.anyTag } 'tags mode: one removed, match all'

# Back to Match Item; Match Backpack Contents is skipped while in tags mode.
Open-Backpack
Open-FilterTab 1 'primary=2'
Button-Click 0
Button-Click 0
$mark = Get-LogMark
Button-Click 1
Read-Control $mark 'primary=0'
Press-Key ESC
Start-Sleep -Milliseconds 500
Assert-Filter 0 { param($f) $f.primary -eq 0 -and $f.typeState -eq 0 } 'tags mode skips STORAGE (allow -> block -> allow), back to item'

# ---- Advanced Filter: direction button + control 20 lower -------------------------------------------
Open-Backpack
Open-FilterTab 2
Ctl-Move 9 (-20 + 9)
Start-Sleep -Milliseconds 400
Save-Screenshot 'adv-filter-direction' | Out-Null
Ctl-Click 9 (-20 + 9)
Ctl-Move 9 (-20 + 9)
Start-Sleep -Milliseconds 400
Save-Screenshot 'adv-filter-input' | Out-Null
Ctl-Click 9 (-20 + 9)
Press-Key ESC
Start-Sleep -Milliseconds 500
Assert-Filter 1 { param($f) $f.direction -eq 2 } 'direction: both -> input -> output'

# ---- Advanced Deposit: Allow / Block / Match Inventory Contents ------------------------------------
Open-Backpack
Open-FilterTab 3
Button-Click 0
Button-Move 0
Start-Sleep -Milliseconds 400
Save-Screenshot 'deposit-inventory' | Out-Null
Button-Click 0
Tab-Click 3

# ---- Advanced Restock and the basic tabs ------------------------------------------------------------
Open-FilterTab 4
Gui-Move 100 3
Start-Sleep -Milliseconds 300
Save-Screenshot 'adv-restock-tab' | Out-Null
Tab-Click 4
Open-FilterTab 5
if ($C.Slots -ne 9 -or $C.PerRow -ne 3 -or $C.W -ne 54) { throw "basic pickup: expected 9 slots in rows of 3 (54 wide), got $($C.Slots)/$($C.PerRow) $($C.W)" }
Gui-Move 100 3
Start-Sleep -Milliseconds 300
Save-Screenshot 'pickup-tab' | Out-Null
Tab-Click 5
Open-FilterTab 6
Gui-Move 100 3
Start-Sleep -Milliseconds 300
Save-Screenshot 'filter-tab' | Out-Null
Tab-Click 6
Open-FilterTab 7
Gui-Move 100 3
Start-Sleep -Milliseconds 300
Save-Screenshot 'deposit-tab' | Out-Null
Press-Key ESC
Start-Sleep -Milliseconds 500
Assert-Filter 2 { param($f) $f.typeState -eq 0 } 'deposit type cycled back to allow'

# ---- Deposit / Restock triggers ----------------------------------------------------------------------
# A chest two blocks east, looked at; C (inventory interaction key) runs the upgrades.
Invoke-Chat '/setblock ~2 ~ ~ chest'
Invoke-Chat '/tp @s ~ ~ ~ facing ~2 ~ ~'
Start-Sleep -Milliseconds 800
$mark = Get-LogMark
Press-Key 'C'
$line = Wait-LogLine 'key action: .* ran inventory interaction \((\d+) stacks deposited, (\d+) restocked\)' 10 -Since $mark
Start-Sleep -Milliseconds 300
Save-Screenshot 'c-key-status' | Out-Null
# Advanced Deposit is an allow list with nothing in it (matches nothing); basic Deposit is an
# empty block list (everything): the backpack's items go into the chest.
if ($line -notmatch 'interaction \(([1-9]\d*) stacks deposited') { throw "C deposited nothing: $line" }
# Sneak + use: the same upgrades through the item use (SB BackpackItem.useOn).
Start-Sleep -Milliseconds 600
$mark = Get-LogMark
[AbpE2E.Native]::Key(0x10, $false)
Start-Sleep -Milliseconds 400
Click 0.5 0.5 -Relative -Right
Start-Sleep -Milliseconds 400
[AbpE2E.Native]::Key(0x10, $true)
Wait-LogLine 'inventory interaction: .* used a backpack on a container' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 200
Save-Screenshot 'sneak-use-status' | Out-Null
if ((Get-LeviLog -Since $mark) -match 'menu shown: ') { throw 'a chest UI was opened' }
Invoke-Chat '/setblock ~2 ~ ~ air'

# Server-side logic (all four upgrades, the tab actions, noInteractionBlocks).
foreach ($glob in 'filter.*', 'pickup.*', 'deposit.*', 'restock.*') {
    $r = Invoke-SelfTest $glob
    if ($r.Pass -lt 3) { throw "too few passing selftests for ${glob}: pass=$($r.Pass) fail=$($r.Fail)" }
}
