# Draws the Refill / Advanced Refill upgrade textures (16x16): the upgrade base plate with an
# arrow pouring into a stack tray. The advanced tier gets a gold frame.
. "$PSScriptRoot/upgrade_base.ps1" -NoWrite

# g arrow, d arrow shade, w tray, b items in the tray
$symbol = @(
    '................',
    '................',
    '................',
    '.......gd.......',
    '.......gd.......',
    '.......gd.......',
    '.....gggddd.....',
    '......ggdd......',
    '.......gd.......',
    '................',
    '....w.bbbb.w....',
    '....wbbbbbbw....',
    '....wwwwwwww....',
    '................',
    '................',
    '................'
)
$rows = Merge-Rows $UpgradeBaseRows $symbol
$symbolPalette = @{
    g = '#5ee35e'
    d = '#2f9e3a'
    w = '#d8d8d8'
    b = '#8fb7ff'
}

$root = (Resolve-Path "$PSScriptRoot/../..").Path
$basic = $UpgradeBasePalette + $symbolPalette
Save-PixelArt -Rows $rows -Palette $basic -Path (Join-Path $root 'assets/pack/rp/textures/items/refill_upgrade.png')

$advanced = $UpgradeBasePalette.Clone()
$advanced['F'] = '#a87a12'
$advanced['H'] = '#f2cf55'
$advanced['S'] = '#7a5a10'
$advanced = $advanced + $symbolPalette
Save-PixelArt -Rows $rows -Palette $advanced -Path (Join-Path $root 'assets/pack/rp/textures/items/advanced_refill_upgrade.png')
Write-Host 'wrote refill_upgrade.png, advanced_refill_upgrade.png'
