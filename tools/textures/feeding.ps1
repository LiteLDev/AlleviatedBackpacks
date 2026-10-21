# Draws the Feeding upgrade textures (16x16): an apple with a leaf on the upgrade plate.
# Basic = red apple; Advanced = golden apple with gold rivets.
. "$PSScriptRoot/upgrade_base.ps1" -NoWrite

# a apple, d apple shade, l apple light, t stem, g leaf, k leaf shade
$apple = @(
    '................',
    '................',
    '................',
    '........tgg.....',
    '.......tgk......',
    '.....aaatdaa....',
    '....alaaaaadd...',
    '....llaaaaadd...',
    '....laaaaaadd...',
    '....aaaaaaadd...',
    '.....aaaaadd....',
    '.....daadddd....',
    '......dd.dd.....',
    '................',
    '................',
    '................'
)
$rows = Merge-Rows $UpgradeBaseRows $apple
$root = (Resolve-Path "$PSScriptRoot/../..").Path

$basic = $UpgradeBasePalette.Clone()
$basic['a'] = '#d42a2a'
$basic['d'] = '#8e1616'
$basic['l'] = '#ff8a7a'
$basic['t'] = '#5a3a1e'
$basic['g'] = '#4caf3c'
$basic['k'] = '#2e7a24'
$path = Join-Path $root 'assets/pack/rp/textures/items/feeding_upgrade.png'
Save-PixelArt -Rows $rows -Palette $basic -Path $path
Write-Host "wrote $path"

$adv = $basic.Clone()
$adv['a'] = '#f2c84b'
$adv['d'] = '#b8871c'
$adv['l'] = '#fff3a6'
$adv['R'] = '#f5d75a'
$adv['H'] = '#a8927a'
$path = Join-Path $root 'assets/pack/rp/textures/items/advanced_feeding_upgrade.png'
Save-PixelArt -Rows $rows -Palette $adv -Path $path
Write-Host "wrote $path"
