# Draws the Pickup and Filter upgrade textures (16x16, basic + advanced) on top of the Upgrade Base.
# Advanced variants get a gold frame.
#
#   pwsh tools/textures/pickup_filter.ps1
. "$PSScriptRoot/upgrade_base.ps1" -NoWrite

$root = (Resolve-Path "$PSScriptRoot/../..").Path
$out  = Join-Path $root 'assets/pack/rp/textures/items'

# Pickup: an arrow dropping into a tray. g arrow light, a arrow dark, t tray, u tray shadow
$pickupRows = @(
    '................',
    '................',
    '................',
    '................',
    '.......ga.......',
    '.......ga.......',
    '.......ga.......',
    '.....gggaaa.....',
    '......ggaa......',
    '.......ga.......',
    '....t......t....',
    '....tuuuuuut....',
    '................',
    '................',
    '................',
    '................'
)
$pickupPalette = @{ g = '#7ee07a'; a = '#3c9a3a'; t = '#d8d8d8'; u = '#9a9a9a' }

# Filter: a funnel. e funnel edge, b funnel body, w drop (palette keys must not clash with the base's, case-insensitively)
$filterRows = @(
    '................',
    '................',
    '................',
    '................',
    '....eeeeeeee....',
    '....ebbbbbbe....',
    '.....ebbbbe.....',
    '......ebbe......',
    '.......be.......',
    '.......be.......',
    '.......ee.......',
    '........w.......',
    '................',
    '................',
    '................',
    '................'
)
$filterPalette = @{ e = '#2f5f7a'; b = '#7ec8e3'; w = '#7ec8e3' }

$advancedFrame = @{ o = '#3a2a0a'; F = '#b8902e'; H = '#f0cf6a'; S = '#8a6a1e' }

function Save-Upgrade([string]$name, [string[]]$symbol, [hashtable]$symbolPalette, [switch]$Advanced) {
    $rows    = Merge-Rows $UpgradeBaseRows $symbol
    $palette = $UpgradeBasePalette.Clone()
    if ($Advanced) { foreach ($k in $advancedFrame.Keys) { $palette[$k] = $advancedFrame[$k] } }
    foreach ($k in $symbolPalette.Keys) { $palette[$k] = $symbolPalette[$k] }
    $path = Join-Path $out "$name.png"
    Save-PixelArt -Rows $rows -Palette $palette -Path $path
    Write-Host "wrote $path"
}

Save-Upgrade 'pickup_upgrade' $pickupRows $pickupPalette
Save-Upgrade 'advanced_pickup_upgrade' $pickupRows $pickupPalette -Advanced
Save-Upgrade 'filter_upgrade' $filterRows $filterPalette
Save-Upgrade 'advanced_filter_upgrade' $filterRows $filterPalette -Advanced
