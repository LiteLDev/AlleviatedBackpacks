# Draws the Stack upgrade / downgrade / conversion item textures (16x16, original art).
#   upgrades:    the upgrade base plate + three stacked bars in the tier colour + a green arrow up
#   downgrades:  the plate + grey bars + a red arrow down + 1-3 pips for the tier
#   conversions: a paper card showing "source tier -> target tier"
. "$PSScriptRoot/lib.ps1"
. "$PSScriptRoot/upgrade_base.ps1" -NoWrite

$root = (Resolve-Path "$PSScriptRoot/../..").Path
$out  = Join-Path $root 'assets/pack/rp/textures/items'

# Tier colours: light (bar) and dark (bar shade).
$tierColors = [ordered]@{
    starter = @('#e8895a', '#9c4f2c') # copper
    tier_1  = @('#ececec', '#9a9a9a') # iron
    tier_2  = @('#fde35a', '#c2961a') # gold
    tier_3  = @('#6ff3ea', '#1f9590') # diamond
    tier_4  = @('#7a6a74', '#3a3036') # netherite
    omega   = @('#e07cff', '#8a2fb8') # omega
}

# B bar, D bar shade, G arrow, K arrow outline
$upSymbol = @(
    '................',
    '................',
    '................',
    '.......GK.......',
    '......GGGK......',
    '.....GGGGGK.....',
    '.......GK.......',
    '....BBBBBBBB....',
    '....DDDDDDDD....',
    '....BBBBBBBB....',
    '....DDDDDDDD....',
    '....BBBBBBBB....',
    '....DDDDDDDD....',
    '................',
    '................',
    '................'
)
# Omega gets a little sparkle on top of the arrow.
$omegaExtra = @(
    '................',
    '................',
    '...P........P...',
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
    '................',
    '................',
    '................'
)
$downSymbol = @(
    '................',
    '................',
    '................',
    '....BBBBBBBB....',
    '....DDDDDDDD....',
    '....BBBBBBBB....',
    '....DDDDDDDD....',
    '.......GK.......',
    '.....GGGGGK.....',
    '......GGGK......',
    '.......GK.......',
    '................',
    '................',
    '................',
    '................',
    '................'
)
$pipRows = @{
    1 = '.......P........'
    2 = '......P.P.......'
    3 = '.....P.P.P......'
}

foreach ($tier in $tierColors.Keys) {
    $c    = $tierColors[$tier]
    $rows = Merge-Rows $UpgradeBaseRows $upSymbol
    if ($tier -eq 'omega') { $rows = Merge-Rows $rows $omegaExtra }
    $pal = $UpgradeBasePalette + @{ B = $c[0]; D = $c[1]; G = '#66e05a'; K = '#1f5a1a'; P = '#fff6a8' }
    $name = if ($tier -eq 'starter') { 'stack_starter_upgrade' } elseif ($tier -eq 'omega') { 'stack_omega_upgrade' } else { "stack_${tier}_upgrade" }
    Save-PixelArt -Rows $rows -Palette $pal -Path (Join-Path $out "$name.png")
    Write-Host "wrote $name.png"
}

foreach ($n in 1, 2, 3) {
    $pips = @('................') * 16
    $pips[12] = $pipRows[$n]
    $rows = Merge-Rows (Merge-Rows $UpgradeBaseRows $downSymbol) $pips
    $pal  = $UpgradeBasePalette + @{ B = '#b5b5b5'; D = '#6e6e6e'; G = '#ff5a4a'; K = '#7a1a12'; P = '#ffd0c8' }
    Save-PixelArt -Rows $rows -Palette $pal -Path (Join-Path $out "stack_downgrade_tier_${n}_upgrade.png")
    Write-Host "wrote stack_downgrade_tier_${n}_upgrade.png"
}

# Conversion card: o outline, W paper, L text lines, G arrow, X/Y source colours, T/U target colours.
$card = @(
    '................',
    '................',
    '.oooooooooooooo.',
    '.oWWWWWWWWWWWWo.',
    '.oWWWWWWWWWWWWo.',
    '.oWXXXWWGWWTTTo.',
    '.oWXYYWWGGWTUUo.',
    '.oWXYYGGGGGTUUo.',
    '.oWXYYWWGGWTUUo.',
    '.oWWWWWWGWWWWWo.',
    '.oWLLLLLLLLLLWo.',
    '.oWWWWWWWWWWWWo.',
    '.oWLLLLLLWWWWWo.',
    '.oooooooooooooo.',
    '................',
    '................'
)
$conversions = 'starter_to_tier_1', 'starter_to_tier_2', 'starter_to_tier_3', 'starter_to_tier_4',
    'tier_1_to_tier_2', 'tier_1_to_tier_3', 'tier_1_to_tier_4', 'tier_2_to_tier_3', 'tier_2_to_tier_4', 'tier_3_to_tier_4'
foreach ($conv in $conversions) {
    $from, $to = $conv -split '_to_'
    $pal = @{
        o = '#2a2420'; W = '#efe4c8'; L = '#bfae8c'; G = '#3d9a2e'
        X = $tierColors[$from][0]; Y = $tierColors[$from][1]
        T = $tierColors[$to][0]; U = $tierColors[$to][1]
    }
    Save-PixelArt -Rows $card -Palette $pal -Path (Join-Path $out "stack_conversion_$conv.png")
    Write-Host "wrote stack_conversion_$conv.png"
}
