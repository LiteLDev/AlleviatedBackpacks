# Pump / Advanced Pump upgrades (U17): held buckets, world sources, cauldrons, fluid filter.
$r = Invoke-SelfTest 'pump.*'
if ($r.Pass -lt 1 -or $r.Fail -gt 0) { throw "pump selftests: pass=$($r.Pass) fail=$($r.Fail)" }
# The upgrade items come from the behavior pack and must exist.
Send-Console 'give @a alleviatedbackpacks:pump_upgrade'
Send-Console 'give @a alleviatedbackpacks:advanced_pump_upgrade'
Wait-Seconds 2
if (Get-ServerLog | Select-String 'Unknown item|unknown item') { throw 'pump items are not registered' }
