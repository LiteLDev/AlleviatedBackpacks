# Foundation (F1): core model, storage, upgrade plumbing and the chest-menu framework.
foreach ($glob in 'core.*', 'storage.*', 'upgrade.*', 'menu.*') {
    $r = Invoke-SelfTest $glob
    if ($r.Pass -lt 1) { throw "no passing selftests for $glob" }
}
# The backpack items come from the behavior pack and must exist.
Send-Console 'backpack give iron'
Send-Console 'give @a alleviatedbackpacks:netherite_backpack'
Wait-Seconds 2
if (Get-ServerLog | Select-String 'Unknown item|unknown item') { throw 'backpack items are not registered' }
