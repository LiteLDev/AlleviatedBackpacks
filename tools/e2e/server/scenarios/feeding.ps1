# Feeding upgrade (SB): all feeding selftests on BDS (feeding.registration_and_config also checks that both
# upgrade items exist in the behavior pack).
$r = Invoke-SelfTest 'feeding.*'
if ($r.Fail -gt 0 -or $r.Pass -lt 12) { throw "feeding selftests: pass=$($r.Pass) fail=$($r.Fail)" }
