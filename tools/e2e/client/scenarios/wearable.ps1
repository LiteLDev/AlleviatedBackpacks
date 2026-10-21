# U19 wearable backpacks in a real client: selftests, then the 3D backpack on the player's back
# (third-person views), in the hand, on a husk, and opening the worn backpack (right-click on a
# held backpack, double sneak). Camera views use /camera presets (F5 presses after chat get lost).
function Set-View([string]$preset) {
    Invoke-Chat "/camera @s set minecraft:$preset"
    Start-Sleep -Seconds 1
}

$r = Invoke-SelfTest 'wearable.*'
if ($r.Fail -gt 0 -or $r.Pass -lt 10) { throw "wearable selftests: pass=$($r.Pass) fail=$($r.Fail)" }

# Always leave the camera in first person (the game remembers the view for the next run).
try {
    Invoke-Chat '/gamemode creative'
    Invoke-Chat '/tp @s ~ ~ ~ 0 10'
    Invoke-Chat '/replaceitem entity @s slot.armor.chest 0 alleviatedbackpacks:gold_backpack'
    Start-Sleep -Seconds 1
    Save-Screenshot 'worn-first-person' | Out-Null
    Set-View 'third_person'
    Save-Screenshot 'worn-back' | Out-Null
    Set-View 'third_person_front'
    Save-Screenshot 'worn-front' | Out-Null

    # Other tiers.
    Invoke-Chat '/replaceitem entity @s slot.armor.chest 0 alleviatedbackpacks:netherite_backpack'
    Set-View 'third_person'
    Save-Screenshot 'worn-back-netherite' | Out-Null

    # Held in the hand: the normal item icon (the 3D model is only shown when worn).
    Invoke-Chat '/replaceitem entity @s slot.weapon.mainhand 0 alleviatedbackpacks:iron_backpack'
    Set-View 'third_person_front'
    Save-Screenshot 'held-front' | Out-Null
    Set-View 'first_person'
    Save-Screenshot 'held-first-person' | Out-Null

    # Right-clicking a held backpack opens it (it must not be equipped, the chest slot is taken).
    $mark = Get-LogMark
    Click 0.5 0.5 -Relative -Right
    Wait-LogLine 'menu shown: Iron Backpack' 10 -Since $mark | Out-Null
    Start-Sleep -Milliseconds 800
    Save-Screenshot 'use-opens-menu' | Out-Null
    Close-Screen
    Open-Inventory
    Save-Screenshot 'after-use-inventory' | Out-Null
    Close-Screen
    Invoke-Chat '/replaceitem entity @s slot.weapon.mainhand 0 air'

    # Double sneak opens the worn backpack (real key presses).
    $mark = Get-LogMark
    Press-Key 'SHIFT' 120
    Start-Sleep -Milliseconds 80
    Press-Key 'SHIFT' 120
    Wait-LogLine 'menu shown: ' 10 -Since $mark | Out-Null
    Start-Sleep -Milliseconds 800
    Save-Screenshot 'double-sneak-menu' | Out-Null
    Close-Screen

    # A husk wearing a backpack (mobs render chest attachables too), seen from a third-person camera.
    Invoke-Chat '/difficulty normal'
    Invoke-Chat '/summon husk ~2 ~ ~-3'
    Start-Sleep -Milliseconds 500
    Invoke-Chat '/replaceitem entity @e[type=husk] slot.armor.chest 0 alleviatedbackpacks:diamond_backpack'
    Invoke-Chat '/effect @e[type=husk] slowness 60 255 true'
    Set-View 'third_person_front'
    Start-Sleep -Seconds 1
    Save-Screenshot 'husk-1' | Out-Null
    Start-Sleep -Seconds 2
    Save-Screenshot 'husk-2' | Out-Null
    Set-View 'first_person'
    Invoke-Chat '/kill @e[type=husk]'
    Invoke-Chat '/difficulty peaceful'
    Invoke-Chat '/gamemode survival'
} finally {
    try { Invoke-Chat '/camera @s set minecraft:first_person'; Invoke-Chat '/camera @s clear' } catch {}
}
Assert-NoErrors
