# P2-10: Anvil, Smithing, Infinity and Survival Infinity upgrades (engine logic, native tab
# actions, simulated menu interaction, permissions, placed-backpack protection). The selftests
# create the upgrade items, so they also prove the behavior-pack items exist.
foreach ($glob in 'anvil.*', 'smithing.*', 'infinity.*') {
    $r = Invoke-SelfTest $glob
    if ($r.Pass -lt 3) { throw "expected at least 3 passing selftests for $glob" }
}
