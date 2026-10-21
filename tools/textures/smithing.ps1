# Draws the Smithing Upgrade texture: a smithing hammer over a netherite ingot on the upgrade base.
. "$PSScriptRoot/upgrade_base.ps1" -NoWrite

# G hammer head, W head highlight, T handle, N netherite ingot, Q ingot highlight
$symbol = @(
    '................',
    '................',
    '................',
    '...GGGGG........',
    '...GWWWG........',
    '...GGGGG........',
    '.....T..........',
    '......T.........',
    '.......T........',
    '........T.......',
    '......NNNNNN....',
    '.....NQQQQQN....',
    '.....NNNNNNN....',
    '................',
    '................',
    '................'
)
$rows = Merge-Rows $UpgradeBaseRows $symbol
$pal  = $UpgradeBasePalette + @{ G = '#4a4a50'; W = '#a0a0a8'; T = '#6b4423'; N = '#3b2f34'; Q = '#6a5960' }

$root = (Resolve-Path "$PSScriptRoot/../..").Path
$path = Join-Path $root 'assets/pack/rp/textures/items/smithing_upgrade.png'
Save-PixelArt -Rows $rows -Palette $pal -Path $path
Write-Host "wrote $path"
