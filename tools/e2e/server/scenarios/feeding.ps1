# U7 Feeding upgrade: all feeding selftests on BDS (feeding.food_lookup also checks that both
# upgrade items exist in the behavior pack).
$r = Invoke-SelfTest 'feeding.*'
if ($r.Fail -gt 0 -or $r.Pass -lt 16) { throw "feeding selftests: pass=$($r.Pass) fail=$($r.Fail)" }
