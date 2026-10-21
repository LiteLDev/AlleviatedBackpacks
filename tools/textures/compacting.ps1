# Draws the Compacting Upgrade textures (16x16): four small lumps in the corners being pressed
# into one solid block in the middle. Basic = iron colours, advanced = gold colours and frame.
. "$PSScriptRoot/upgrade_base.ps1" -NoWrite

# c lump, d motion dot, X block, Y block shade
$symbol = @(
    '................',
    '................',
    '................',
    '...cc......cc...',
    '...cc......cc...',
    '.....d....d.....',
    '......XXXY......',
    '......XXXY......',
    '......XXXY......',
    '......YYYY......',
    '.....d....d.....',
    '...cc......cc...',
    '...cc......cc...',
    '................',
    '................',
    '................'
)
$rows = Merge-Rows $UpgradeBaseRows $symbol
$root = (Resolve-Path "$PSScriptRoot/../..").Path

$basic = $UpgradeBasePalette.Clone()
$basic['c'] = '#d0d0d0'
$basic['d'] = '#a8a8a8'
$basic['X'] = '#ececec'
$basic['Y'] = '#9a9a9a'
$path = Join-Path $root 'assets/pack/rp/textures/items/compacting_upgrade.png'
Save-PixelArt -Rows $rows -Palette $basic -Path $path
Write-Host "wrote $path"

$advanced = $UpgradeBasePalette.Clone()
$advanced['F'] = '#8a6d2a'
$advanced['H'] = '#d9b54a'
$advanced['S'] = '#6e5520'
$advanced['c'] = '#f2cf46'
$advanced['d'] = '#c9a227'
$advanced['X'] = '#ffe875'
$advanced['Y'] = '#b8901c'
$path = Join-Path $root 'assets/pack/rp/textures/items/advanced_compacting_upgrade.png'
Save-PixelArt -Rows $rows -Palette $advanced -Path $path
Write-Host "wrote $path"
