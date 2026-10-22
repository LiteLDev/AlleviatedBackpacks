# Draws the Battery Upgrade texture (16x16): a charged cell with two terminals on the upgrade plate.
param([switch]$Preview)

. "$PSScriptRoot/upgrade_base.ps1" -NoWrite

# t terminal, c casing, e casing highlight, y charge (bright), w charge (warm), b bolt
$battery = @(
    '................',
    '................',
    '................',
    '.....tt..tt.....',
    '....cccccccc....',
    '....ceyyyyyc....',
    '....cyyyybyc....',
    '....cyyybbyc....',
    '....cybbbbyc....',
    '....cybbyyyc....',
    '....cwbwwwwc....',
    '....cwwwwwwc....',
    '....cccccccc....',
    '................',
    '................',
    '................'
)
$palette = $UpgradeBasePalette + @{
    t = '#d9921c'
    c = '#2b2b2b'
    e = '#fff6a8'
    y = '#ffe23a'
    w = '#ff7a2a'
    b = '#fffbe6'
}
$root = (Resolve-Path "$PSScriptRoot/../..").Path
$path = Join-Path $root 'assets/pack/rp/textures/items/battery_upgrade.png'
$rows = Merge-Rows $UpgradeBaseRows $battery
Save-PixelArt -Rows $rows -Palette $palette -Path $path
if ($Preview) { Save-PixelArt -Rows $rows -Palette $palette -Path ($path -replace '\.png$', '.preview.png') -Scale 16 }
Write-Host "wrote $path"
