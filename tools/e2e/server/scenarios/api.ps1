# Capability API (G3): energy / fluid capabilities, fluid registry, cauldrons, placed backpacks,
# plus the phase-1 tank that is wired through it.
if (-not (Get-ServerLog | Select-String 'Capability API registered')) { throw 'the capability service was not registered' }
$r = Invoke-SelfTest 'api.*'
if ($r.Pass -lt 6 -or $r.Fail -gt 0) { throw "api selftests: pass=$($r.Pass) fail=$($r.Fail)" }
foreach ($glob in 'tank.*', 'pump.*', 'xppump.*') {
    $r = Invoke-SelfTest $glob
    if ($r.Pass -lt 1 -or $r.Fail -gt 0) { throw "$glob selftests: pass=$($r.Pass) fail=$($r.Fail)" }
}
