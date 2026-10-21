# Draws the Magnet / Advanced Magnet upgrade textures (16x16): a red horseshoe magnet with
# silver poles on the upgrade plate. The advanced one gets a gold frame.
. "$PSScriptRoot/upgrade_base.ps1" -NoWrite

# x magnet red, y red shade, w pole silver, v pole shade, z field spark
$MagnetRows = @(
    '................',
    '................',
    '................',
    '...z........z...',
    '....wv....wv....',
    '....wv....wv....',
    '....xy....xy....',
    '....xy....xy....',
    '....xy....xy....',
    '....xxy..xxy....',
    '.....xxxxxy.....',
    '......yyyy......',
    '................',
    '................',
    '................',
    '................'
)
$MagnetPalette = @{
    x = '#d83a32'
    y = '#8e1f1a'
    w = '#e6e6ea'
    v = '#9a9aa6'
    z = '#7fe3ff'
}

$root  = (Resolve-Path "$PSScriptRoot/../..").Path
$rows  = Merge-Rows $UpgradeBaseRows $MagnetRows
$basic = $UpgradeBasePalette + $MagnetPalette
$path  = Join-Path $root 'assets/pack/rp/textures/items/magnet_upgrade.png'
Save-PixelArt -Rows $rows -Palette $basic -Path $path
Write-Host "wrote $path"

# Advanced: gold frame / highlight / shadow instead of the bronze ones.
$advanced = $basic.Clone()
$advanced['F'] = '#b8860b'
$advanced['H'] = '#ffd75e'
$advanced['S'] = '#9c7212'
$path = Join-Path $root 'assets/pack/rp/textures/items/advanced_magnet_upgrade.png'
Save-PixelArt -Rows $rows -Palette $advanced -Path $path
Write-Host "wrote $path"
