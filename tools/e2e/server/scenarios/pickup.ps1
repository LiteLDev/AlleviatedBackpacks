# U5 Pickup & Filter upgrades: selftests on BDS (SimulatedPlayers pick up real item entities).
foreach ($glob in 'pickup.*', 'filter.*') {
    $r = Invoke-SelfTest $glob
    if ($r.Pass -lt 5) { throw "too few passing selftests for ${glob}: pass=$($r.Pass) fail=$($r.Fail)" }
}
# The upgrade items come from the behavior pack and must exist.
$mark = Get-LogMark
foreach ($id in 'pickup_upgrade', 'advanced_pickup_upgrade', 'filter_upgrade', 'advanced_filter_upgrade') {
    Send-Console "give @a alleviatedbackpacks:$id"
}
Wait-Seconds 2
if (Get-ServerLog -Since $mark | Select-String 'Unknown item|unknown item|Syntax error') { throw 'upgrade items are not registered' }