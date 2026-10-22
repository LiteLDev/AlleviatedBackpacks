# P2-11: Sophisticated Backpacks' backpack model on every surface, driven with real input.
# Scenes are built by `/backpack modeltest <scene>` south of the player, who looks south.
#   icons-*      every tier's GUI icon (plain and dyed): vanilla inventory, hotbar, native screen
#   stands-*     worn (backs) and held (fronts) on armor stands, every tier, dyed on odd tiers
#   player-*     worn by the player (back / front), held in first and third person
#   parts        tanks with fluid levels, battery charge, full milk tank, item display (worn)
#   ground       every tier dropped on the ground
#   placed-*     every tier placed as a block, waterlogged, parts, item display, comparator
function Set-View([string]$preset) {
    Invoke-Chat "/camera @s set minecraft:$preset"
    Start-Sleep -Seconds 1
}
function Scene([string]$name) {
    $mark = Get-LogMark
    Invoke-Chat "/backpack modeltest $name"
    Wait-LogLine "model test $($name): \d+ objects" 15 -Since $mark | Out-Null
    Start-Sleep -Seconds 2
}

Wait-LogLine 'companion: server ready' 90 | Out-Null
try {
    Invoke-Chat '/gamemode creative'
    Invoke-Chat '/clear @s'
    Invoke-Chat '/time set noon'
    Invoke-Chat '/tp @s ~ ~ ~ 0 25'
    Start-Sleep -Seconds 1

    # ---- GUI icons --------------------------------------------------------------------------------
    Scene 'kit'
    Invoke-Chat '/replaceitem entity @s slot.hotbar 0 alleviatedbackpacks:netherite_backpack'
    Invoke-Chat '/replaceitem entity @s slot.hotbar 1 alleviatedbackpacks:leather_backpack'
    Start-Sleep -Seconds 2
    Save-Screenshot 'icons-hotbar' | Out-Null
    Invoke-Chat '/gamemode survival'
    Open-Inventory
    Start-Sleep -Seconds 2
    Save-Screenshot 'icons-inventory' | Out-Null
    Close-Screen
    Invoke-Chat '/gamemode creative'
    $mark = Get-LogMark
    Press-Key '1'
    Invoke-Chat '/backpack open'   # (the B key also opens the vanilla emote wheel on remote servers)
    if (Wait-LogLine 'companion: drawing the native backpack screen' 10 -Since $mark) {
        Start-Sleep -Seconds 2
        Save-Screenshot 'icons-native' | Out-Null
        Press-Key 'E'
        Start-Sleep -Seconds 1
    }

    # ---- held / worn by the player ------------------------------------------------------------------
    Invoke-Chat '/clear @s'
    Invoke-Chat '/replaceitem entity @s slot.weapon.mainhand 0 alleviatedbackpacks:gold_backpack'
    Start-Sleep -Seconds 2
    Save-Screenshot 'player-held-first-person' | Out-Null
    $i = 0
    foreach ($p in '3 0 12 -8 0 180 0 0.5') {
        Invoke-Chat "/backpack modeltest pose `"$p`""
        Start-Sleep -Milliseconds 800
        Save-Screenshot "calib-fp-$i" | Out-Null
        $i++
    }
    Set-View 'third_person_front'
    Save-Screenshot 'player-held-front' | Out-Null
    $i = 0
    foreach ($p in '2 2 12 -0.9 0 90 0 0.75') {
        Invoke-Chat "/backpack modeltest pose `"$p`""
        Start-Sleep -Milliseconds 800
        Save-Screenshot "calib-tp-$i" | Out-Null
        $i++
    }
    Invoke-Chat '/backpack modeltest pose off'
    Invoke-Chat '/replaceitem entity @s slot.weapon.mainhand 0 air'
    Invoke-Chat '/replaceitem entity @s slot.armor.chest 0 alleviatedbackpacks:diamond_backpack'
    Start-Sleep -Seconds 1
    Save-Screenshot 'player-worn-front' | Out-Null
    Set-View 'third_person'
    Save-Screenshot 'player-worn-back' | Out-Null
    Set-View 'first_person'
    Invoke-Chat '/replaceitem entity @s slot.armor.chest 0 air'

    # ---- armor stands -----------------------------------------------------------------------------
    Invoke-Chat '/tp @s ~ ~ ~ 0 15'
    Scene 'stands'
    Save-Screenshot 'stands' | Out-Null
    Invoke-Chat '/kill @e[type=armor_stand]'
    Start-Sleep -Seconds 1
    Scene 'parts'
    Save-Screenshot 'parts' | Out-Null
    Invoke-Chat '/tp @s ~ ~ ~ 0 40'
    Start-Sleep -Seconds 1
    Save-Screenshot 'parts-top' | Out-Null
    Invoke-Chat '/kill @e[type=armor_stand]'

    # ---- ground ---------------------------------------------------------------------------------------
    Invoke-Chat '/tp @s ~ ~ ~ 0 45'
    Scene 'drop'
    Save-Screenshot 'ground' | Out-Null
    Invoke-Chat '/kill @e[type=item]'

    # ---- placed -----------------------------------------------------------------------------------------
    Invoke-Chat '/tp @s ~ ~ ~ 0 30'
    Scene 'place'
    # comparator behind the half full leather backpack (placed at ~6 ~ ~6), dust after it
    Invoke-Chat '/setblock ~6 ~ ~7 unpowered_comparator ["minecraft:cardinal_direction"="north"]'
    Invoke-Chat '/setblock ~6 ~ ~8 redstone_wire'
    Start-Sleep -Seconds 3
    Save-Screenshot 'placed' | Out-Null
    Invoke-Chat '/tp @s ~-3 ~1 ~3 0 40'
    Start-Sleep -Seconds 2
    Save-Screenshot 'placed-parts' | Out-Null
    Invoke-Chat '/tp @s ~6 ~ ~ 0 40'
    Start-Sleep -Seconds 1
    Save-Screenshot 'placed-display' | Out-Null
    $i = 0
    foreach ($p in '5 -5 -1 1 -90 0 0 0.5') {
        Invoke-Chat "/backpack modeltest pose `"$p`""
        Start-Sleep -Milliseconds 800
        Save-Screenshot "calib-display-$i" | Out-Null
        $i++
    }
    Invoke-Chat '/backpack modeltest pose off'
    Invoke-Chat '/tp @s ~3 ~ ~ 0 25'
    Start-Sleep -Seconds 2
    Save-Screenshot 'placed-comparator' | Out-Null
    Invoke-Chat '/tp @s ~-3 ~ ~-6 0 30'
    Start-Sleep -Seconds 1
} finally {
    try { Invoke-Chat '/camera @s set minecraft:first_person'; Invoke-Chat '/camera @s clear' } catch {}
}
Assert-NoErrors
