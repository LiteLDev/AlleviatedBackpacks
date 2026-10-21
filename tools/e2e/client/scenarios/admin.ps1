# Admin unit (U20): selftests in a local world, then the real commands typed in chat by the
# (operator) player, and `/backpack openfor` showing another backpack's GUI.
$r = Invoke-SelfTest 'admin.*'
if ($r.Fail -gt 0 -or $r.Pass -lt 12) { throw "admin selftests: pass=$($r.Pass) fail=$($r.Fail)" }

Invoke-Chat '/clear @s'
Invoke-Chat '/replaceitem entity @s slot.hotbar 0 alleviatedbackpacks:gold_backpack'
Invoke-Chat '/replaceitem entity @s slot.hotbar 1 emerald 12'
Press-Key '1'
Start-Sleep -Seconds 1

# Open the backpack once (assigns its uuid) and put the emeralds in.
$mark = Get-LogMark
Click 0.5 0.5 -Relative -Right
Wait-LogLine 'menu shown: Gold Backpack' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 800
[AbpE2E.Native]::Key(0x10, $false)
Click 0.400 0.819 -Relative
[AbpE2E.Native]::Key(0x10, $true)
Start-Sleep -Milliseconds 800
Close-Screen

$mark = Get-LogMark
Invoke-Chat '/backpack info'
$line = Wait-LogLine 'backpack info: uuid=' 10 -Since $mark
if ($line -notmatch 'uuid=([0-9a-f-]{36})') { throw "no uuid in: $line" }
$uuid = $Matches[1]

# Let the holder scan record the player, then list / stats in chat.
Start-Sleep -Seconds 2
$mark = Get-LogMark
Invoke-Chat '/backpack list'
Wait-LogLine 'admin: list -> \d+ backpacks' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 500
Save-Screenshot 'list' | Out-Null

$mark = Get-LogMark
Invoke-Chat "/backpack info $($uuid.Substring(0, 8))"
$info = Wait-LogLine 'admin: info ' 10 -Since $mark
if ($info -notmatch '12 items') { throw "expected 12 items: $info" }
Start-Sleep -Milliseconds 500
Save-Screenshot 'info' | Out-Null

# Move the backpack away, then inspect it with openfor.
Invoke-Chat '/clear @s'
$mark = Get-LogMark
Invoke-Chat "/backpack openfor $uuid"
Wait-LogLine 'menu shown: Backpack ' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 1000
Save-Screenshot 'openfor' | Out-Null
Close-Screen

$mark = Get-LogMark
Invoke-Chat '/backpack stats'
Wait-LogLine 'admin: stats stored=' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 500
Save-Screenshot 'stats' | Out-Null

# Give it back by uuid.
$mark = Get-LogMark
Invoke-Chat "/backpack give @s $uuid"
Wait-LogLine "admin: gave backpack $uuid to " 10 -Since $mark | Out-Null
Assert-NoErrors
