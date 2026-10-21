# U4 Stack upgrades: limits, multiplication, removal/swap rules, conversions, GUI counts.
$r = Invoke-SelfTest 'stack.*'
if ($r.Fail -gt 0 -or $r.Pass -lt 9) { throw "stack selftests: pass=$($r.Pass) fail=$($r.Fail)" }
# Every stack item comes from the behavior pack.
foreach ($id in 'stack_starter_upgrade', 'stack_tier_1_upgrade', 'stack_tier_4_upgrade', 'stack_omega_upgrade',
    'stack_downgrade_tier_3_upgrade', 'stack_conversion_tier_1_to_tier_2') {
    Send-Console "give @a alleviatedbackpacks:$id"
}
Wait-Seconds 2
if (Get-ServerLog | Select-String 'Unknown item|unknown item') { throw 'stack items are not registered' }
