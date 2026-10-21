# U16 Jukebox upgrade: selftests (discs, menu, playback, playlist, listeners, stop conditions).
$r = Invoke-SelfTest 'jukebox.*'
if ($r.Pass -lt 12) { throw "expected at least 12 passing jukebox selftests, got $($r.Pass)" }
# The upgrade items come from the behavior pack and must exist.
Send-Console 'give @a alleviatedbackpacks:jukebox_upgrade'
Send-Console 'give @a alleviatedbackpacks:advanced_jukebox_upgrade'
Wait-Seconds 2
if (Get-ServerLog | Select-String 'Unknown item|unknown item') { throw 'jukebox upgrade items are not registered' }
