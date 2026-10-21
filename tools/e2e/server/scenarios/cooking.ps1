# U13 cooking: Smelting / Smoking / Blasting upgrades and their Auto variants.
$r = Invoke-SelfTest 'cooking.*'
if ($r.Pass -lt 13 -or $r.Fail -gt 0) { throw "cooking selftests: pass=$($r.Pass) fail=$($r.Fail)" }
# The upgrade items come from the behavior pack and must exist.
foreach ($id in 'smelting', 'smoking', 'blasting', 'auto_smelting', 'auto_smoking', 'auto_blasting') {
    Send-Console "give @a alleviatedbackpacks:${id}_upgrade"
}
Wait-Seconds 2
if (Get-ServerLog | Select-String 'Unknown item|unknown item|Syntax error') { throw 'cooking upgrade items are not registered' }
