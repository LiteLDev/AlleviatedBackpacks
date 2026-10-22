# P2-8 Mob Catcher / Advanced Mob Catcher: capture rules, footprints, slot blocking, release,
# removal/swap refusals and the native screen data. Hostile tests raise the difficulty themselves.
$r = Invoke-SelfTest 'mobcatcher.*'
if ($r.Fail -gt 0 -or $r.Pass -lt 8) { throw "mobcatcher selftests: pass=$($r.Pass) fail=$($r.Fail)" }
