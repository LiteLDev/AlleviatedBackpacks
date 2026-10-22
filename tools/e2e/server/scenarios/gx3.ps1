# GX-3 (GUI, settings and input gaps), server side:
#   templates as SNBT, Match-NBT memory slots in sorting, the open tab kept on the storage,
#   right-click stashing in station slots, the Diamond backpack as smithing base without durability.
$expect = @(
    'templates.snbt_files', 'templates.export_pack',
    'sbsettings.memory_match_nbt_sort',
    'mainscreen.open_tab_on_storage', 'mainscreen.kept_search_and_tab',
    'stash.vanilla_station_slots',
    'tiers.smithing_without_durability', 'tiers.recipes_registered', 'tiers.lore_and_tint'
)
$bad = @()
foreach ($name in $expect) {
    $r = Invoke-SelfTest $name
    if ($r.Pass -ne 1 -or $r.Fail -ne 0) { $bad += $name }
}
# The behavior pack loads the netherite smithing recipe without the old durability workaround.
if (Get-ServerLog | Select-String -Pattern 'not a piece of armor or equipment') { $bad += 'netherite smithing recipe refused' }
if (-not (Get-ServerLog | Select-String -Pattern 'smithing: the diamond backpack is accepted as the netherite smithing base')) { $bad += 'smithing base hook not used' }
if ($bad.Count) { throw "GX-3 failures: $($bad -join ', ')" }
