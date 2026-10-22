# P2-14 (keybinds, input behaviours, tooltips), server side: right-click stashing rules (SB
# overrideStackedOnOther / overrideOtherStackedOnMe, SC stash overlay results), SB's key action
# order and the contents tooltip lines, plus the companion protocol tests they touch.
$r = Invoke-SelfTest 'stash.*'
if ($r.Pass -lt 6 -or $r.Fail -gt 0) { throw "stash selftests: pass=$($r.Pass) fail=$($r.Fail)" }
$r = Invoke-SelfTest 'input.*'
if ($r.Pass -lt 2 -or $r.Fail -gt 0) { throw "input selftests: pass=$($r.Pass) fail=$($r.Fail)" }
$r = Invoke-SelfTest 'companion.*'
if ($r.Fail -gt 0) { throw "companion selftests failed" }
