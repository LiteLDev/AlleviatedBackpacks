# GX-2 (client, `-Mode local` and `-Mode remote-mod`): what of the upgrade / world gaps a player sees.
#   1. SC's ender_linker_endpoint recipe with real input in a crafting table:
#      unlinked linker + iron backpack  -> a bound linker; the backpack stays in the grid (PRIMARY);
#      bound linker + empty leather backpack -> the backpack, now a secondary endpoint.
#   2. Alchemy: one check throws a splash potion (filter 0) and drinks a potion (filter 1), SC's
#      applyTo loop; third-person screenshots of the splash.
#   3. A mob's jukebox backpack plays attached to the mob (the client attaches the record to it).
# Screenshots: crafting grid / results with tooltips, the splash.

if ($env:ABP_E2E_HOST_DIR) { Wait-LogLine 'companion: server ready' 90 | Out-Null }

function Set-View([string]$preset) {
    Invoke-Chat "/camera @s set minecraft:$preset"
    Start-Sleep -Seconds 1
}

# Positions in the crafting table screen (fractions of the 1280x720 screenshot, see recipes.ps1).
$gridX = 0.547, 0.581, 0.615
$gridY = 0.306, 0.367, 0.426
$hotY = 0.729
$outX = 0.731; $outY = 0.367
function HotX([int]$k) { (648 + 43.1 * $k) / 1280 }
function Move-ToGrid([int]$hot, [int]$row, [int]$col) {
    Click (HotX $hot) $hotY -Relative; Start-Sleep -Milliseconds 400
    Click $gridX[$col] $gridY[$row] -Relative; Start-Sleep -Milliseconds 400
}

# ---- 1. Ender Linker crafting --------------------------------------------------------------------------
Invoke-Chat '/clear @s'
Invoke-Chat '/replaceitem entity @s slot.hotbar 0 alleviatedbackpacks:ender_linker 1'
Invoke-Chat '/replaceitem entity @s slot.hotbar 1 alleviatedbackpacks:iron_backpack 1'
Invoke-Chat '/replaceitem entity @s slot.hotbar 2 alleviatedbackpacks:leather_backpack 1'
Invoke-Chat '/setblock ~ ~-1 ~2 crafting_table'
Invoke-Chat '/tp @s ~ ~ ~ 0 40'
Press-Key '9'
Start-Sleep -Milliseconds 500
Click 0.5 0.5 -Relative -Right
Start-Sleep -Seconds 2
Save-Screenshot 'table-open' | Out-Null

# CREATE_PRIMARY
Move-ToGrid 0 1 1   # linker: centre
Move-ToGrid 1 2 1   # iron backpack: below
Move-Mouse 0.80 0.60 -Relative
Start-Sleep -Milliseconds 800
Save-Screenshot 'create-grid' | Out-Null
$mark = Get-LogMark
Click $outX $outY -Relative; Start-Sleep -Milliseconds 700
Click (HotX 0) $hotY -Relative; Start-Sleep -Milliseconds 700
Wait-LogLine 'ender linker craft by .* \(linker_endpoint_primary\): success' 15 -Since $mark | Out-Null
Write-Step 'CREATE_PRIMARY crafted'
Move-Mouse 0.80 0.60 -Relative
Start-Sleep -Milliseconds 800
Save-Screenshot 'create-crafted' | Out-Null
Move-Mouse (HotX 0) $hotY -Relative
Start-Sleep -Milliseconds 900
Save-Screenshot 'create-bound-linker' | Out-Null
Move-Mouse $gridX[1] $gridY[2] -Relative
Start-Sleep -Milliseconds 900
Save-Screenshot 'create-primary-in-grid' | Out-Null
# The primary endpoint back to the hotbar (slot 1).
Click $gridX[1] $gridY[2] -Relative; Start-Sleep -Milliseconds 400
Click (HotX 1) $hotY -Relative; Start-Sleep -Milliseconds 400

# ADD_SECONDARY
Move-ToGrid 0 1 1   # bound linker: centre
Move-ToGrid 2 2 1   # empty leather backpack: below
Move-Mouse 0.80 0.60 -Relative
Start-Sleep -Milliseconds 800
Save-Screenshot 'add-grid' | Out-Null
$mark = Get-LogMark
Click $outX $outY -Relative; Start-Sleep -Milliseconds 900
Wait-LogLine 'ender linker craft by .* \(linker_endpoint_secondary\): success' 15 -Since $mark | Out-Null
Write-Step 'ADD_SECONDARY crafted'
Save-Screenshot 'add-on-cursor' | Out-Null
Click (HotX 2) $hotY -Relative; Start-Sleep -Milliseconds 900
Move-Mouse (HotX 2) $hotY -Relative
Start-Sleep -Milliseconds 900
Save-Screenshot 'add-secondary' | Out-Null
Close-Screen
Start-Sleep -Seconds 1
Open-Inventory
Start-Sleep -Milliseconds 600
Save-Screenshot 'inventory-after' | Out-Null
Close-Screen

# ---- 2. Alchemy: splash + potion in one check ------------------------------------------------------------
try {
    Invoke-Chat '/clear @s'
    Invoke-Chat '/effect @s clear'
    Invoke-Chat '/gamemode survival @s'
    Invoke-Chat '/tp @s ~ ~ ~ 0 10'
    Set-View 'third_person'
    $mark = Get-LogMark
    Invoke-Chat '/backpack alchemy splashsetup'
    Wait-LogLine 'alchemy splashsetup: gave iron backpack' 15 -Since $mark | Out-Null
    for ($i = 0; $i -lt 4; $i++) {
        Start-Sleep -Milliseconds 250
        Save-Screenshot "splash-$i" | Out-Null
    }
    Start-Sleep -Seconds 3
    $m = Get-LogMark
    Invoke-Chat '/backpack alchemy status'
    $line = Wait-LogLine 'alchemy status: effects=' 15 -Since $m
    Write-Step $line
    if ($line -notmatch 'effects=\[[^\]]*\b1:\d' -or $line -notmatch '\b12:\d') { throw "splash (speed) and potion (fire resistance) expected: $line" }
    if ($line -notmatch 'fire_resistance=0') { throw "the potion should have been used: $line" }
} finally {
    Set-View 'first_person'
    Invoke-Chat '/gamemode creative @s'
    Invoke-Chat '/effect @s clear'
}

# ---- 3. A jukebox mob's music follows the mob ------------------------------------------------------------
$mark = Get-LogMark
$r = Invoke-SelfTest 'wearable.mob_jukebox_follows'
if ($r.Fail -gt 0 -or $r.Pass -lt 1) { throw "wearable.mob_jukebox_follows: pass=$($r.Pass) fail=$($r.Fail)" }
$attached = (Get-LeviLog -Since $mark) | Where-Object { $_ -match 'jukebox: playing .* attached to entity' -and $_ -notmatch '\[host\]' }
if (-not $attached) { throw 'the client did not attach the mob jukebox sound to the mob' }
Write-Step "client: $($attached | Select-Object -First 1)"

Assert-NoErrors
