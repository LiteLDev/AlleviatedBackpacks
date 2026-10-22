# P2-7 Alchemy & Advanced Alchemy upgrades: all alchemy selftests on BDS (items, default
# conditions, ON_FIRE / UNDER_WATER / HURT on a real player, splash, milk, ominous bottle, golden
# apple, match options, the carried ticker, tab actions and a placed backpack's targets).
$r = Invoke-SelfTest 'alchemy.*'
if ($r.Fail -gt 0 -or $r.Pass -lt 13) { throw "alchemy selftests: pass=$($r.Pass) fail=$($r.Fail)" }
