# Bundles and SB `canBePlacedInContainerItems`: refused by default, allowed when the config says so
# (the engine's own bundle validation is asked), `/backpack reload` applies a change, and companion
# clients are told the rule.
$r = Invoke-SelfTest 'bundle.*'
if ($r.Fail -gt 0 -or $r.Pass -lt 5) { throw "expected 5 bundle selftests to pass, got pass=$($r.Pass) fail=$($r.Fail)" }
