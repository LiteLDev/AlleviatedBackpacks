# Draws the Void upgrade textures (16x16): a dark swirling hole on the upgrade plate.
# The advanced variant gets gold corner rivets and a brighter rim.
. "$PSScriptRoot/upgrade_base.ps1" -NoWrite

# k void core, p dark purple, v swirl, w swirl highlight
$voidRows = @(
    '................',
    '................',
    '................',
    '................',
    '......pppp......',
    '.....pvvkpp.....',
    '....pvkkkwvp....',
    '....pkkwkkvp....',
    '....pvkkwkkp....',
    '....pvwkkkvp....',
    '.....ppkvvp.....',
    '......pppp......',
    '................',
    '................',
    '................',
    '................'
)
$voidPalette = @{
    k = '#0b0612'
    p = '#3b1f52'
    v = '#7a3fb0'
    w = '#c08cf0'
}

# Advanced: gold rivets (g) replacing the silver ones.
$advancedRivets = @(
    '................',
    '................',
    '................',
    '...g........g...',
    '................',
    '................',
    '................',
    '................',
    '................',
    '................',
    '................',
    '................',
    '...g........g...',
    '................',
    '................',
    '................'
)

$root = (Resolve-Path "$PSScriptRoot/../..").Path
$basic = Merge-Rows $UpgradeBaseRows $voidRows
Save-PixelArt -Rows $basic -Palette ($UpgradeBasePalette + $voidPalette) -Path (Join-Path $root 'assets/pack/rp/textures/items/void_upgrade.png')

$advPalette = $UpgradeBasePalette + $voidPalette + @{ g = '#f2c94c' }
$advPalette['v'] = '#9b55d8'
$advanced = Merge-Rows (Merge-Rows $UpgradeBaseRows $voidRows) $advancedRivets
Save-PixelArt -Rows $advanced -Palette $advPalette -Path (Join-Path $root 'assets/pack/rp/textures/items/advanced_void_upgrade.png')
Write-Host 'wrote void_upgrade.png, advanced_void_upgrade.png'
