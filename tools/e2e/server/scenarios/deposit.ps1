# U10: Deposit / Restock upgrades (logic, settings menu, sneak-use on containers).
foreach ($glob in 'deposit.*', 'restock.*') {
    $r = Invoke-SelfTest $glob
    if ($r.Pass -lt 3) { throw "too few passing selftests for ${glob}: pass=$($r.Pass) fail=$($r.Fail)" }
}
# The upgrade items come from the behavior pack and must exist.
foreach ($id in 'deposit_upgrade', 'advanced_deposit_upgrade', 'restock_upgrade', 'advanced_restock_upgrade') {
    Send-Console "give @a alleviatedbackpacks:$id"
}
Wait-Seconds 2
if (Get-ServerLog | Select-String 'Unknown item|unknown item') { throw 'upgrade items are not registered' }
