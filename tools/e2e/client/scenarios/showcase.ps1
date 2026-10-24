# Showcase media for the README and the levimc.org gallery (docs/showcase/). Stages pleasant scenes
# in a normal world (showcase.prelaunch.ps1) and captures stills and video frames to
# client/out/showcase/. tools/e2e/client/showcase-encode.ps1 turns them into docs/showcase/.
#   $env:ABP_SHOWCASE_PART = 'scout' only looks around the spawn (to choose a seed / spot).

Wait-LogLine 'companion: server ready' 90 | Out-Null
$part = $env:ABP_SHOWCASE_PART

# F1 hides the HUD (and the hand). Chat commands are typed with the HUD shown.
$script:HudOn = $true
function Hud([bool]$on) { if ($script:HudOn -ne $on) { Press-Key F1; Start-Sleep -Milliseconds 400; $script:HudOn = $on } }
function Chat([string]$cmd, [int]$ms = 300) { Hud $true; Invoke-Chat $cmd; Start-Sleep -Milliseconds $ms }
# A still with the HUD hidden, after the chat has faded.
function Shot([string]$name, [int]$settleMs = 3000) { Hud $false; Start-Sleep -Milliseconds $settleMs; Save-Screenshot $name | Out-Null }

# Frames of the game window at the capture rate the machine manages: JPEGs + frames.txt
# (file, seconds since start) for the encoder.
# $actions: @{ <second> = { ... } } run once each when the clock passes them (frames keep true times).
function Save-Frames([string]$name, [double]$seconds, [hashtable]$actions = @{}) {
    $pending = [System.Collections.Generic.List[double]]::new([double[]]@($actions.Keys | Sort-Object))
    $dir = Join-Path (Join-Path $script:OutRoot $script:Unit) "frames-$name"
    if (Test-Path $dir) { Remove-Item -Recurse -Force $dir }
    New-Item -ItemType Directory -Force $dir | Out-Null
    $c = Get-ClientRect
    $h = Get-GameWindow
    $wr = New-Object AbpE2E.Native+RECT; [AbpE2E.Native]::GetWindowRect($h, [ref]$wr) | Out-Null
    $jpg = [System.Drawing.Imaging.ImageCodecInfo]::GetImageEncoders() | Where-Object { $_.MimeType -eq 'image/jpeg' }
    $ep = New-Object System.Drawing.Imaging.EncoderParameters 1
    $ep.Param[0] = New-Object System.Drawing.Imaging.EncoderParameter ([System.Drawing.Imaging.Encoder]::Quality), 95L
    $full = New-Object System.Drawing.Bitmap ($wr.Right - $wr.Left), ($wr.Bottom - $wr.Top)
    $bmp = New-Object System.Drawing.Bitmap $c.W, $c.H
    $lines = [System.Collections.Generic.List[string]]::new()
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $i = 0
    while ($sw.Elapsed.TotalSeconds -lt $seconds) {
        if ($pending.Count -and $sw.Elapsed.TotalSeconds -ge $pending[0]) {
            $at = $pending[0]; $pending.RemoveAt(0)
            $key = $actions.Keys | Where-Object { [double]$_ -eq $at } | Select-Object -First 1
            & $actions[$key]
        }
        $t = $sw.Elapsed.TotalSeconds
        $gf = [System.Drawing.Graphics]::FromImage($full)
        $hdc = $gf.GetHdc()
        [AbpE2E.Native]::PrintWindow($h, $hdc, 2) | Out-Null
        $gf.ReleaseHdc($hdc); $gf.Dispose()
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $g.DrawImage($full, (New-Object System.Drawing.Rectangle 0, 0, $c.W, $c.H), (New-Object System.Drawing.Rectangle ($c.X - $wr.Left), ($c.Y - $wr.Top), $c.W, $c.H), 'Pixel')
        $g.Dispose()
        $f = '{0:D4}.jpg' -f $i
        $bmp.Save((Join-Path $dir $f), $jpg, $ep)
        $lines.Add(('{0} {1:F4}' -f $f, $t))
        $i++
    }
    $full.Dispose(); $bmp.Dispose()
    $lines | Set-Content (Join-Path $dir 'frames.txt')
    Write-Step ("frames {0}: {1} in {2:F1} s ({3:F1} fps)" -f $name, $i, $sw.Elapsed.TotalSeconds, ($i / $sw.Elapsed.TotalSeconds))
}

# Quiet chat and a fixed sunny noon.
Chat '/gamerule sendcommandfeedback false'
Chat '/gamerule commandblockoutput false'
Chat '/gamerule dodaylightcycle false'
Chat '/gamerule doweathercycle false'
Chat '/gamerule domobspawning false'
Chat '/time set 5000'
Chat '/weather clear'
Chat '/gamemode creative'

if ($part -eq 'scout') {
    Chat '/gamemode spectator'
    Start-Sleep -Seconds 8
    foreach ($yaw in 0, 90, 180, 270) {
        Chat "/tp @s ~ ~ ~ $yaw 10" 200
        Shot "scout-$yaw"
    }
    Chat '/tp @s ~ ~25 ~ 0 35' 200
    Shot 'scout-high-0' 5000
    Chat '/tp @s ~ ~ ~ 180 35' 200
    Shot 'scout-high-180' 5000
    Save-Frames 'rate' 3
    Hud $true
    return
}

# ---- native screen helpers (layout logged by the client mod, in GUI units) ----------------------
$script:L = $null
function Read-Layout($mark) {
    $line = Wait-LogLine 'companion: screen layout session=' 15 -Since $mark
    if ($line -notmatch 'left=([\d.]+) top=([\d.]+) width=(\d+) height=(\d+) rows=(\d+)/(\d+) scroll=(\w+) invGuiScale=([\d.]+) .*invLabel=(\d+),(\d+) upgrades=(\d+) tabs=(\d+) slots=(\d+) cols=(\d+)') {
        throw "unexpected layout line: $line"
    }
    $script:L = [pscustomobject]@{
        Left = [double]$Matches[1]; Top = [double]$Matches[2]; Width = [int]$Matches[3]; Height = [int]$Matches[4]
        Inv = [double]$Matches[8]; InvX = [int]$Matches[9]; InvY = [int]$Matches[10]; Cols = [int]$Matches[14]
    }
}
function Gui-Move([double]$gx, [double]$gy) { Move-Mouse (($L.Left + $gx) / $L.Inv) (($L.Top + $gy) / $L.Inv) }
function Gui-Click([double]$gx, [double]$gy, [switch]$Right) { Gui-Move $gx $gy; Start-Sleep -Milliseconds 250; Click (($L.Left + $gx) / $L.Inv) (($L.Top + $gy) / $L.Inv) -Right:$Right; Start-Sleep -Milliseconds 400 }
function Hotbar-Pos([int]$i) { @(($L.InvX + $i * 18 + 8), ($L.InvY + 12 + 54 + 4 + 8)) }
function Upgrade-Pos([int]$i) { @(-7, (6 + $i * 16 + 8)) }
function Tab-Pos([int]$i) { @(($L.Width + 1 + 8), (4 + $i * 25 + 4 + 8)) }
function Open-Backpack {
    $mark = Get-LogMark
    Press-Key 'B'
    Wait-LogLine 'companion: drawing the native backpack screen' 10 -Since $mark | Out-Null
    Read-Layout $mark
    Start-Sleep -Milliseconds 800
}
function Close-Backpack { Press-Key 'E'; Start-Sleep -Milliseconds 1500 }
function Key-Combo([int]$mod, [string]$key) {
    Assert-GameForeground
    [AbpE2E.Native]::Key([uint16]$mod, $false); Start-Sleep -Milliseconds 60
    Press-Key $key
    [AbpE2E.Native]::Key([uint16]$mod, $true); Start-Sleep -Milliseconds 300
}
function Sneak([bool]$down) { Focus-Game; [AbpE2E.Native]::Key(0x10, -not $down); Start-Sleep -Milliseconds 300 }

# A backpack filled like a player's: SB's dynamic templates (/sbp template dynamic ...).
function New-Backpack([string]$name, [string]$tier, [string[]]$items, [string[]]$upgrades) {
    Chat "/sbp template dynamic begin $name `"alleviatedbackpacks:${tier}_backpack`"" 200
    foreach ($it in $items) { Chat "/sbp template dynamic additem $name $it" 100 }
    foreach ($u in $upgrades) { Chat "/sbp template dynamic addupgrade $name alleviatedbackpacks:$u" 100 }
    $mark = Get-LogMark
    Chat "/sbp template dynamic end $name" 500
    Wait-LogLine "templates: dynamic template '$name' ended" 10 -Since $mark | Out-Null
}

# The scenes are built on the grass north of the spawn beach (seed 20260521); spreadplayers puts
# the player on the surface.
$site = if ($env:ABP_SHOWCASE_SITE) { $env:ABP_SHOWCASE_SITE } else { '~-4 ~-24' }
Chat "/spreadplayers $site 0 1 @s" 2500

# ---- 1. the tier lineup, placed by hand (sneak + right-click) -----------------------------------
# A level strip of grass north of the player; facing north, each backpack is placed 2 blocks ahead
# and faces back south, towards the camera.
Chat '/fill ~-7 ~-3 ~-7 ~12 ~-1 ~6 dirt'
Chat '/fill ~-7 ~-1 ~-7 ~12 ~-1 ~6 grass_block'
Chat '/fill ~-7 ~ ~-7 ~12 ~6 ~6 air'
$tiers = 'leather', 'copper', 'iron', 'gold', 'diamond', 'netherite'
for ($i = 0; $i -lt 6; $i++) {
    Chat "/replaceitem entity @s slot.weapon.mainhand 0 alleviatedbackpacks:$($tiers[$i])_backpack" 200
    $dx = if ($i) { 1 } else { 0 }
    Chat "/tp @s ~$dx ~ ~ 180 38" 600
    Sneak $true
    Click 0.5 0.5 -Relative -Right
    Start-Sleep -Milliseconds 500
    Sneak $false
}
Chat '/replaceitem entity @s slot.weapon.mainhand 0 air'
Chat '/gamemode spectator'
Chat '/tp @s ~-2.5 ~-0.6 ~0.4 180 12'   # close and low: the row fills the width
Shot 'placed-lineup' 4000
Chat '/tp @s ~ ~1.2 ~1.6 180 30'
Shot 'placed-lineup-high' 3000
Chat '/tp @s ~2.5 ~-0.6 ~-2 180 0' 1500   # back where the last backpack was placed from
Chat '/gamemode creative'

# ---- 2. the backpack screen, full, with upgrades -------------------------------------------------
Chat '/clear @s'
$items = @(
    'oak_log 192', 'birch_log 128', 'oak_planks 256', 'cobblestone 768', 'cobbled_deepslate 128', 'stone_bricks 128',
    'bricks 64', 'dirt 256', 'gravel 64', 'sand 192', 'glass 64', 'torch 192', 'coal 160', 'raw_iron 48', 'iron_ingot 64', 'raw_gold 30', 'gold_ingot 41',
    'copper_ingot 64', 'redstone 192', 'lapis_lazuli 64', 'diamond 23', 'emerald 17', 'amethyst_shard 24',
    'bread 32', 'apple 16', 'cooked_beef 24', 'carrot 40', 'potato 40', 'golden_carrot 12', 'melon_slice 48',
    'cooked_porkchop 20', 'baked_potato 30', 'pumpkin_pie 8', 'cake 1', 'clay_ball 32', 'slime_ball 12',
    'name_tag 2', 'saddle 1', 'compass 1', 'clock 1', 'book 6', 'experience_bottle 16', 'shears 1',
    'fishing_rod 1', 'bucket 1',
    'iron_pickaxe 1', 'diamond_sword 1', 'bow 1', 'arrow 64', 'string 20', 'leather 12', 'feather 16', 'bone 18',
    'ender_pearl 8', 'flint 12', 'wheat_seeds 30', 'sugar_cane 30', 'oak_sapling 12', 'poppy 6', 'dandelion 8'
)
New-Backpack 'show' 'diamond' $items @('crafting_upgrade', 'magnet_upgrade', 'feeding_upgrade', 'pickup_upgrade', 'smelting_upgrade')
New-Backpack 'tip' 'gold' @('cooked_beef 30', 'iron_ingot 64', 'oak_log 128', 'torch 40', 'diamond 9') @('feeding_upgrade', 'magnet_upgrade')
Press-Key '1'
Start-Sleep -Milliseconds 500
Hud $true
Open-Backpack
Move-Mouse 0.97 0.6 -Relative
Start-Sleep -Milliseconds 800
Save-Screenshot 'screen-full' | Out-Null
$t = Tab-Pos 1; Gui-Click $t[0] $t[1]   # tab 0 is the settings tab; 1.. are the upgrades
Start-Sleep -Milliseconds 800
Move-Mouse 0.97 0.6 -Relative
Start-Sleep -Milliseconds 500
Save-Screenshot 'screen-crafting' | Out-Null
$t = Tab-Pos 1; Gui-Click $t[0] $t[1]   # close it (an open tab covers the ones below)
Start-Sleep -Milliseconds 600
$t = Tab-Pos 3; Gui-Click $t[0] $t[1]
Start-Sleep -Milliseconds 800
Move-Mouse 0.97 0.6 -Relative
Start-Sleep -Milliseconds 500
Save-Screenshot 'screen-feeding' | Out-Null
$t = Tab-Pos 3; Gui-Click $t[0] $t[1]   # close the tab again
# Shift over the gold backpack in the hotbar: its contents tooltip.
$h = Hotbar-Pos 1; Gui-Move $h[0] $h[1]
Start-Sleep -Milliseconds 300
Assert-GameForeground; [AbpE2E.Native]::Key(0x10, $false); Start-Sleep -Milliseconds 900
Save-Screenshot 'tooltip' | Out-Null
[AbpE2E.Native]::Key(0x10, $true); Start-Sleep -Milliseconds 200
Close-Backpack

# ---- 3. magnet in action (video), worn backpack, third person ------------------------------------
# A level grass pad nearer the beach, looking south-east over the sea.
Chat '/spreadplayers ~8 ~13 0 1 @s' 2500
Chat '/fill ~-5 ~-3 ~-5 ~5 ~-1 ~5 dirt'
Chat '/fill ~-5 ~-1 ~-5 ~5 ~-1 ~5 grass_block'
Chat '/fill ~-5 ~ ~-5 ~5 ~5 ~5 air'
Chat '/clear @s'
Chat '/replaceitem entity @s slot.armor.chest 0 alleviatedbackpacks:iron_backpack'
Chat '/replaceitem entity @s slot.hotbar 8 alleviatedbackpacks:advanced_magnet_upgrade'
Press-Key '1'
Open-Backpack
$h = Hotbar-Pos 8; Gui-Click $h[0] $h[1]
$u = Upgrade-Pos 0; Gui-Click $u[0] $u[1]
Move-Mouse 0.97 0.6 -Relative            # no tooltip over the storage when it opens again
Start-Sleep -Milliseconds 300
Close-Backpack
$mark = Get-LogMark
Key-Combo 0x12 'Z'      # Toggle Upgrade 1: magnet off while the stage is set
Wait-LogLine 'key action: .* switched upgrade 1 \(advanced_magnet\) off' 10 -Since $mark | Out-Null
# Things to collect: a few blocks around the player, broken so that they drop as items.
# Two blocks away or more, so that the player doesn't pick them up the vanilla way first.
Chat '/fill ~-3 ~ ~2 ~-1 ~ ~3 pumpkin'
Chat '/fill ~1 ~ ~2 ~3 ~ ~3 melon_block'
Chat '/fill ~-1 ~ ~4 ~1 ~ ~4 oak_log'
Chat '/fill ~-3 ~ ~-3 ~3 ~ ~4 air destroy' 300
Chat '/fill ~-5 ~-1 ~-5 ~5 ~-1 ~5 grass_block replace dirt'   # grass under the blocks turned to dirt
foreach ($it in 'pumpkin', 'melon_slice', 'oak_log') { Chat "/clear @s $it" 100 }
# A free camera behind the player's left shoulder (three-quarter back view, the sea beyond). A
# straight back view shows only the default skin's dark hair, which reads as a black square.
Chat '/tp @s ~ ~ ~ 0 10'
Chat '/camera @s set minecraft:free pos ~2.2 ~2.4 ~-3.4 facing ~ ~1.2 ~1' 6000   # let the chat fade
Shot 'worn-back' 500
Hud $true
Start-Sleep -Seconds 2
# Advanced Magnet (range 5) on (Alt+Z) after 1.5 s: everything in range flies into the backpack; B opens it at 4.5 s.
Save-Frames 'magnet' 8 @{ 1.5 = { Key-Combo 0x12 'Z' }; 4.5 = { Press-Key 'B' } }
Close-Backpack
Chat '/camera @s clear'

# ---- 4. crafting a backpack (crafting table, recipe book) -----------------------------------------
Chat '/clear @s'
Chat '/gamemode survival'
Chat '/give @s leather 4'
Chat '/give @s string 4'
Chat '/give @s chest 1'
Chat '/replaceitem entity @s slot.hotbar 8 crafting_table 1'
Chat '/fill ~-1 ~ ~1 ~1 ~1 ~3 air'
Chat '/setblock ~ ~-1 ~2 crafting_table'
Chat '/tp @s ~ ~ ~ 0 40' 800
Press-Key '9'
Click 0.5 0.5 -Relative -Right
Start-Sleep -Seconds 2
Click 0.30 0.31 -Relative; Start-Sleep -Milliseconds 500
Type-Text 'backpack'
Start-Sleep -Milliseconds 800
Click 0.230 0.378 -Relative; Start-Sleep -Milliseconds 1000
Move-Mouse 0.5 0.97 -Relative
Start-Sleep -Milliseconds 800
Save-Screenshot 'crafting-table' | Out-Null
Close-Screen
Chat '/gamemode creative'
Hud $true
Assert-NoErrors
