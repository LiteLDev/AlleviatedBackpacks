# P2-6 (Tank, Battery, Pump, XP Pump): SB sizing, inventory columns, container slots, pumps,
# experience pump, battery charging and the native screen's tab data / actions.
$problems = @()
foreach ($glob in 'tank.*', 'battery.*', 'pump.*', 'xppump.*', 'api.*') {
    $r = Invoke-SelfTest $glob
    if ($r.Fail -gt 0 -or $r.Pass -lt 4) { $problems += "$glob selftests: pass=$($r.Pass) fail=$($r.Fail)" }
}
# The upgrade items come from the behavior pack and must exist.
foreach ($id in 'tank_upgrade', 'battery_upgrade', 'pump_upgrade', 'advanced_pump_upgrade', 'xp_pump_upgrade') {
    Send-Console "give @a alleviatedbackpacks:$id"
}
Wait-Seconds 2
if (Get-ServerLog | Select-String 'Unknown item|unknown item') { $problems += 'fluid upgrade items are not registered' }
if ($problems) { throw ($problems -join '; ') }
