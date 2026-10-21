# G2 (client companion), server side: handshake bookkeeping, native screen model + patches,
# screen actions through the menu framework's authority checks, contents tooltip, key actions.
$r = Invoke-SelfTest 'companion.*'
if ($r.Pass -lt 10 -or $r.Fail -gt 0) { throw "companion selftests: pass=$($r.Pass) fail=$($r.Fail)" }
# The chest menus still behave (observers and slot overrides are additive).
$r = Invoke-SelfTest 'menu.*'
if ($r.Fail -gt 0) { throw "menu selftests failed" }
Send-Console 'backpack companion'
Wait-LogLine 'companion: 0 player\(s\) with the client mod' 15 | Out-Null
