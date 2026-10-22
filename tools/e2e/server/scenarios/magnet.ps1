# Magnet upgrade (SB): range cube, shared clock, tags, XP into tanks, filter, settings menu.
$r = Invoke-SelfTest 'magnet.*'
if ($r.Fail -gt 0 -or $r.Pass -lt 14) { throw "magnet selftests: pass=$($r.Pass) fail=$($r.Fail)" }
# The upgrade items come from the behavior pack and must exist.
$mark = Get-LogMark
Send-Console 'give @a alleviatedbackpacks:magnet_upgrade'
Send-Console 'give @a alleviatedbackpacks:advanced_magnet_upgrade'
Wait-Seconds 2
if (Get-ServerLog -Since $mark | Select-String 'Unknown item|unknown item') { throw 'magnet upgrade items are not registered' }
