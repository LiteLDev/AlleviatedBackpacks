# GX-5 parity gaps with a real client (`-Mode local` and `-Mode remote-mod`):
#   1. SB upgrade limits: a second Crafting upgrade goes in (two independent crafting tabs); a
#      second Jukebox is refused with SC's error overlay highlighting the installed one.
#   2. A worn Gold Backpack makes piglins neutral; with an Iron Backpack the piglin attacks.
#   3. A dispenser places a backpack in front of it (facing the dispenser).
#   4. Placed backpacks by hand (ported from the retired fake-chest scenario `placed`): sneak +
#      right-click places, right-click opens the native screen, sneak + empty hand picks it up.
#   5. A dropped Everlasting backpack is not pushed by an explosion.
# parity.prelaunch.ps1 forces the first-person camera (crosshair aiming), parity.restore.ps1 puts
# it back. Coordinates in the backpack screen come from the mod's "companion: screen layout" line.

Wait-LogLine 'companion: server ready' 90 | Out-Null
$remote = $script:Mode -and $script:Mode -ne 'local'

$script:L = $null
function Read-Layout($mark) {
    $line = Wait-LogLine 'companion: screen layout session=' 15 -Since $mark
    if ($line -notmatch 'left=([\d.]+) top=([\d.]+) width=(\d+) height=(\d+) .* invGuiScale=([\d.]+) .* invLabel=(\d+),(\d+)') {
        throw "unexpected layout line: $line"
    }
    $script:L = [pscustomobject]@{
        Left = [double]$Matches[1]; Top = [double]$Matches[2]; Width = [int]$Matches[3]; Height = [int]$Matches[4]
        Inv = [double]$Matches[5]; InvX = [int]$Matches[6]; InvY = [int]$Matches[7]
    }
}
function Gui-Move([double]$gx, [double]$gy) { Move-Mouse (($L.Left + $gx) / $L.Inv) (($L.Top + $gy) / $L.Inv) }
function Gui-Click([double]$gx, [double]$gy) { Click (($L.Left + $gx) / $L.Inv) (($L.Top + $gy) / $L.Inv); Start-Sleep -Milliseconds 400 }
function Hotbar-Pos([int]$i) { @(($L.InvX + $i * 18 + 8), ($L.InvY + 12 + 54 + 4 + 8)) }
function Upgrade-Pos([int]$i) { @(-7, (6 + $i * 16 + 8)) }
function Tab-Pos([int]$i) { @(($L.Width + 1 + 8), (4 + $i * 25 + 4 + 8)) }
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
    Start-Sleep -Milliseconds $(if ($remote) { 3500 } else { 1500 })
}
# Chat command with a log answer, retried when the chat did not open (the game sometimes drops
# the T right after a screen closed).
function Invoke-Checked([string]$command, [string]$answer, [int]$timeout = 10) {
    for ($attempt = 1; $attempt -le 3; $attempt++) {
        $mark = Get-LogMark
        Invoke-Chat $command
        try { return (Wait-LogLine $answer $timeout -Since $mark) } catch {
            Write-Step "$command : no answer (attempt $attempt)"
            Press-Key ESC
            Start-Sleep -Milliseconds 1500
        }
    }
    throw "$command got no answer matching '$answer'"
}
function Get-Status {
    $line = Invoke-Checked '/backpack parity status' 'parity status: health='
    if ($line -notmatch 'health=(\d+) piglins=(\d+) targeting=(\d+)') { throw "unexpected status: $line" }
    [pscustomobject]@{ Health = [int]$Matches[1]; Piglins = [int]$Matches[2]; Targeting = [int]$Matches[3]; Line = $line }
}
$script:PlacedAt = @()
function Get-PlacedCount {
    $mark = Get-LogMark
    $line = Invoke-Checked '/backpack placed' 'placed backpacks: \d+'
    Start-Sleep -Milliseconds 300
    $script:PlacedAt = @(Get-LeviLog -Since $mark | ForEach-Object { if ($_ -match 'dim 0 at (-?\d+) (-?\d+) (-?\d+):') { "$($Matches[1]) $($Matches[2]) $($Matches[3])" } } | Sort-Object -Unique)
    return [int]([regex]::Match($line, 'placed backpacks: (\d+)').Groups[1].Value)
}
function Sneak([bool]$down) { Focus-Game; [AbpE2E.Native]::Key(0x10, -not $down); Start-Sleep -Milliseconds 300 }

# ---- 1. upgrade limits ----------------------------------------------------------------------------
Invoke-Chat '/clear @s'
Invoke-Chat '/gamemode creative'
Invoke-Checked '/backpack parity limits' 'parity limits: gave diamond backpack' | Out-Null
Press-Key '1'
Start-Sleep -Milliseconds 500
Open-Backpack
Save-Screenshot 'limits-open' | Out-Null
# A second Crafting upgrade: SB has no limit.
$h = Hotbar-Pos 1; Gui-Click $h[0] $h[1]
$u = Upgrade-Pos 2; Gui-Click $u[0] $u[1]
Start-Sleep -Milliseconds 400
Save-Screenshot 'limits-two-crafting' | Out-Null
# A second Jukebox: SB's maxUpgradesPerStorage jukebox_upgrades|1 refuses it, highlighting slot 1.
$h = Hotbar-Pos 2; Gui-Click $h[0] $h[1]
$u = Upgrade-Pos 3; Gui-Click $u[0] $u[1]
Start-Sleep -Milliseconds 200
Save-Screenshot 'limits-jukebox-refused' | Out-Null
$h = Hotbar-Pos 2; Gui-Click $h[0] $h[1] # the jukebox back into the hotbar
# Tabs: 0 settings, 1 crafting (slot 0), 2 jukebox (slot 1), 3 crafting (slot 2).
$t = Tab-Pos 3; Gui-Click $t[0] $t[1]
Start-Sleep -Milliseconds 600
Save-Screenshot 'limits-second-crafting-tab' | Out-Null
$t = Tab-Pos 3; Gui-Click $t[0] $t[1]
Close-Backpack
$info = Invoke-Checked '/backpack info' 'backpack info: '
if ($info -notmatch 'upgrades=\[crafting, jukebox, crafting\]') { throw "expected two crafting upgrades and one jukebox: $info" }

# ---- 2. piglins and the gold backpack --------------------------------------------------------------
Invoke-Chat '/clear @s'
Invoke-Chat '/gamemode survival'
Invoke-Chat '/difficulty easy'
Invoke-Chat '/effect @s resistance 60 4 true' # hits do no damage: the target tells
Invoke-Chat '/replaceitem entity @s slot.armor.chest 0 alleviatedbackpacks:gold_backpack'
Invoke-Chat '/tp @s ~ ~ ~ 0 15'
Invoke-Chat '/summon piglin ~ ~ ~4 0 0 spawn_adult_melee'
Start-Sleep -Seconds 5
$s = Get-Status
Save-Screenshot 'piglin-gold-backpack' | Out-Null
Write-Step "gold backpack: $($s.Line)"
if ($s.Piglins -lt 1) { throw "no piglin near the player: $($s.Line)" }
if ($s.Targeting -ne 0 -or $s.Health -lt 20) { throw "the piglin attacked a Gold Backpack wearer: $($s.Line)" }
# Overworld piglins turn into zombified piglins after 15 s: a fresh one for the control.
Invoke-Chat '/kill @e[type=piglin]'
Invoke-Chat '/replaceitem entity @s slot.armor.chest 0 alleviatedbackpacks:iron_backpack'
Invoke-Chat '/summon piglin ~ ~ ~4 0 0 spawn_adult_melee'
$attacked = $false
for ($i = 0; $i -lt 10 -and -not $attacked; $i++) {
    Start-Sleep -Seconds 1
    $s = Get-Status
    $attacked = $s.Targeting -gt 0 -or $s.Health -lt 20
}
Save-Screenshot 'piglin-iron-backpack' | Out-Null
Write-Step "iron backpack: $($s.Line)"
if (-not $attacked) { throw "the piglin ignored a player without gold (setup broken?): $($s.Line)" }
Invoke-Chat '/kill @e[type=piglin]'
Invoke-Chat '/kill @e[type=zombie_pigman]'
Invoke-Chat '/difficulty peaceful'
Invoke-Chat '/effect @s clear'
Invoke-Chat '/effect @s instant_health 1 10 true'
Invoke-Chat '/replaceitem entity @s slot.armor.chest 0 air'

# ---- 3. dispensers place backpacks ----------------------------------------------------------------
Invoke-Chat '/gamemode creative'
Invoke-Chat '/tp @s ~ ~ ~ -90 25'
Invoke-Chat '/fill ~2 ~ ~-1 ~4 ~2 ~1 air'
Invoke-Chat '/setblock ~3 ~ ~ dispenser ["facing_direction"=5]'
Invoke-Chat '/replaceitem block ~3 ~ ~ slot.container 0 alleviatedbackpacks:iron_backpack'
$before = Get-PlacedCount
Invoke-Chat '/setblock ~3 ~1 ~ redstone_block'
Start-Sleep -Seconds 1
$after = Get-PlacedCount
Save-Screenshot 'dispenser-placed' | Out-Null
if ($after -ne $before + 1) { throw "the dispenser did not place the backpack ($before -> $after)" }
Invoke-Chat '/tp @s ~5 ~1 ~3 135 25'
Start-Sleep -Seconds 1
Save-Screenshot 'dispenser-placed-side' | Out-Null
Invoke-Chat '/tp @s ~-5 ~1 ~-3 -90 25' # it fell back to the ground meanwhile: land on it again
Start-Sleep -Seconds 1

# ---- 4. placing by hand, opening, picking up --------------------------------------------------------
Invoke-Chat '/clear @s'
Invoke-Chat '/gamemode survival'
Invoke-Chat '/replaceitem entity @s slot.hotbar 0 alleviatedbackpacks:iron_backpack'
# Stand in the middle of a block looking steeply down at the next block south: the backpack goes
# there and the crosshair stays on it.
$s = Get-Status
if ($s.Line -notmatch 'block=(-?\d+),(-?\d+),(-?\d+)') { throw "no position in: $($s.Line)" }
$inv = [cultureinfo]::InvariantCulture
$cx = ([int]$Matches[1] + 0.5).ToString($inv); $cy = [int]$Matches[2]; $cz = ([int]$Matches[3] + 0.5).ToString($inv)
Invoke-Chat "/tp @s $cx $cy $cz 0 60"
Press-Key '1'
Start-Sleep -Seconds 1
$before = Get-PlacedCount
$placedBefore = $script:PlacedAt
Sneak $true
Click 0.5 0.5 -Relative -Right
Start-Sleep -Milliseconds 400
Sneak $false
Start-Sleep -Seconds 1
Save-Screenshot 'hand-placed' | Out-Null
$after = Get-PlacedCount
if ($after -ne $before + 1) { throw "sneak + right-click did not place the backpack ($before -> $after)" }
$mark = Get-LogMark
Click 0.5 0.5 -Relative -Right
Wait-LogLine 'companion: native screen [0-9]+ for|companion: drawing the native backpack screen' 15 -Since $mark | Out-Null
Start-Sleep -Milliseconds 800
Save-Screenshot 'hand-placed-open' | Out-Null
if ((Get-LeviLog -Since $mark) -match 'menu shown: ') { throw 'a fake chest was opened for a companion player' }
Close-Backpack
Press-Key '3'
Start-Sleep -Milliseconds 500
Sneak $true
Click 0.5 0.5 -Relative -Right
Start-Sleep -Milliseconds 400
Sneak $false
Start-Sleep -Seconds 1
Save-Screenshot 'hand-picked-up' | Out-Null
$final = Get-PlacedCount
if ($final -ne $before) { throw "sneak + right-click with an empty hand did not pick the backpack up ($after -> $final)" }

# ---- 5. everlasting vs explosions ----------------------------------------------------------------
Invoke-Chat '/gamemode creative'
Invoke-Chat '/tp @s ~ ~ ~ -90 30'
Invoke-Checked '/backpack parity everlasting' 'parity everlasting: dropped' | Out-Null
Start-Sleep -Seconds 2
Save-Screenshot 'everlasting-before' | Out-Null
$line = Invoke-Checked '/backpack parity explode' 'parity explode: (moved|gone)' 15
Start-Sleep -Milliseconds 500
Save-Screenshot 'everlasting-after' | Out-Null
if ($line -notmatch 'moved ([\d.]+)') { throw "the everlasting backpack is gone after the explosion: $line" }
if ([double]$Matches[1] -gt 0.05) { throw "the explosion pushed the everlasting backpack: $line" }

# ---- selftests of the same features on this server --------------------------------------------------
foreach ($glob in 'admin.sb_upgrade_limits', 'crafting.two_upgrades_independent', 'wearable.piglin_neutral', 'placed.dispenser_*', 'everlasting.explosion_knockback', 'toolswapper.key_block') {
    $r = Invoke-SelfTest $glob
    if ($r.Fail -gt 0 -or $r.Pass -lt 1) { throw "${glob}: pass=$($r.Pass) fail=$($r.Fail)" }
}
Assert-NoErrors
