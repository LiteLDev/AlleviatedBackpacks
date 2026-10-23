# GX-1 rendering gaps, driven with real input in `-Mode local` and `-Mode remote-mod`:
#   1. vanilla slots draw the two-colour layered backpack icon (hotbar, inventory)
#   2. tank bars with the game's animated still textures (water, lava, milk, potion) and SB's red
#      error overlay over a tank when removing the stack upgrade fails
#   3. the Item Display tab's rotatable preview: the real 3D model (display entity)
#   4. the smithing tab's armor stand without its base plate
#   5. tank fluids on the model (armor stands): real sprites, tinted
#   6. dropped backpacks and backpacks in item frames: the 3D model with SB's ground / fixed
#      transforms
#   7. cooking: flame / smoke particles and the furnace crackle on a worn and a placed backpack
#   8. mob catcher: a cold cow keeps its climate variant (actor properties) in the preview, and
#      the capture shrink animation (stretched to 60 ticks for the screenshots)

Wait-LogLine 'companion: server ready' 90 | Out-Null
Write-Step 'companion handshake done'

function Set-View([string]$preset) { Invoke-Chat "/camera @s set minecraft:$preset"; Start-Sleep -Seconds 1 }
function Scene([string]$cmd, [string]$name) {
    $mark = Get-LogMark
    Invoke-Chat "/backpack $cmd $name"
    Wait-LogLine "(model|render) test $($name): \d+ objects" 15 -Since $mark | Out-Null
    Start-Sleep -Seconds 2
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
function Open-Backpack {
    $m = Get-LogMark
    Press-Key 'B'
    Wait-LogLine 'companion: drawing the native backpack screen' 10 -Since $m | Out-Null
    Read-Layout $m
    Start-Sleep -Milliseconds 600
}
function Close-Backpack {
    $m = Get-LogMark
    Press-Key 'E'
    Wait-LogLine 'native screen action: close by' 10 -Since $m | Out-Null
    Start-Sleep -Seconds 2 # let the screen close before typing in chat (B is also the emote key)
}
function Sneak([bool]$down) { Assert-GameForeground; [AbpE2E.Native]::Key(0x10, -not $down); Start-Sleep -Milliseconds 300 }

# `$env:ABP_GX1_ONLY = '6,8'` runs only those sections (iterating on one gap).
function Want([int]$n) { -not $env:ABP_GX1_ONLY -or (($env:ABP_GX1_ONLY -split ',') -contains "$n") }

$problems = @()
try {
    Invoke-Chat '/gamemode creative'
    Invoke-Chat '/clear @s'
    Invoke-Chat '/time set noon'
    Invoke-Chat '/kill @e[type=!player]'
    # A clear grass floor at y -61 (the fixture world is flat, but a run can come up with terrain).
    Invoke-Chat '/fill -10 -60 -6 10 -45 14 air'
    Invoke-Chat '/fill -10 -61 -6 10 -61 14 grass'
    Invoke-Chat '/tp @s 0.5 -60 0.5 0 25'
    Start-Sleep -Seconds 1

    # ---- 1. vanilla slots ---------------------------------------------------------------------------
    if (Want 1) {
    Scene 'modeltest' 'kit'
    Invoke-Chat '/replaceitem entity @s slot.hotbar 0 alleviatedbackpacks:netherite_backpack'
    Invoke-Chat '/replaceitem entity @s slot.hotbar 1 alleviatedbackpacks:leather_backpack'
    Start-Sleep -Seconds 2
    Save-Screenshot 'slots-hotbar' | Out-Null
    Invoke-Chat '/gamemode survival'
    Open-Inventory
    Start-Sleep -Seconds 2
    Save-Screenshot 'slots-inventory' | Out-Null
    Close-Screen
    Invoke-Chat '/gamemode creative'
    try { Wait-LogLine 'backpack icons: vanilla slots draw the layered model icon' 5 | Out-Null }
    catch { $problems += 'vanilla slots did not draw the layered icon' }

    }
    # ---- 2. tank bars and the error overlay ------------------------------------------------------------
    if (Want 2) {
    Invoke-Chat '/clear @s'
    Scene 'rendertest' 'fluidbars'
    Press-Key '1'
    Start-Sleep -Milliseconds 500
    Open-Backpack
    Gui-Move ($L.Width / 2) -30
    Start-Sleep -Milliseconds 800
    Save-Screenshot 'fluid-bars-1' | Out-Null
    Start-Sleep -Milliseconds 350
    Save-Screenshot 'fluid-bars-2' | Out-Null
    # Taking the stack upgrade out is refused (the full water tank needs its multiplier): SB's red
    # overlay over the upgrade slot and the tank bar.
    Gui-Click (-15 + 8) (6 + 8)
    Start-Sleep -Milliseconds 300
    Gui-Move ($L.Width / 2) -30
    Start-Sleep -Milliseconds 300
    Save-Screenshot 'tank-error-overlay' | Out-Null
    Close-Backpack
    Invoke-Chat '/clear @s'
    Scene 'rendertest' 'fluidbars2'
    Press-Key '1'
    Start-Sleep -Milliseconds 500
    Open-Backpack
    Gui-Move ($L.Width / 2) -30
    Start-Sleep -Milliseconds 800
    Save-Screenshot 'fluid-bars-milk-potion' | Out-Null

    }
    # ---- 3. item display preview -------------------------------------------------------------------------
    if (Want 3) {
    $mark = Get-LogMark
    Click (($L.Left + $L.Width + 1 + 8) / $L.Inv) (($L.Top + 4 + 4 + 8) / $L.Inv)
    $line = Wait-LogLine 'settings screen: layout left=' 15 -Since $mark
    if ($line -notmatch 'left=([\d.]+) top=([\d.]+) width=(\d+)') { throw "settings layout: $line" }
    $sl = [double]$Matches[1]; $st = [double]$Matches[2]; $sw = [int]$Matches[3]
    $sinv = $L.Inv
    Start-Sleep -Milliseconds 600
    # Item Display tab (index 2): its icon, then the preview turns to (30, 45) within a second.
    Click (($sl + $sw + 1 + 8) / $sinv) (($st + 4 + 25 * 2 + 4 + 8) / $sinv)
    Start-Sleep -Milliseconds 1500
    Move-Mouse (($sl + $sw / 2) / $sinv) (($st - 20) / $sinv)
    Start-Sleep -Milliseconds 400
    Save-Screenshot 'preview-default' | Out-Null
    try { Wait-LogLine 'entity preview: alleviatedbackpacks:backpack_display renders' 5 | Out-Null }
    catch { $problems += 'the item display preview did not render the display entity' }
    if ($env:ABP_GX1_CALIBRATE) {
        # Local worlds only (the command and the client share the process): preview pose variants.
        Press-Key 'E'; Start-Sleep -Seconds 2
        $i = 0
        foreach ($c in '0 0 0 0 0.5 1 0 0', '0 1 0 0 0.5 1 0 0', '0 -1 0 0 0.5 1 0 0', '0 0 0 1 0.5 1 0 0', '0 0 0 -1 0.5 1 0 0', '0 1 -180 1 0.5 1 0 0') {
            Invoke-Chat "/backpack modeltest pose `"$c`""
            Start-Sleep -Milliseconds 300
            Open-Backpack
            $m = Get-LogMark
            Click (($L.Left + $L.Width + 1 + 8) / $L.Inv) (($L.Top + 4 + 4 + 8) / $L.Inv)
            Wait-LogLine 'settings screen: layout left=' 15 -Since $m | Out-Null
            Start-Sleep -Milliseconds 500
            Click (($sl + $sw + 1 + 8) / $sinv) (($st + 4 + 25 * 2 + 4 + 8) / $sinv)
            Start-Sleep -Milliseconds 1500
            Save-Screenshot "preview-cal-$i" | Out-Null
            $i++
            Press-Key 'E'; Start-Sleep -Seconds 2
        }
        Invoke-Chat '/backpack modeltest pose off'
        Open-Backpack
        $m = Get-LogMark
        Click (($L.Left + $L.Width + 1 + 8) / $L.Inv) (($L.Top + 4 + 4 + 8) / $L.Inv)
        Wait-LogLine 'settings screen: layout left=' 15 -Since $m | Out-Null
        Start-Sleep -Milliseconds 500
        Click (($sl + $sw + 1 + 8) / $sinv) (($st + 4 + 25 * 2 + 4 + 8) / $sinv)
        Start-Sleep -Milliseconds 1500
    }
    # Drag the preview (SB: y += 2 dx, x += 2 dy).
    $px = ($sl + $sw + 3 + 36) / $sinv; $py = ($st + 4 + 25 * 2 + 24 + 36) / $sinv
    Move-Mouse $px $py
    [AbpE2E.Native]::MouseButton(0, $false); Start-Sleep -Milliseconds 120
    for ($k = 1; $k -le 6; $k++) { Move-Mouse ($px + $k * 5 / $sinv) $py; Start-Sleep -Milliseconds 80 }
    [AbpE2E.Native]::MouseButton(0, $true); Start-Sleep -Milliseconds 400
    Save-Screenshot 'preview-dragged-right' | Out-Null
    Move-Mouse $px $py
    [AbpE2E.Native]::MouseButton(0, $false); Start-Sleep -Milliseconds 120
    for ($k = 1; $k -le 6; $k++) { Move-Mouse $px ($py + $k * 5 / $sinv); Start-Sleep -Milliseconds 80 }
    [AbpE2E.Native]::MouseButton(0, $true); Start-Sleep -Milliseconds 400
    Save-Screenshot 'preview-dragged-down' | Out-Null
    Press-Key 'E'
    Start-Sleep -Seconds 2

    }
    # ---- 4. smithing armor stand ---------------------------------------------------------------------------
    if (Want 4) {
    Invoke-Chat '/clear @s'
    $mark = Get-LogMark
    Invoke-Chat '/backpack station setup'
    Wait-LogLine 'station setup: station' 15 -Since $mark | Out-Null
    Press-Key '1'
    Start-Sleep -Milliseconds 600
    Open-Backpack
    $tx = $L.Width + 1 + 8; $ty = 4 + 25 * 2 + 4 + 8
    for ($try = 1; $try -le 3; $try++) {
        $m = Get-LogMark
        Gui-Click $tx $ty
        Start-Sleep -Milliseconds 900
        if ((Get-LeviLog -Since $m) -match 'station tab: smithing open') { break }
    }
    Gui-Move ($L.Width + 30) (4 + 25 * 2 + 120)
    Start-Sleep -Milliseconds 800
    Save-Screenshot 'smithing-stand' | Out-Null
    Close-Backpack

    }
    # ---- 5. tank fluids on the model -------------------------------------------------------------------
    if (Want 5) {
    Invoke-Chat '/clear @s'
    Invoke-Chat '/tp @s 0.5 -60 0.5 0 15'
    Scene 'rendertest' 'fluidstands'
    Save-Screenshot 'model-fluids' | Out-Null
    Invoke-Chat '/tp @s 0.5 -59 1.5 0 30'
    Start-Sleep -Seconds 1
    Save-Screenshot 'model-fluids-close' | Out-Null
    Invoke-Chat '/kill @e[type=armor_stand]'

    }
    # ---- 6. dropped and framed backpacks -----------------------------------------------------------------
    if (Want 6) {
    Invoke-Chat '/tp @s 0.5 -60 0.5 0 45'
    Invoke-Chat '/gamemode spectator' # spectators don't pick the items up
    Scene 'modeltest' 'drop'
    Save-Screenshot 'ground' | Out-Null
    Invoke-Chat '/tp @s 0.5 -59.5 2 0 50'
    Start-Sleep -Seconds 1
    Save-Screenshot 'ground-close' | Out-Null
    Invoke-Chat '/kill @e[type=item]'
    Invoke-Chat '/gamemode creative'
    Invoke-Chat '/tp @s 0.5 -60 0.5 0 10'
    Scene 'rendertest' 'frames'
    Save-Screenshot 'frames' | Out-Null
    Invoke-Chat '/tp @s 0.5 -60 1.2 0 25'
    Start-Sleep -Seconds 1
    Save-Screenshot 'frames-close' | Out-Null
    foreach ($w in 'a dropped backpack draws its 3D model', 'a framed backpack draws its 3D model') {
        try { Wait-LogLine "item entity models: $w" 5 | Out-Null } catch { $problems += "item entity models: no '$w'" }
    }
    Invoke-Chat '/fill -4 -60 2 4 -58 5 air'
    Invoke-Chat '/kill @e[type=item]'

    }
    # ---- 7. cooking particles and crackle --------------------------------------------------------------
    if (Want 7) {
    Invoke-Chat '/tp @s 0.5 -60 0.5 0 20'
    Scene 'rendertest' 'cooking'
    Start-Sleep -Seconds 2
    Save-Screenshot 'cooking-placed' | Out-Null
    Set-View 'third_person'
    Start-Sleep -Seconds 1
    Save-Screenshot 'cooking-worn-1' | Out-Null
    Start-Sleep -Milliseconds 700
    Save-Screenshot 'cooking-worn-2' | Out-Null
    Set-View 'first_person'
    Invoke-Chat '/camera @s clear'
    try { Wait-LogLine 'upgrade particles: playing block.furnace.lit' 30 | Out-Null } catch { $problems += 'no furnace crackle' }
    Invoke-Chat '/replaceitem entity @s slot.armor.chest 0 air'
    Invoke-Chat '/setblock 0 -60 2 air'

    }
    # ---- 8. mob catcher: properties and the capture animation ------------------------------------------
    if (Want 8) {
    Invoke-Chat '/clear @s'
    $mark = Get-LogMark
    Invoke-Chat '/backpack mobcatcher setup'
    Wait-LogLine 'mobcatcher setup: gave netherite backpack' 15 -Since $mark | Out-Null
    Invoke-Chat '/kill @e[type=!player]'
    Invoke-Chat '/difficulty easy'
    Invoke-Chat '/tp @s 0.5 -60 0.5 0 24'
    Invoke-Chat '/summon cow 0.5 -60 3 180 0'
    Invoke-Chat '/effect @e[type=cow] slowness 600 255 true'
    Invoke-Chat '/tp @e[type=cow] 0.5 -60 3 180 0'
    Scene 'rendertest' 'coldcow'
    Invoke-Chat '/tp @s 0.5 -60 0.5 0 24'
    Start-Sleep -Seconds 1
    Save-Screenshot 'cold-cow' | Out-Null
    Scene 'rendertest' 'slowcapture'
    $mark = Get-LogMark
    Sneak $true
    Start-Sleep -Milliseconds 300
    Click 0.5 0.5 -Relative -Right
    Start-Sleep -Milliseconds 250
    Save-Screenshot 'capture-1' | Out-Null
    Start-Sleep -Milliseconds 700
    Save-Screenshot 'capture-2' | Out-Null
    Start-Sleep -Milliseconds 700
    Save-Screenshot 'capture-3' | Out-Null
    Sneak $false
    try { Wait-LogLine 'mob catcher: capture effect for minecraft:cow \(60 ticks\)' 10 -Since $mark | Out-Null }
    catch { $problems += 'no capture effect on the client' }
    Start-Sleep -Seconds 2
    Scene 'rendertest' 'fastcapture'
    Open-Backpack
    Gui-Move ($L.Width / 2) -30
    Start-Sleep -Milliseconds 900
    Save-Screenshot 'cold-cow-preview' | Out-Null
    try { Wait-LogLine 'mob catcher: applied the properties of captured mob' 5 | Out-Null }
    catch { $problems += 'the captured cow preview did not get its properties' }
    Close-Backpack
    }
    # ---- 9. first person: worn hidden, held with SB's first-person transform ----------------------------
    if (Want 9) {
    Invoke-Chat '/kill @e[type=!player]'
    Invoke-Chat '/gamemode creative'
    Invoke-Chat '/clear @s'
    Invoke-Chat '/camera @s set minecraft:first_person'
    Invoke-Chat '/tp @s 0.5 -60 0.5 0 10'
    # Reference: a vanilla block held in first person.
    Invoke-Chat '/replaceitem entity @s slot.weapon.mainhand 0 chest'
    Start-Sleep -Seconds 1
    Save-Screenshot 'fp-held-chest' | Out-Null
    Invoke-Chat '/replaceitem entity @s slot.weapon.mainhand 0 alleviatedbackpacks:gold_backpack'
    Start-Sleep -Seconds 1
    Save-Screenshot 'fp-held' | Out-Null
    if ($env:ABP_GX1_CALIBRATE) {
        $i = 0
        foreach ($p in $env:ABP_GX1_FP_POSES -split ';') {
            if (-not $p) { continue }
            Invoke-Chat "/backpack modeltest pose `"3 $p`""
            Start-Sleep -Milliseconds 800
            Save-Screenshot "fp-cal-$i" | Out-Null
            $i++
        }
        Invoke-Chat '/backpack modeltest pose off'
    }
    Invoke-Chat '/replaceitem entity @s slot.weapon.mainhand 0 air'
    Invoke-Chat '/replaceitem entity @s slot.armor.chest 0 alleviatedbackpacks:diamond_backpack'
    Start-Sleep -Seconds 1
    Save-Screenshot 'fp-worn' | Out-Null
    Invoke-Chat '/tp @s 0.5 -60 0.5 0 80'
    Start-Sleep -Seconds 1
    Save-Screenshot 'fp-worn-look-down' | Out-Null
    Invoke-Chat '/tp @s 0.5 -60 0.5 0 10'
    Invoke-Chat '/camera @s set minecraft:third_person'
    Start-Sleep -Seconds 1
    Save-Screenshot 'tp-worn-back' | Out-Null
    Invoke-Chat '/camera @s set minecraft:third_person_front'
    Start-Sleep -Seconds 1
    Save-Screenshot 'tp-worn-front' | Out-Null
    Invoke-Chat '/replaceitem entity @s slot.weapon.mainhand 0 alleviatedbackpacks:gold_backpack'
    Start-Sleep -Seconds 1
    Save-Screenshot 'tp-held-front' | Out-Null
    Invoke-Chat '/camera @s set minecraft:first_person'
    Invoke-Chat '/camera @s clear'
    Invoke-Chat '/gamemode survival'
    Open-Inventory
    Start-Sleep -Seconds 2
    Save-Screenshot 'fp-inventory-preview' | Out-Null
    Close-Screen
    Invoke-Chat '/gamemode creative'
    }
} finally {
    try {
        Invoke-Chat '/camera @s clear'
        Invoke-Chat '/kill @e[type=!player]'
        Invoke-Chat '/difficulty peaceful'
        Invoke-Chat '/gamemode survival'
    } catch {}
}
if ($problems.Count -gt 0) { throw ($problems -join '; ') }
Assert-NoErrors
