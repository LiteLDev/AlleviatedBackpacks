# U18: Anvil & Smithing upgrades (engine logic + simulated menu interaction). The selftests create
# the upgrade items, so they also prove the behavior-pack items exist.
foreach ($glob in 'anvil.*', 'smithing.*') {
    $r = Invoke-SelfTest $glob
    if ($r.Pass -lt 3) { throw "expected at least 3 passing selftests for $glob" }
}
