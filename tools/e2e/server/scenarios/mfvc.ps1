# P2-4: Magnet, Feeding, Void and Compacting (SB behaviour) plus their native tab data.
$expected = @{ 'magnet.*' = 13; 'feeding.*' = 12; 'void.*' = 9; 'compacting.*' = 7; 'mfvc.*' = 1 }
foreach ($glob in $expected.Keys) {
    $r = Invoke-SelfTest $glob
    if ($r.Fail -gt 0 -or $r.Pass -lt $expected[$glob]) { throw "$glob selftests: pass=$($r.Pass) fail=$($r.Fail)" }
}
