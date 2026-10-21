# Refill upgrade (U11): logic selftests against SimulatedPlayers, plus the item/recipe registration.
$r = Invoke-SelfTest 'refill.*'
if ($r.Fail -gt 0 -or $r.Pass -lt 8) { throw "refill selftests: pass=$($r.Pass) fail=$($r.Fail)" }
Send-Console 'give @a alleviatedbackpacks:refill_upgrade'
Send-Console 'give @a alleviatedbackpacks:advanced_refill_upgrade'
Wait-Seconds 2
if (Get-ServerLog | Select-String 'Unknown item|unknown item') { throw 'refill upgrade items are not registered' }
