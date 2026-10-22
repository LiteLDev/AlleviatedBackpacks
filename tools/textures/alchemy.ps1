# Draws the Alchemy upgrade textures (16x16): a round-bottomed flask with a bubbling brew on the
# upgrade plate. Basic = magenta brew; Advanced = the same flask with an amber brew and gold rivets.
. "$PSScriptRoot/upgrade_base.ps1" -NoWrite

# c cork, g glass outline, w glass highlight, b brew, d brew shade, l brew light, u bubble
$flask = @(
    '................',
    '................',
    '.......cc.......',
    '.......cc.......',
    '......gwwg......',
    '.......wg.......',
    '......gwug......',
    '.....gwlbbg.....',
    '....gwlbubdg....',
    '....gwbbbbdg....',
    '....gbbbbbdg....',
    '....gdbbbddg....',
    '.....gddddg.....',
    '......gggg......',
    '................',
    '................'
)
$rows = Merge-Rows $UpgradeBaseRows $flask
$root = (Resolve-Path "$PSScriptRoot/../..").Path

$basic = $UpgradeBasePalette.Clone()
$basic['c'] = '#8a5a32'
$basic['g'] = '#2c3440'
$basic['w'] = '#d8f0ff'
$basic['b'] = '#c23fb8'
$basic['d'] = '#7a1f78'
$basic['l'] = '#f08ae6'
$basic['u'] = '#ffd2fa'
$path = Join-Path $root 'assets/pack/rp/textures/items/alchemy_upgrade.png'
Save-PixelArt -Rows $rows -Palette $basic -Path $path
Write-Host "wrote $path"

$adv = $basic.Clone()
$adv['b'] = '#e8a21e'
$adv['d'] = '#a8620c'
$adv['l'] = '#ffd56a'
$adv['u'] = '#fff1c2'
$adv['R'] = '#f5d75a'
$adv['H'] = '#a8927a'
$path = Join-Path $root 'assets/pack/rp/textures/items/advanced_alchemy_upgrade.png'
Save-PixelArt -Rows $rows -Palette $adv -Path $path
Write-Host "wrote $path"
