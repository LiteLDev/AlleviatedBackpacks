# Draws the XP Pump Upgrade texture (16x16): an experience orb flowing down into the plate.
param([switch]$Preview)

. "$PSScriptRoot/upgrade_base.ps1" -NoWrite

# n orb outline, y orb rim, g orb green, l orb light, a arrow
$xp = @(
    '................',
    '................',
    '................',
    '......nnnn......',
    '.....nyyyyn.....',
    '....nyggggyn....',
    '....nygllgyn....',
    '....nygllgyn....',
    '....nyggggyn....',
    '.....nyyyyn.....',
    '......nnnn......',
    '......aaaa......',
    '.......aa.......',
    '................',
    '................',
    '................'
)
$palette = $UpgradeBasePalette + @{
    n = '#2d4a12'
    y = '#c8e03a'
    g = '#6cc832'
    l = '#e4ff8c'
    a = '#f0f0f0'
}
$root = (Resolve-Path "$PSScriptRoot/../..").Path
$path = Join-Path $root 'assets/pack/rp/textures/items/xp_pump_upgrade.png'
$rows = Merge-Rows $UpgradeBaseRows $xp
Save-PixelArt -Rows $rows -Palette $palette -Path $path
if ($Preview) { Save-PixelArt -Rows $rows -Palette $palette -Path ($path -replace '\.png$', '.preview.png') -Scale 16 }
Write-Host "wrote $path"
