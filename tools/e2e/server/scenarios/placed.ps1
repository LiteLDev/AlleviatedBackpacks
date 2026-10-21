# U2: placeable backpack block + hopper upgrades.
# The block, its loot table and the upgrade items come from the behavior pack: no content errors.
$contentErrors = Get-ServerLog | Select-String -Pattern 'placed_backpack|hopper_upgrade|abp_placed' | Select-String -Pattern 'error|invalid|fail' -CaseSensitive:$false
if ($contentErrors) { throw "content errors for the U2 pack files:`n$($contentErrors -join "`n")" }

foreach ($glob in 'placed.*', 'hopper.*') {
    $r = Invoke-SelfTest $glob
    if ($r.Pass -lt 3) { throw "expected passing selftests for $glob (pass=$($r.Pass) fail=$($r.Fail))" }
}
# The admin listing works from the console.
$mark = Get-LogMark
Send-Console 'backpack placed'
Wait-LogLine 'placed backpacks: 0' 10 -Since $mark | Out-Null
