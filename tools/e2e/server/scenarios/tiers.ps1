# U1/P2-12 tiers: SB recipes (tier upgrades, dyeing through the dye tag, next-tier upgrades),
# contents carried over, SB dye colours, creative variants, lore.
$r = Invoke-SelfTest 'tiers.*'
if ($r.Fail -gt 0 -or $r.Pass -lt 10) { throw "expected 10 tiers selftests to pass, got pass=$($r.Pass) fail=$($r.Fail)" }
# The behavior pack must load every recipe without content-log errors.
if (Get-ServerLog | Select-String -Pattern 'alleviatedbackpacks:.*(recipe|Recipe)' | Select-String -Pattern 'ERROR|error|invalid') {
    throw 'content log reports recipe errors'
}