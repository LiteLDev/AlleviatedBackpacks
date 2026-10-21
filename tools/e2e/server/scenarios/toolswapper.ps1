# U12 Tool Swapper: selftests (swap logic, hooks, settings) and the upgrade items.
$r = Invoke-SelfTest 'toolswapper.*'
if ($r.Pass -lt 10) { throw "expected at least 10 passing toolswapper selftests, got $($r.Pass)" }
Send-Console 'give @a alleviatedbackpacks:tool_swapper_upgrade'
Send-Console 'give @a alleviatedbackpacks:advanced_tool_swapper_upgrade'
Wait-Seconds 2
if (Get-ServerLog | Select-String 'Unknown item|unknown item') { throw 'tool swapper items are not registered' }
