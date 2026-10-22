# P2-4: Magnet, Feeding, Void and Compacting (SB behaviour) plus their native tab data / actions.
$expected = [ordered]@{ 'magnet.*' = 14; 'feeding.*' = 13; 'void.*' = 10; 'compacting.*' = 8 }
foreach ($glob in $expected.Keys) {
    $r = Invoke-SelfTest $glob
    if ($r.Fail -gt 0 -or $r.Pass -lt $expected[$glob]) { throw "$glob selftests: pass=$($r.Pass) fail=$($r.Fail)" }
}
