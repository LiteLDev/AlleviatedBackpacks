# Void upgrade (SB): ALWAYS / SLOT_OVERFLOW / STORAGE_OVERFLOW, voidAlwaysEnabled, "work in GUI", fluids, settings menu.
# (The selftests also require both upgrade items to be registered by the behavior pack.)
$r = Invoke-SelfTest 'void.*'
if ($r.Pass -lt 10 -or $r.Fail -gt 0) { throw "void selftests: pass=$($r.Pass) fail=$($r.Fail)" }
