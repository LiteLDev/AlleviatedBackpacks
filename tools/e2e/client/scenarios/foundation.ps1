# Foundation (F1): every selftest in a local world, then the backpack opened with the real B key.
# The phase-1 fake chest is never shown to a companion player any more (the client mod draws the
# native SB screen), so the item moves the old chest clicks did are covered by `mainscreen` and
# `companion`; this scenario only checks that the key opens the native screen.
$r = Invoke-SelfTest
if ($r.Fail -gt 0 -or $r.Pass -lt 10) { throw "foundation selftests: pass=$($r.Pass) fail=$($r.Fail)" }

Wait-LogLine 'companion: server ready' 90 | Out-Null
Invoke-Chat '/clear @s'
Invoke-Chat '/replaceitem entity @s slot.hotbar 0 alleviatedbackpacks:diamond_backpack'
Press-Key '1'
Start-Sleep -Seconds 1
Save-Screenshot 'holding' | Out-Null

# The open-backpack key (B): selftests may have left blocks in front of the player, so a right click
# could hit one of them instead of using the item.
$mark = Get-LogMark
Press-Key 'B'
Wait-LogLine 'companion: drawing the native backpack screen' 10 -Since $mark | Out-Null
if (Get-LeviLog -Since $mark | Select-String -Pattern 'menu shown: ') { throw 'a fake chest was shown to a companion player' }
Start-Sleep -Milliseconds 800
Save-Screenshot 'native-open' | Out-Null
Close-Screen
Start-Sleep -Seconds 1
Save-Screenshot 'closed' | Out-Null
Assert-NoErrors
