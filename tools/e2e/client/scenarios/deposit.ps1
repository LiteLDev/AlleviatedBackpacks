# U10: Deposit / Restock upgrades in a local world.
# 1. selftests; 2. install an Advanced Deposit upgrade through the real backpack GUI and open its
# settings (filter) menu; 3. sneak + use the backpack on a chest: the items move into the chest, a
# message shows above the hotbar and neither the chest nor the backpack GUI opens; 4. a plain use
# still opens the chest.
foreach ($glob in 'deposit.*', 'restock.*') {
    $r = Invoke-SelfTest $glob
    if ($r.Fail -gt 0 -or $r.Pass -lt 3) { throw "${glob}: pass=$($r.Pass) fail=$($r.Fail)" }
}

function Assert-BackpackItems([int]$expected) {
    $mark = Get-LogMark
    Invoke-Chat '/backpack info'
    $line = Wait-LogLine 'backpack info: ' 10 -Since $mark
    if ($line -notmatch " items=$expected ") { throw "expected items=$expected, got: $line" }
}
# Double chest layout (1600x900 client area): slot (row, col) of the 54-slot menu / player hotbar.
function MenuSlot([int]$index) { @((0.365 + 0.035 * ($index % 9)), (0.232 + 0.0623 * [math]::Floor($index / 9))) }
function HotbarSlot([int]$index) { @((0.365 + 0.035 * $index), 0.819) }

Invoke-Chat '/clear @s'
Invoke-Chat '/tp @s 0.5 -60 0.5 facing 0.5 -55 6.5'
Invoke-Chat '/replaceitem entity @s slot.hotbar 0 alleviatedbackpacks:diamond_backpack'
Invoke-Chat '/replaceitem entity @s slot.hotbar 1 alleviatedbackpacks:advanced_deposit_upgrade'
Invoke-Chat '/replaceitem entity @s slot.hotbar 2 cobblestone 64'
Invoke-Chat '/replaceitem entity @s slot.hotbar 3 iron_ingot 20'
Press-Key '1'
Start-Sleep -Seconds 1

# Open the backpack, shift-click the items in, install the upgrade on the upgrades page.
$mark = Get-LogMark
Click 0.5 0.5 -Relative -Right
Wait-LogLine 'menu shown: Diamond Backpack \(54 slots\)' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 800
foreach ($i in 2, 3) {
    $p = HotbarSlot $i
    [AbpE2E.Native]::Key(0x10, $false)
    Click $p[0] $p[1] -Relative
    [AbpE2E.Native]::Key(0x10, $true)
    Start-Sleep -Milliseconds 500
}
$p = MenuSlot 52   # upgrades page button
Click $p[0] $p[1] -Relative
Start-Sleep -Milliseconds 800
$p = HotbarSlot 1
Click $p[0] $p[1] -Relative
Start-Sleep -Milliseconds 400
$p = MenuSlot 0    # first upgrade slot
Click $p[0] $p[1] -Relative
Start-Sleep -Milliseconds 800
Save-Screenshot 'upgrade-installed' | Out-Null
$mark = Get-LogMark
$p = MenuSlot 18   # settings button of upgrade slot 0
Click $p[0] $p[1] -Relative
Wait-LogLine 'menu shown: Advanced Deposit Upgrade \(27 slots\)' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 800
Save-Screenshot 'deposit-settings' | Out-Null
Close-Screen
Start-Sleep -Milliseconds 500
Assert-BackpackItems 84

# A chest in front of the player.
Invoke-Chat '/setblock 0 -60 2 chest'
Invoke-Chat '/tp @s 0.5 -60 0.5 facing 0.5 -59.5 2.5'
Start-Sleep -Seconds 1
Save-Screenshot 'facing-chest' | Out-Null

# Sneak + use: deposit.
$mark = Get-LogMark
[AbpE2E.Native]::Key(0x10, $false)
Start-Sleep -Milliseconds 400
Click 0.5 0.5 -Relative -Right
Start-Sleep -Milliseconds 300
Save-Screenshot 'deposited' | Out-Null
[AbpE2E.Native]::Key(0x10, $true)
Start-Sleep -Milliseconds 800
if (Get-LeviLog -Since $mark | Select-String 'menu shown') { throw 'a menu opened on sneak-use' }
Save-Screenshot 'after-deposit' | Out-Null
Assert-BackpackItems 0

# A plain use still opens the chest (showing the deposited items).
Click 0.5 0.5 -Relative -Right
Start-Sleep -Milliseconds 1200
Save-Screenshot 'chest-opened' | Out-Null
Close-Screen
Assert-NoErrors
