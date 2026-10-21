# U14: Crafting + Stonecutter upgrades (logic selftests against the real recipe registry).
foreach ($glob in 'crafting.*', 'stonecutter.*') {
    $r = Invoke-SelfTest $glob
    if ($r.Pass -lt 1) { throw "no passing selftests for $glob" }
}
Send-Console 'give @a alleviatedbackpacks:crafting_upgrade'
Send-Console 'give @a alleviatedbackpacks:stonecutter_upgrade'
Wait-Seconds 2
if (Get-ServerLog | Select-String 'Unknown item|unknown item') { throw 'upgrade items are not registered' }
