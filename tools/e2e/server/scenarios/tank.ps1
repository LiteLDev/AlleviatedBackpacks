# Tank upgrade (U17): fluid tanks, container input/output slots, backpack fluid API.
$r = Invoke-SelfTest 'tank.*'
if ($r.Pass -lt 1 -or $r.Fail -gt 0) { throw "tank selftests: pass=$($r.Pass) fail=$($r.Fail)" }
# The upgrade items come from the behavior pack and must exist.
Send-Console 'give @a alleviatedbackpacks:tank_upgrade'
Wait-Seconds 2
if (Get-ServerLog | Select-String 'Unknown item|unknown item') { throw 'tank items are not registered' }
