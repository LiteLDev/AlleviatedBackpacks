# P2-11: backpack model render state + SB placed-block behaviour (server side).
# The blocks, the display entity and the model resources load without content errors.
$contentErrors = Get-ServerLog | Select-String -Pattern 'placed_.*backpack|backpack_display|alleviatedbackpacks' | Select-String -Pattern 'error|invalid|fail' -CaseSensitive:$false
if ($contentErrors) { throw "content errors for the P2-11 pack files:`n$($contentErrors -join "`n")" }

$r = Invoke-SelfTest 'model.*'
if ($r.Pass -lt 2 -or $r.Fail -gt 0) { throw "model selftests: pass=$($r.Pass) fail=$($r.Fail)" }
$r = Invoke-SelfTest 'placed.*'
if ($r.Pass -lt 12 -or $r.Fail -gt 0) { throw "placed selftests: pass=$($r.Pass) fail=$($r.Fail)" }
