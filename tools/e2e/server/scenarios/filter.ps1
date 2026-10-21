# Filter upgrade only (the full U5 run is scenario "pickup").
$r = Invoke-SelfTest 'filter.*'
if ($r.Pass -lt 5) { throw "too few passing filter selftests: pass=$($r.Pass) fail=$($r.Fail)" }