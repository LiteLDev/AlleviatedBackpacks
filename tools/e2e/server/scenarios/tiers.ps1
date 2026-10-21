# U1 tiers: recipes, tier upgrades keeping contents, dyeing, lore.
$r = Invoke-SelfTest 'tiers.*'
if ($r.Pass -lt 7) { throw "expected 7 tiers selftests to pass, got pass=$($r.Pass) fail=$($r.Fail)" }
# The behavior pack must load every tier recipe without content-log errors.
if (Get-ServerLog | Select-String -Pattern 'alleviatedbackpacks:.*(recipe|Recipe)' | Select-String -Pattern 'ERROR|error|invalid') {
    throw 'content log reports recipe errors'
}
