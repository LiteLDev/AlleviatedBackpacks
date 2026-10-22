# Run with -Mode remote-vanilla: a client WITHOUT the AlleviatedBackpacks client mod on a BDS with
# the server mod. Backpack screens need the client mod, so every way of opening one (using the
# item, sneak+use, /backpack open) is refused with a localized message; nothing crashes and no
# chest UI appears.
if ($script:Mode -ne 'remote-vanilla') { throw 'vanilla-refused needs -Mode remote-vanilla' }

Invoke-Chat '/clear @s'
Invoke-Chat '/give @s alleviatedbackpacks:leather_backpack'
Press-Key '1'
Start-Sleep -Seconds 1

$mark = Get-LogMark
Click 0.5 0.5 -Relative -Right   # use the backpack
Wait-LogLine '\[host\].*companion: .* has no client mod; backpack screen refused' 15 -Since $mark | Out-Null
Start-Sleep -Milliseconds 800
$shot = Save-Screenshot 'use-refused'
if ((Get-LeviLog -Since $mark) -match 'menu shown: ') { throw 'a chest menu was opened for a vanilla client' }

Start-Sleep -Seconds 4 # the message is throttled (3 s)
$mark = Get-LogMark
Invoke-Chat '/backpack open'
Wait-LogLine '\[host\].*companion: .* has no client mod; backpack screen refused' 15 -Since $mark | Out-Null
Start-Sleep -Milliseconds 800
Save-Screenshot 'command-refused' | Out-Null
if ((Get-LeviLog -Since $mark) -match 'menu shown: ') { throw 'a chest menu was opened for a vanilla client' }
Assert-NoErrors
