# U2: placeable backpack block + hopper upgrades, in a real local world.
# 1. The selftests (simulated players).  2. Real input: sneak + right-click places the backpack,
# right-click opens it, sneak + right-click with an empty hand picks it up again.
foreach ($glob in 'placed.*', 'hopper.*') {
    $r = Invoke-SelfTest $glob
    if ($r.Fail -gt 0 -or $r.Pass -lt 3) { throw "selftests $glob : pass=$($r.Pass) fail=$($r.Fail)" }
}

function Get-PlacedCount {
    $mark = Get-LogMark
    Invoke-Chat '/backpack placed'
    $line = Wait-LogLine 'placed backpacks: \d+' 10 -Since $mark
    return [int]([regex]::Match($line, 'placed backpacks: (\d+)').Groups[1].Value)
}
function Sneak([bool]$down) { Focus-Game; [AbpE2E.Native]::Key(0x10, -not $down); Start-Sleep -Milliseconds 300 }

Invoke-Chat '/clear @s'
Invoke-Chat '/gamemode survival'
Invoke-Chat '/replaceitem entity @s slot.hotbar 0 alleviatedbackpacks:iron_backpack'
Invoke-Chat '/replaceitem entity @s slot.hotbar 1 alleviatedbackpacks:hopper_upgrade'
Invoke-Chat '/tp @s ~ ~ ~ 0 55'
Press-Key '1'
Start-Sleep -Seconds 1
$before = Get-PlacedCount
Save-Screenshot 'aim' | Out-Null

# Sneak + right-click on the ground: the backpack is placed.
Sneak $true
Click 0.5 0.5 -Relative -Right
Start-Sleep -Milliseconds 400
Sneak $false
Start-Sleep -Seconds 1
Save-Screenshot 'placed' | Out-Null
$after = Get-PlacedCount
if ($after -ne $before + 1) { throw "sneak + right-click did not place the backpack ($before -> $after)" }

# A view from the side.
Invoke-Chat '/tp @s ~1.5 ~ ~ 40 40'
Start-Sleep -Seconds 1
Save-Screenshot 'placed-side' | Out-Null
Invoke-Chat '/tp @s ~-1.5 ~ ~ 0 55'
Start-Sleep -Seconds 1

# Right-click: the backpack GUI opens.
$mark = Get-LogMark
Click 0.5 0.5 -Relative -Right
Wait-LogLine 'menu shown: Iron Backpack \(54 slots\)' 15 -Since $mark | Out-Null
Start-Sleep -Milliseconds 800
Save-Screenshot 'placed-gui' | Out-Null
Close-Screen

# Sneak + right-click with an empty hand: back into the hand.
Press-Key '3'
Start-Sleep -Milliseconds 500
Sneak $true
Click 0.5 0.5 -Relative -Right
Start-Sleep -Milliseconds 400
Sneak $false
Start-Sleep -Seconds 1
Save-Screenshot 'picked-up' | Out-Null
$final = Get-PlacedCount
if ($final -ne $before) { throw "sneak + right-click with an empty hand did not pick the backpack up ($after -> $final)" }
Press-Key '3'
$mark = Get-LogMark
Invoke-Chat '/backpack info'
$line = Wait-LogLine 'backpack info: |Hold a backpack' 10 -Since $mark
if ($line -notmatch 'tier=iron') { throw "the picked-up backpack is not in the hand: $line" }
Assert-NoErrors
