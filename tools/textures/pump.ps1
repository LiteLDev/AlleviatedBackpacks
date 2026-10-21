# Draws the Pump Upgrade and Advanced Pump Upgrade textures (16x16): a hand pump with a water drop.
param([switch]$Preview)

. "$PSScriptRoot/upgrade_base.ps1" -NoWrite

# k handle, x pump body outline, y pump body, z spout, d water drop
$pump = @(
    '................',
    '................',
    '................',
    '...kkkkkk.......',
    '........k.......',
    '.....xxxxxx.....',
    '.....xyyyyx.....',
    '.....xyyyyx.....',
    '......xyyx......',
    '......xyyxzzz...',
    '......xyyxxxzz..',
    '......xyyx...d..',
    '......xxxx..dd..',
    '................',
    '................',
    '................'
)
$root = (Resolve-Path "$PSScriptRoot/../..").Path
$variants = @(
    @{ File = 'pump_upgrade.png';          k = '#3b3b3b'; x = '#30343a'; y = '#8d949e'; z = '#5d636b' },
    @{ File = 'advanced_pump_upgrade.png'; k = '#3b3b3b'; x = '#6b4a10'; y = '#f2c12e'; z = '#b8860b' }
)
foreach ($v in $variants) {
    $palette = $UpgradeBasePalette + @{ k = $v.k; x = $v.x; y = $v.y; z = $v.z; d = '#3d7bf0' }
    $path = Join-Path $root "assets/pack/rp/textures/items/$($v.File)"
    $rows = Merge-Rows $UpgradeBaseRows $pump
    Save-PixelArt -Rows $rows -Palette $palette -Path $path
    if ($Preview) { Save-PixelArt -Rows $rows -Palette $palette -Path ($path -replace '\.png$', '.preview.png') -Scale 16 }
    Write-Host "wrote $path"
}
