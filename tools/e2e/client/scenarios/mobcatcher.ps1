# P2-8 Mob Catcher (client, -Mode local and -Mode remote-mod): capture a cow and a zombie with real
# input (sneak + right-click with the backpack in hand), the native screen's live mob previews over
# their slot footprints, the hover tooltip (name, "Click to release", hearts) and release icon, the
# removal refusal overlay, releasing by clicking, the muffled ambient sounds and a refusal (basic
# catcher vs a hostile mob). No chest UI may appear.

Wait-LogLine 'companion: server ready' 90 | Out-Null

function Sneak([bool]$down) { Assert-GameForeground; [AbpE2E.Native]::Key(0x10, -not $down); Start-Sleep -Milliseconds 300 }

# Sneak + right-click the mob under the crosshair (it stands 2.5 blocks ahead, the camera looks
# 25 degrees down so the crosshair is on its body).
function Use-OnMob {
    Sneak $true
    Start-Sleep -Milliseconds 300
    Click 0.5 0.5 -Relative -Right
    Start-Sleep -Milliseconds 700
    Sneak $false
}

function Summon-Ahead([string]$type) {
    Invoke-Chat "/summon $type ~ ~ ~2.5"
    Start-Sleep -Milliseconds 400
    Invoke-Chat "/effect @e[type=$type] slowness 600 255 true"
    Invoke-Chat "/effect @e[type=$type] fire_resistance 600 1 true"
    Invoke-Chat '/tp @s ~ ~ ~ 0 25'
    Start-Sleep -Milliseconds 800
}

# ---- layout helpers (GUI units of the native screen, see companion.ps1) --------------------------
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
    Write-Step ("layout: {0}x{1} at {2},{3} rows {4}/{5} scroll={6} tabs={7}" -f $L.Width, $L.Height, $L.Left, $L.Top, $L.Visible, $L.Rows, $L.Scroll, $L.Tabs)
}
function Gui-Move([double]$gx, [double]$gy) { Move-Mouse (($L.Left + $gx) / $L.Inv) (($L.Top + $gy) / $L.Inv) }
function Gui-Click([double]$gx, [double]$gy, [switch]$Right) { Click (($L.Left + $gx) / $L.Inv) (($L.Top + $gy) / $L.Inv) -Right:$Right; Start-Sleep -Milliseconds 400 }
function Storage-Pos([int]$i) { $cols = if ($L.Width -ge 230) { 12 } else { 9 }; @((8 + ($i % $cols) * 18 + 8), (18 + [math]::Floor($i / $cols) * 18 + 8)) }

$problems = @()
try {
    Invoke-Chat '/clear @s'
    Invoke-Chat '/gamemode creative'
    Invoke-Chat '/difficulty easy'
    Invoke-Chat '/kill @e[type=!player]'
    Invoke-Chat '/time set noon'
    $mark = Get-LogMark
    Invoke-Chat '/backpack mobcatcher setup'
    Wait-LogLine 'mobcatcher setup: gave netherite backpack' 15 -Since $mark | Out-Null

    # ---- capture a cow (passive) ---------------------------------------------------------------------
    Summon-Ahead 'cow'
    Save-Screenshot 'cow-ahead' | Out-Null
    $mark = Get-LogMark
    Use-OnMob
    Wait-LogLine 'mob catcher: .* captured minecraft:cow into .* at slot 0' 10 -Since $mark | Out-Null
    Start-Sleep -Milliseconds 200
    Save-Screenshot 'cow-captured' | Out-Null

    # ---- capture a zombie (hostile: the advanced catcher) -----------------------------------------------
    Summon-Ahead 'zombie'
    $mark = Get-LogMark
    Use-OnMob
    Wait-LogLine 'mob catcher: .* captured minecraft:zombie' 10 -Since $mark | Out-Null
    Start-Sleep -Milliseconds 200
    Save-Screenshot 'zombie-captured' | Out-Null

    # ---- the native screen with both mobs -------------------------------------------------------------
    $mark = Get-LogMark
    Press-Key 'B'
    Wait-LogLine 'companion: drawing the native backpack screen' 10 -Since $mark | Out-Null
    Read-Layout $mark
    Wait-LogLine 'mob catcher: preview of minecraft:cow rendered' 10 -Since $mark | Out-Null
    Wait-LogLine 'mob catcher: preview of minecraft:zombie rendered' 10 -Since $mark | Out-Null
    Gui-Move ($L.Width / 2) -30
    Start-Sleep -Milliseconds 800
    Save-Screenshot 'screen-previews' | Out-Null
    if ((Get-LeviLog -Since $mark) -match 'menu shown: ') { throw 'a fake chest was opened for a companion player' }

    # Hover: tooltip with the name, "Click to release" and hearts; the release icon.
    $p = Storage-Pos 13 # the cow's footprint (3x4 from slot 0)
    Gui-Move $p[0] $p[1]
    Start-Sleep -Milliseconds 700
    Save-Screenshot 'cow-tooltip' | Out-Null
    $p = Storage-Pos 28 # inside the zombie's footprint (from slot 3)
    Gui-Move $p[0] $p[1]
    Start-Sleep -Milliseconds 700
    Save-Screenshot 'zombie-tooltip' | Out-Null

    # Scrolled: the zombie's footprint is clipped by the storage panel.
    if ($L.Scroll) {
        Scroll -2
        Start-Sleep -Milliseconds 600
        Save-Screenshot 'scrolled' | Out-Null
        Scroll 5
        Start-Sleep -Milliseconds 400
    }

    # Taking the upgrade out is refused: SB's red overlay over the upgrade and the mobs' slots.
    Gui-Click (-15 + 8) (6 + 8)
    Start-Sleep -Milliseconds 300
    Gui-Move ($L.Width / 2) -30
    Start-Sleep -Milliseconds 300
    Save-Screenshot 'remove-refused' | Out-Null

    # Release the cow with a left click on it.
    $mark = Get-LogMark
    $p = Storage-Pos 13
    Gui-Click $p[0] $p[1]
    Wait-LogLine 'mob catcher: .* released minecraft:cow' 10 -Since $mark | Out-Null
    Start-Sleep -Milliseconds 700
    Gui-Move ($L.Width / 2) -30
    Start-Sleep -Milliseconds 300
    Save-Screenshot 'cow-released-screen' | Out-Null
    Close-Screen
    Start-Sleep -Milliseconds 600
    Save-Screenshot 'cow-released-world' | Out-Null

    # Carried captured mobs: muffled ambient sounds every 220-480 ticks.
    try {
        Wait-LogLine 'mob catcher: playing muffled ambient sounds of captured mobs' 40 | Out-Null
    } catch { $problems += "no ambient sound: $_" }

    # ---- refusal: the basic catcher can't take hostile mobs ---------------------------------------------
    $mark = Get-LogMark
    Invoke-Chat '/backpack mobcatcher basic'
    Wait-LogLine 'mobcatcher setup: gave iron backpack' 15 -Since $mark | Out-Null
    Summon-Ahead 'zombie'
    $mark = Get-LogMark
    Use-OnMob
    Wait-LogLine 'mob catcher: .* could not capture minecraft:zombie: Hostile mobs require Advanced Mob Catcher Upgrade' 10 -Since $mark | Out-Null
    Start-Sleep -Milliseconds 200
    Save-Screenshot 'refused-hostile' | Out-Null
} finally {
    try {
        Invoke-Chat '/kill @e[type=!player]'
        Invoke-Chat '/difficulty peaceful'
        Invoke-Chat '/gamemode survival'
    } catch {}
}
if ($problems.Count -gt 0) { throw ($problems -join '; ') }
Assert-NoErrors
