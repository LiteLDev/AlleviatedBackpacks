# P2-13 world systems with a real client (`-Mode local` and `-Mode remote-mod`):
#   - the Ender Linker in the native backpack screen (right click a backpack slot with the linker on
#     the cursor: primary, then a secondary with the bound linker) and the role tooltips;
#   - opening another player's backpack: a simulated player stands with its back to us, wearing a
#     backpack; a right click on its back opens it as the native screen;
#   - `/sbp list` and `/sbp give` typed in chat.
# Server-side rules are covered by the selftests (server harness `world`).

Wait-LogLine 'companion: server ready' 90 | Out-Null
Write-Step 'companion handshake done'

$script:L = $null
function Read-Layout($mark) {
    $line = Wait-LogLine 'companion: screen layout session=' 15 -Since $mark
    if ($line -notmatch 'left=([\d.]+) top=([\d.]+) width=(\d+) height=(\d+) rows=(\d+)/(\d+) scroll=(\w+) invGuiScale=([\d.]+) .*invLabel=(\d+),(\d+) upgrades=(\d+) tabs=(\d+)') {
        throw "unexpected layout line: $line"
    }
    $script:L = [pscustomobject]@{
        Left = [double]$Matches[1]; Top = [double]$Matches[2]; Width = [int]$Matches[3]; Height = [int]$Matches[4]
        Inv = [double]$Matches[8]; InvX = [int]$Matches[9]; InvY = [int]$Matches[10]
    }
}
function Gui-Move([double]$gx, [double]$gy) { Move-Mouse (($L.Left + $gx) / $L.Inv) (($L.Top + $gy) / $L.Inv) }
function Gui-Click([double]$gx, [double]$gy, [switch]$Right) { Click (($L.Left + $gx) / $L.Inv) (($L.Top + $gy) / $L.Inv) -Right:$Right; Start-Sleep -Milliseconds 450 }
function Hotbar-Pos([int]$i) { @(($L.InvX + 8 + $i * 18), ($L.InvY + 12 + 54 + 4 + 8)) }
# Remote: a chat opened right after a screen closed can get lost. Retries until `pattern` is logged.
function Invoke-ChatUntil([string]$text, [string]$pattern, [int]$tries = 3) {
    for ($t = 0; $t -lt $tries; $t++) {
        Start-Sleep -Milliseconds 1000
        $m = Get-LogMark
        Invoke-Chat $text
        try { return (Wait-LogLine $pattern 8 -Since $m) } catch { Write-Step "no '$pattern' after '$text'; retrying" }
    }
    throw "'$text' never logged '$pattern'"
}

# ---- Ender Linker ---------------------------------------------------------------------------------
Invoke-Chat '/clear @s'
$mark = Get-LogMark
Invoke-Chat '/backpack companion setup'
$line = Wait-LogLine 'companion setup: gave netherite backpack ([0-9a-f-]{36})' 15 -Since $mark
if ($line -notmatch 'backpack ([0-9a-f-]{36})') { throw "no uuid: $line" }
$netheriteUuid = $Matches[1]
Invoke-Chat '/replaceitem entity @s slot.hotbar 1 alleviatedbackpacks:leather_backpack'
Invoke-Chat '/replaceitem entity @s slot.hotbar 2 alleviatedbackpacks:copper_backpack'
Invoke-Chat '/replaceitem entity @s slot.hotbar 3 alleviatedbackpacks:ender_linker'
Press-Key '1'
Start-Sleep -Milliseconds 600

$mark = Get-LogMark
Press-Key 'B'
Wait-LogLine 'companion: drawing the native backpack screen' 10 -Since $mark | Out-Null
Read-Layout $mark
Start-Sleep -Milliseconds 600

# The unlinked linker onto the cursor, then a right click on the leather backpack: new group.
$p = Hotbar-Pos 3
Gui-Click $p[0] $p[1]
$p = Hotbar-Pos 1
$mark = Get-LogMark
Gui-Click $p[0] $p[1] -Right
Wait-LogLine 'linked storage: .* linked with result success' 10 -Since $mark | Out-Null
Wait-LogLine 'linked storage: group .* created from backpack' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 500
Save-Screenshot 'linker-primary' | Out-Null

# The bound linker (still on the cursor) on the copper backpack: secondary endpoint.
$p = Hotbar-Pos 2
$mark = Get-LogMark
Gui-Click $p[0] $p[1] -Right
Wait-LogLine 'linked storage: secondary endpoint .* added to group' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 500

# Role tooltips: "Main Linked" on the leather backpack, "Linked" on the copper one.
$p = Hotbar-Pos 1
Gui-Move $p[0] $p[1]
Start-Sleep -Milliseconds 700
Save-Screenshot 'tooltip-primary' | Out-Null
$p = Hotbar-Pos 2
Gui-Move $p[0] $p[1]
Start-Sleep -Milliseconds 700
Save-Screenshot 'tooltip-secondary' | Out-Null
if ((Get-LeviLog -Since $mark) -match 'menu shown: ') { throw 'a fake chest was opened for a companion player' }
Press-Key 'E'
Start-Sleep -Milliseconds 1500

# The secondary (a copper backpack item) opens the shared leather storage: 27 slots. Only the
# copper backpack stays in the inventory, so Open Backpack (B) picks it.
# (Remote: a chat opened right after a screen closed can get lost; retry until B picks the copper.)
$opened = $false
for ($try = 0; $try -lt 3 -and -not $opened; $try++) {
    Start-Sleep -Milliseconds 1500
    Invoke-Chat '/replaceitem entity @s slot.hotbar 0 air'
    Invoke-Chat '/replaceitem entity @s slot.hotbar 1 air'
    Start-Sleep -Milliseconds 800
    $mark = Get-LogMark
    Press-Key 'B'
    $line = Wait-LogLine "companion: native screen \d+ for '" 10 -Since $mark
    Wait-LogLine 'companion: drawing the native backpack screen' 10 -Since $mark | Out-Null
    if ($line -match "for 'Copper Backpack' \(27 slots") { $opened = $true; break }
    Write-Step "B opened another backpack ($line); retrying"
    Press-Key 'E'
}
if (-not $opened) { throw 'the secondary endpoint did not open the shared 27-slot storage' }
Start-Sleep -Milliseconds 800
Save-Screenshot 'secondary-open' | Out-Null
Press-Key 'E'
Start-Sleep -Milliseconds 800

# ---- another player's backpack ----------------------------------------------------------------------
Press-Key '5' # empty hand
Invoke-ChatUntil '/backpack world target' 'world target: spawned abp_e2e_target wearing backpack' | Out-Null
Start-Sleep -Milliseconds 1500
Save-Screenshot 'target-back' | Out-Null
$mark = Get-LogMark
Click 0.5 0.5 -Relative -Right
Wait-LogLine "access: .* -> abp_e2e_target's backpack: opened" 10 -Since $mark | Out-Null
Wait-LogLine 'companion: drawing the native backpack screen' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 900
Save-Screenshot 'other-player-backpack' | Out-Null
if ((Get-LeviLog -Since $mark) -match 'menu shown: ') { throw 'a fake chest was opened for a companion player' }
Press-Key 'E'
Start-Sleep -Milliseconds 800
Invoke-Chat '/backpack world untarget'

# ---- /sbp ----------------------------------------------------------------------------------------------
Invoke-ChatUntil '/sbp list' 'sbp: list -> [1-9]\d* backpacks' | Out-Null
Start-Sleep -Milliseconds 300
Save-Screenshot 'sbp-list' | Out-Null # the chat lines show on the HUD for a few seconds

Invoke-ChatUntil "/sbp give @s $netheriteUuid" "sbp: gave backpack $netheriteUuid to 1 players" | Out-Null
Start-Sleep -Milliseconds 300
Save-Screenshot 'sbp-give' | Out-Null
Assert-NoErrors
