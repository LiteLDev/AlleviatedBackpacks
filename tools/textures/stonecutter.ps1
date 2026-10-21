# Draws the Stonecutter Upgrade texture (16x16): the upgrade base plate with a saw blade over a stone block.
. "$PSScriptRoot/upgrade_base.ps1" -NoWrite

# b blade, c blade centre, x blade edge, g stone top, d stone side, e stone shadow
$symbol = @(
    '................',
    '................',
    '.......xx.......',
    '.....x.bb.x.....',
    '......bbbb......',
    '....xbbccbbx....',
    '....xbbccbbx....',
    '......bbbb......',
    '...gggxbbxggg...',
    '...gddddddddg...',
    '...gddddddddd...',
    '...gddddddddd...',
    '...geeeeeeeee...',
    '................',
    '................',
    '................'
)
$rows    = Merge-Rows $UpgradeBaseRows $symbol
$palette = $UpgradeBasePalette + @{
    x = '#3d4247'
    b = '#e4e8eb'
    c = '#6b4a2a'
    g = '#b4b4b4'
    d = '#7f7f7f'
    e = '#5c5c5c'
}
$root = (Resolve-Path "$PSScriptRoot/../..").Path
$path = Join-Path $root 'assets/pack/rp/textures/items/stonecutter_upgrade.png'
Save-PixelArt -Rows $rows -Palette $palette -Path $path
Write-Host "wrote $path"
