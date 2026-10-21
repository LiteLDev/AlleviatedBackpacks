# Draws the Inception and Everlasting upgrade textures (16x16, original pixel art) on top of the
# Upgrade Base plate.
#   pwsh tools/textures/inception_everlasting.ps1
#     -> assets/pack/rp/textures/items/{inception,everlasting}_upgrade.png
# Overlay palette letters must not collide (case-insensitively) with the base: o F H M S R.

. "$PSScriptRoot/upgrade_base.ps1" -NoWrite

$root = (Resolve-Path "$PSScriptRoot/../..").Path
$out = Join-Path $root 'assets/pack/rp/textures/items'

# Inception: a small backpack (teal) inside a bigger one (purple).
# p outline, l body, k inner outline, e inner bag, w inner clasp
$inception = @(
    '................',
    '................',
    '................',
    '......pppp......',
    '....pppppppp....',
    '....pllllllp....',
    '....plkkkklp....',
    '....plkeeklp....',
    '....plkwwklp....',
    '....plkeeklp....',
    '....plkkkklp....',
    '....pllllllp....',
    '....pppppppp....',
    '................',
    '................',
    '................'
)
Save-PixelArt -Rows (Merge-Rows $UpgradeBaseRows $inception) -Path (Join-Path $out 'inception_upgrade.png') -Palette (
    $UpgradeBasePalette + @{ p = '#2e1747'; l = '#8a56c4'; k = '#1f2f2a'; e = '#3fd9a6'; w = '#d8fff0' }
)

# Everlasting: a golden infinity loop with a light glint.
# y gold, g gold shadow, z glint
$everlasting = @(
    '................',
    '................',
    '................',
    '................',
    '................',
    '....yy....yy....',
    '...zgyy..yygy...',
    '...y..yyyy..y...',
    '...y...yy...y...',
    '...y..yyyy..y...',
    '...gyyg..gyyg...',
    '....gg....gg....',
    '................',
    '................',
    '................',
    '................'
)
Save-PixelArt -Rows (Merge-Rows $UpgradeBaseRows $everlasting) -Path (Join-Path $out 'everlasting_upgrade.png') -Palette (
    $UpgradeBasePalette + @{ y = '#ffd43b'; g = '#c98a12'; z = '#fff7c2' }
)
Write-Host "wrote inception_upgrade.png and everlasting_upgrade.png to $out"
