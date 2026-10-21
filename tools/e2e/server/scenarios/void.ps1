# Void upgrade (U9): pickup voiding, overflow mode, deny lists, "work in GUI", settings menu.
# (The selftests also require both upgrade items to be registered by the behavior pack.)
$r = Invoke-SelfTest 'void.*'
if ($r.Pass -lt 9 -or $r.Fail -gt 0) { throw "void selftests: pass=$($r.Pass) fail=$($r.Fail)" }
