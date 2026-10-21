# U16 Jukebox upgrade: selftests in a local world, then the real GUI: install an Advanced Jukebox
# upgrade, open its menu, insert a disc, play and stop with real clicks.
$r = Invoke-SelfTest 'jukebox.*'
if ($r.Fail -gt 0 -or $r.Pass -lt 12) { throw "jukebox selftests: pass=$($r.Pass) fail=$($r.Fail)" }

# Double chest layout (fractions of the 1600x900 client area, see foundation.ps1).
function MenuSlot([int]$n) { return @((0.365 + ($n % 9) * 0.035), (0.232 + [math]::Floor($n / 9) * 0.0622)) }
function InvSlot([int]$col, [int]$row) { return @((0.365 + $col * 0.035), (0.632 + $row * 0.0622)) }
function Shift-Click($pos) {
    [AbpE2E.Native]::Key(0x10, $false)
    Click $pos[0] $pos[1] -Relative
    [AbpE2E.Native]::Key(0x10, $true)
    Start-Sleep -Milliseconds 800
}
function Click-At($pos) {
    Click $pos[0] $pos[1] -Relative
    Start-Sleep -Milliseconds 800
}

Invoke-Chat '/clear @s'
Invoke-Chat '/replaceitem entity @s slot.hotbar 0 alleviatedbackpacks:gold_backpack'
Invoke-Chat '/replaceitem entity @s slot.inventory 0 alleviatedbackpacks:advanced_jukebox_upgrade'
Invoke-Chat '/replaceitem entity @s slot.inventory 1 music_disc_cat'
Press-Key '1'
Start-Sleep -Seconds 1

# 1. Open the backpack, go to the upgrades page, shift-click the upgrade in.
$mark = Get-LogMark
Click 0.5 0.5 -Relative -Right
Wait-LogLine 'menu shown: .*\(54 slots\)' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 800
Click-At (MenuSlot 52)
Shift-Click (InvSlot 0 0)
Save-Screenshot 'upgrades-installed' | Out-Null

# 2. Settings button of upgrade slot 0 opens the jukebox menu.
$mark = Get-LogMark
Click-At (MenuSlot 18)
Wait-LogLine 'menu shown: Advanced Jukebox Upgrade \(54 slots\)' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 800
Save-Screenshot 'jukebox-menu' | Out-Null

# 3. Shift-click the disc in, press Play.
Shift-Click (InvSlot 1 0)
$mark = Get-LogMark
Click-At (MenuSlot 48)
Wait-LogLine 'jukebox: playing record\.cat' 10 -Since $mark | Out-Null
$status = MenuSlot 4
Move-Mouse $status[0] $status[1] -Relative
Start-Sleep -Seconds 3
Save-Screenshot 'jukebox-playing' | Out-Null

# 4. Stop.
$mark = Get-LogMark
Click-At (MenuSlot 49)
Wait-LogLine 'jukebox: stopped' 10 -Since $mark | Out-Null
Save-Screenshot 'jukebox-stopped' | Out-Null
Close-Screen
Assert-NoErrors
