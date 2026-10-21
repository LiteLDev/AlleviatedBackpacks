# Draws the Jukebox / Advanced Jukebox upgrade textures (16x16): a music disc on the upgrade plate.
# The advanced one has a gold frame and a blue label.
. "$PSScriptRoot/upgrade_base.ps1" -NoWrite

# k disc outline, g disc body, v groove, L label, x spindle hole
$disc = @(
    '................',
    '................',
    '................',
    '.....kkkkkk.....',
    '....kggggggk....',
    '...kggvvvvggk...',
    '...kgvgLLgvgk...',
    '...kgvLxLLvgk...',
    '...kgvLLLLvgk...',
    '...kgvgLLgvgk...',
    '...kggvvvvggk...',
    '....kggggggk....',
    '.....kkkkkk.....',
    '................',
    '................',
    '................'
)
$rows = Merge-Rows $UpgradeBaseRows $disc
$discPalette = @{ k = '#141414'; g = '#2a2a2e'; v = '#45454c'; x = '#0a0a0a' }

$root = (Resolve-Path "$PSScriptRoot/../..").Path
$basic = $UpgradeBasePalette.Clone() + $discPalette + @{ L = '#e8a33a' }
Save-PixelArt -Rows $rows -Palette $basic -Path (Join-Path $root 'assets/pack/rp/textures/items/jukebox_upgrade.png')

$advanced = $UpgradeBasePalette.Clone() + $discPalette + @{ L = '#4fc3f7' }
$advanced['F'] = '#b8892a'
$advanced['H'] = '#f2d16b'
$advanced['S'] = '#8f6a1f'
Save-PixelArt -Rows $rows -Palette $advanced -Path (Join-Path $root 'assets/pack/rp/textures/items/advanced_jukebox_upgrade.png')
Write-Host 'wrote jukebox_upgrade.png, advanced_jukebox_upgrade.png'
