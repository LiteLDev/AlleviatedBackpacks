# Draws the Tank Upgrade texture (16x16): a glass tank half full of water on the upgrade plate.
param([switch]$Preview)

. "$PSScriptRoot/upgrade_base.ps1" -NoWrite

# c iron cap, g glass edge, e empty glass, w water, b water highlight
$tank = @(
    '................',
    '................',
    '................',
    '.....cccccc.....',
    '....gceeeecg....',
    '....geeeeeeg....',
    '....geeeeeeg....',
    '....gbbwwwwg....',
    '....gbwwwwwg....',
    '....gwwwwwwg....',
    '....gwwwwwwg....',
    '....gcwwwwcg....',
    '.....cccccc.....',
    '................',
    '................',
    '................'
)
$palette = $UpgradeBasePalette + @{
    c = '#4a4f57'
    g = '#c8dde6'
    e = '#8fa9b3'
    w = '#2f5fd0'
    b = '#6f9cf0'
}
$root = (Resolve-Path "$PSScriptRoot/../..").Path
$path = Join-Path $root 'assets/pack/rp/textures/items/tank_upgrade.png'
$rows = Merge-Rows $UpgradeBaseRows $tank
Save-PixelArt -Rows $rows -Palette $palette -Path $path
if ($Preview) { Save-PixelArt -Rows $rows -Palette $palette -Path ($path -replace '\.png$', '.preview.png') -Scale 16 }
Write-Host "wrote $path"
