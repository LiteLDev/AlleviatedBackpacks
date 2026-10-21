# XP Pump upgrade (U17): storing / taking levels, automatic level keeping, mending, menu.
$r = Invoke-SelfTest 'xppump.*'
if ($r.Pass -lt 1 -or $r.Fail -gt 0) { throw "xppump selftests: pass=$($r.Pass) fail=$($r.Fail)" }
# The upgrade items come from the behavior pack and must exist.
Send-Console 'give @a alleviatedbackpacks:xp_pump_upgrade'
Wait-Seconds 2
if (Get-ServerLog | Select-String 'Unknown item|unknown item') { throw 'xppump items are not registered' }
