# Capability API (G3) in a real local world: selftests only (the API has no UI of its own).
if (-not (Get-LeviLog | Select-String 'Capability API registered')) { throw 'the capability service was not registered' }
$r = Invoke-SelfTest 'api.*'
if ($r.Pass -lt 6 -or $r.Fail -gt 0) { throw "api selftests: pass=$($r.Pass) fail=$($r.Fail)" }
$r = Invoke-SelfTest 'tank.*'
if ($r.Pass -lt 1 -or $r.Fail -gt 0) { throw "tank selftests: pass=$($r.Pass) fail=$($r.Fail)" }
Assert-NoErrors
