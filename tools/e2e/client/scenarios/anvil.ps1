# Anvil, Smithing and Infinity upgrades: selftests in a local world. The real-input GUI flow (the
# native SB tabs) is `anvil_smithing_infinity.ps1`; the phase-1 fake chest no longer appears for
# companion players.
foreach ($glob in 'anvil.*', 'smithing.*', 'infinity.*') {
    $r = Invoke-SelfTest $glob
    if ($r.Fail -gt 0 -or $r.Pass -lt 3) { throw "selftests ${glob}: pass=$($r.Pass) fail=$($r.Fail)" }
}
