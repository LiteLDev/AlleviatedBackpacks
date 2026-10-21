# U19 wearable backpacks in a real client: selftests, then the 3D backpack on the player's back
# (third-person views), in the hand, on a zombie, and opening the worn backpack by double sneak.
function Press-F5 { Assert-GameForeground; [AbpE2E.Native]::Key(0x74, $false); Start-Sleep -Milliseconds 60; [AbpE2E.Native]::Key(0x74, $true); Start-Sleep -Milliseconds 600 }

$r = Invoke-SelfTest 'wearable.*'
if ($r.Fail -gt 0 -or $r.Pass -lt 9) { throw "wearable selftests: pass=$($r.Pass) fail=$($r.Fail)" }

Invoke-Chat '/gamemode creative'
Invoke-Chat '/clear @s'
Invoke-Chat '/tp @s ~ ~ ~ 0 10'
Invoke-Chat '/replaceitem entity @s slot.armor.chest 0 alleviatedbackpacks:gold_backpack'
Start-Sleep -Seconds 1
Save-Screenshot 'worn-first-person' | Out-Null
Press-F5                                   # third person, behind the player
Start-Sleep -Seconds 1
Save-Screenshot 'worn-back' | Out-Null
Press-F5                                   # third person, in front of the player
Start-Sleep -Seconds 1
Save-Screenshot 'worn-front' | Out-Null
Press-F5                                   # back to first person

# Other tiers.
Invoke-Chat '/replaceitem entity @s slot.armor.chest 0 alleviatedbackpacks:netherite_backpack'
Press-F5
Start-Sleep -Seconds 1
Save-Screenshot 'worn-back-netherite' | Out-Null
Press-F5; Press-F5

# Held in the hand (small 3D backpack).
Invoke-Chat '/replaceitem entity @s slot.weapon.mainhand 0 alleviatedbackpacks:iron_backpack'
Start-Sleep -Seconds 1
Save-Screenshot 'held-first-person' | Out-Null
Press-F5; Press-F5
Start-Sleep -Seconds 1
Save-Screenshot 'held-front' | Out-Null
Press-F5

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

# A zombie wearing a backpack (mobs render chest attachables too).
Invoke-Chat '/difficulty normal'
Invoke-Chat '/summon husk ~ ~ ~4'
Start-Sleep -Milliseconds 500
Invoke-Chat '/replaceitem entity @e[type=husk] slot.armor.chest 0 alleviatedbackpacks:diamond_backpack'
Invoke-Chat '/effect @e[type=husk] slowness 60 255 true'
Start-Sleep -Seconds 2
Save-Screenshot 'husk-1' | Out-Null
Start-Sleep -Seconds 2
Save-Screenshot 'husk-2' | Out-Null
Invoke-Chat '/kill @e[type=husk]'
Invoke-Chat '/difficulty peaceful'
Invoke-Chat '/gamemode survival'
Assert-NoErrors
