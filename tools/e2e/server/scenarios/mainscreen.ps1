# P2-1 (main backpack screen), server side: SC sort-by order and name sorting, transfer button
# rules, PICKUP_ALL, Shift + double-click, Ctrl + Shift transfer of a full slot, kept search phrase
# and open tab, the error overlay (SC messages, highlighted slots, 60 tick expiry), model fields
# and the locked backpack slot.
$r = Invoke-SelfTest 'mainscreen.*'
if ($r.Pass -lt 10 -or $r.Fail -gt 0) { throw "mainscreen selftests: pass=$($r.Pass) fail=$($r.Fail)" }
# The native screen protocol and the GUI unit still pass with SC's rules.
$r = Invoke-SelfTest 'companion.*'
if ($r.Fail -gt 0) { throw "companion selftests failed" }
$r = Invoke-SelfTest 'gui.*'
if ($r.Fail -gt 0) { throw "gui selftests failed" }
