# Draws the Crafting Upgrade texture (16x16): the upgrade base plate with a 3x3 crafting grid.
. "$PSScriptRoot/upgrade_base.ps1" -NoWrite

# k grid lines (dark wood), w cells (planks), l cell highlight
# (palette keys are case-insensitive: don't reuse the base's o/F/H/M/S/R)
$symbol = @(
    '................',
    '................',
    '................',
    '...kkkkkkkkkk...',
    '...klwklwklwk...',
    '...kwwkwwkwwk...',
    '...kkkkkkkkkk...',
    '...klwklwklwk...',
    '...kwwkwwkwwk...',
    '...kkkkkkkkkk...',
    '...klwklwklwk...',
    '...kwwkwwkwwk...',
    '...kkkkkkkkkk...',
    '................',
    '................',
    '................'
)
$rows    = Merge-Rows $UpgradeBaseRows $symbol
$palette = $UpgradeBasePalette + @{
    k = '#4a3020'
    w = '#b8894f'
    l = '#e0b673'
}
$root = (Resolve-Path "$PSScriptRoot/../..").Path
$path = Join-Path $root 'assets/pack/rp/textures/items/crafting_upgrade.png'
Save-PixelArt -Rows $rows -Palette $palette -Path $path
Write-Host "wrote $path"
