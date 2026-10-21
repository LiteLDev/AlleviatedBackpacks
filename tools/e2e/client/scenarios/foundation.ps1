# Foundation (F1): selftests in a local world, then the real backpack GUI driven by real clicks.
$r = Invoke-SelfTest
if ($r.Fail -gt 0 -or $r.Pass -lt 10) { throw "foundation selftests: pass=$($r.Pass) fail=$($r.Fail)" }

Invoke-Chat '/clear @s'
Invoke-Chat '/replaceitem entity @s slot.hotbar 0 alleviatedbackpacks:diamond_backpack'
Invoke-Chat '/replaceitem entity @s slot.hotbar 1 diamond 20'
Press-Key '1'
Start-Sleep -Seconds 1
Save-Screenshot 'holding' | Out-Null

function Open-Backpack([string]$shot) {
    $mark = Get-LogMark
    Click 0.5 0.5 -Relative -Right
    Wait-LogLine 'menu shown: Diamond Backpack \(54 slots\)' 10 -Since $mark | Out-Null
    Start-Sleep -Milliseconds 800
    Save-Screenshot $shot | Out-Null
}
function Assert-BackpackItems([int]$expected) {
    $mark = Get-LogMark
    Invoke-Chat '/backpack info'
    $line = Wait-LogLine 'backpack info: ' 10 -Since $mark
    if ($line -notmatch " items=$expected ") { throw "expected items=$expected, got: $line" }
}

# Fractions of the 1600x900 client area (GUI scale of the harness): double chest layout.
$hotbar1  = @(0.400, 0.819)   # player hotbar slot 2 (the diamonds)
$storage0 = @(0.365, 0.232)   # first backpack slot
$invSlot  = @(0.400, 0.632)   # player inventory, first row, second column

# 1. Right-click the backpack: the fake chest GUI opens.
Open-Backpack 'menu-storage'

# 2. Shift-click the diamonds into the backpack.
[AbpE2E.Native]::Key(0x10, $false)
Click $hotbar1[0] $hotbar1[1] -Relative
[AbpE2E.Native]::Key(0x10, $true)
Start-Sleep -Milliseconds 800
Save-Screenshot 'shift-click-in' | Out-Null
Close-Screen
Assert-BackpackItems 20

# 3. Reopen: the diamonds are still there. Pick them up and drop them into the inventory.
Open-Backpack 'reopened'
Click $storage0[0] $storage0[1] -Relative
Start-Sleep -Milliseconds 500
Save-Screenshot 'cursor' | Out-Null
Click $invSlot[0] $invSlot[1] -Relative
Start-Sleep -Milliseconds 800
Save-Screenshot 'moved-out' | Out-Null
Close-Screen
Start-Sleep -Seconds 1
Save-Screenshot 'closed' | Out-Null
Assert-BackpackItems 0
Assert-NoErrors
