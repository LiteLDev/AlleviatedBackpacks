# U3 (gui): sorting, transfer buttons, memory / no-sort slots, settings, /backpack open.
$r = Invoke-SelfTest 'gui.*'
if ($r.Pass -lt 12 -or $r.Fail -gt 0) { throw "gui selftests: pass=$($r.Pass) fail=$($r.Fail)" }
# The foundation menu tests still pass with the new buttons / hooks.
$r = Invoke-SelfTest 'menu.*'
if ($r.Fail -gt 0) { throw "menu selftests failed" }
