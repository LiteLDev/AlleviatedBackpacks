# Draws the Anvil Upgrade texture: a small anvil on the upgrade base.
. "$PSScriptRoot/upgrade_base.ps1" -NoWrite

# K anvil body, L top highlight, D shadow
$anvil = @(
    '................',
    '................',
    '................',
    '................',
    '...KKKKKKKKKK...',
    '...KLLLLLLLLK...',
    '....KKKKKKKK....',
    '......KDDK......',
    '......KDDK......',
    '.....KKKKKK.....',
    '....KLLLLLLK....',
    '....KDDDDDDK....',
    '................',
    '................',
    '................',
    '................'
)
$rows = Merge-Rows $UpgradeBaseRows $anvil
$pal  = $UpgradeBasePalette + @{ K = '#3a3a3e'; L = '#8a8a92'; D = '#242428' }

$root = (Resolve-Path "$PSScriptRoot/../..").Path
$path = Join-Path $root 'assets/pack/rp/textures/items/anvil_upgrade.png'
Save-PixelArt -Rows $rows -Palette $pal -Path $path
Write-Host "wrote $path"
