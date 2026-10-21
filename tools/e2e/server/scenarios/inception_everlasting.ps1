# U15: Inception & Everlasting upgrades. The selftests install the real upgrade items (they fail
# when the behavior pack doesn't register alleviatedbackpacks:{inception,everlasting}_upgrade).
foreach ($glob in 'inception.*', 'everlasting.*') {
    $r = Invoke-SelfTest $glob
    if ($r.Pass -lt 5 -or $r.Fail -gt 0) { throw "selftests $glob : pass=$($r.Pass) fail=$($r.Fail)" }
}
