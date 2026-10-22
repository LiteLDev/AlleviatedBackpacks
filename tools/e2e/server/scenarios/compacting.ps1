# Compacting upgrade (SB): recipe lookup, 2x2 / 3x3 compaction after insert, chains, work in GUI, full-slot retry, filter.
$r = Invoke-SelfTest 'compacting.*'
if ($r.Pass -lt 7 -or $r.Fail -gt 0) { throw "compacting selftests: pass=$($r.Pass) fail=$($r.Fail)" }
# The upgrade items come from the behavior pack and must exist.
$mark = Get-LogMark
Send-Console 'give @a alleviatedbackpacks:compacting_upgrade'
Send-Console 'give @a alleviatedbackpacks:advanced_compacting_upgrade'
Wait-Seconds 2
if (Get-ServerLog -Since $mark | Select-String 'Unknown item|unknown item|Syntax error') { throw 'compacting upgrade items are not registered' }
