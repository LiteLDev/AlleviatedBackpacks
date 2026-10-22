# Draws the Infinity Upgrade (Admin) and Survival Infinity Upgrade textures (16x16, original pixel
# art) on top of the Upgrade Base plate.
#   pwsh tools/textures/infinity.ps1
#     -> assets/pack/rp/textures/items/{infinity,survival_infinity}_upgrade.png
# Overlay palette letters must not collide (case-insensitively) with the base: o F H M S R.

. "$PSScriptRoot/upgrade_base.ps1" -NoWrite

$root = (Resolve-Path "$PSScriptRoot/../..").Path
$out = Join-Path $root 'assets/pack/rp/textures/items'

# A thick infinity loop with a glint; the admin variant gets a small crown above it.
# p loop, q loop shadow, z glint, k crown, w crown jewel
$loop = @(
    '................',
    '................',
    '................',
    '................',
    '................',
    '...pppp..pppp...',
    '..pzqqpppqqqpp..',
    '..pq..qppq..qp..',
    '..pq..qppq..qp..',
    '..ppqqpqqpqqpp..',
    '...pppp..pppp...',
    '................',
    '................',
    '................',
    '................',
    '................'
)
$crown = @(
    '................',
    '................',
    '.....k.kk.k.....',
    '.....kkwwkk.....',
    '.....kkkkkk.....',
    '................',
    '................',
    '................',
    '................',
    '................',
    '................',
    '................',
    '................',
    '................',
    '................',
    '................'
)

Save-PixelArt -Rows (Merge-Rows (Merge-Rows $UpgradeBaseRows $loop) $crown) -Path (Join-Path $out 'infinity_upgrade.png') -Palette (
    $UpgradeBasePalette + @{ p = '#d63bd0'; q = '#7a1c86'; z = '#ffd6fb'; k = '#ffcf33'; w = '#e8323c' }
)
Save-PixelArt -Rows (Merge-Rows $UpgradeBaseRows $loop) -Path (Join-Path $out 'survival_infinity_upgrade.png') -Palette (
    $UpgradeBasePalette + @{ p = '#4fe05a'; q = '#1f7a2c'; z = '#e1ffd9' }
)
Write-Host "wrote infinity_upgrade.png and survival_infinity_upgrade.png to $out"
