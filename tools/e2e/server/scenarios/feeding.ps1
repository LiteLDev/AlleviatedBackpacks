# U7 Feeding upgrade: all feeding selftests on BDS (feeding.food_lookup also checks that both
# upgrade items exist in the behavior pack).
$r = Invoke-SelfTest 'feeding.*'
if ($r.Pass -lt 15) { throw "expected 15 passing feeding selftests, got $($r.Pass)" }
