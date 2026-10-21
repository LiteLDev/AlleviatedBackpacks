# Draws the textures of unit U2 (placeable backpack block + hopper upgrades), original pixel art:
#   assets/pack/rp/textures/blocks/placed_<tier>_backpack.png   32x32 UV sheet of the block model
#                                                              (models/blocks/abp_placed_backpack.geo.json)
#   assets/pack/rp/textures/items/hopper_upgrade.png            16x16
#   assets/pack/rp/textures/items/advanced_hopper_upgrade.png   16x16
#
#   pwsh tools/textures/placed.ps1 [-Preview]   (-Preview also writes 8x previews to tools/textures/preview/)
param([switch]$Preview)

. "$PSScriptRoot/lib.ps1"
. "$PSScriptRoot/upgrade_base.ps1" -NoWrite

$root = (Resolve-Path "$PSScriptRoot/../..").Path

# ---- block UV sheet ------------------------------------------------------------------------------
# A 32x32 character grid. Regions (x, y, w, h) must match the per-face UVs of the geometry.
function New-Grid([int]$w, [int]$h) {
    $g = New-Object 'char[][]' $h
    for ($y = 0; $y -lt $h; $y++) { $g[$y] = ('.' * $w).ToCharArray() }
    return , $g
}
function Fill($g, [int]$x, [int]$y, [int]$w, [int]$h, [char]$c) {
    for ($j = $y; $j -lt $y + $h; $j++) { for ($i = $x; $i -lt $x + $w; $i++) { $g[$j][$i] = $c } }
}
function Frame($g, [int]$x, [int]$y, [int]$w, [int]$h, [char]$c) {
    for ($i = $x; $i -lt $x + $w; $i++) { $g[$y][$i] = $c; $g[$y + $h - 1][$i] = $c }
    for ($j = $y; $j -lt $y + $h; $j++) { $g[$j][$x] = $c; $g[$j][$x + $w - 1] = $c }
}
function Panel($g, [int]$x, [int]$y, [int]$w, [int]$h) {
    # body panel: highlight on the left/top, shadow on the right/bottom, dark outline
    Fill $g $x $y $w $h 'M'
    Fill $g ($x + 1) ($y + 1) 1 ($h - 2) 'H'
    Fill $g ($x + $w - 2) ($y + 1) 1 ($h - 2) 'S'
    Fill $g ($x + 1) ($y + $h - 2) ($w - 2) 1 'S'
    Frame $g $x $y $w $h 'o'
}

$g = New-Grid 32 32
# Body front (north) 10x12 at (0,0): lid with trim, clasp.
Panel $g 0 0 10 12
Fill $g 1 1 8 3 'T'; Fill $g 1 4 8 1 'd'
Fill $g 4 3 2 3 'B'
# Body back (south) 10x12 at (10,0): two shoulder straps.
Panel $g 10 0 10 12
Fill $g 12 1 1 10 'k'; Fill $g 17 1 1 10 'k'
Fill $g 12 2 1 1 'B'; Fill $g 17 2 1 1 'B'
# Body sides 6x12 at (20,0) and (26,0): lid trim, a side strap.
foreach ($sx in 20, 26) {
    Panel $g $sx 0 6 12
    Fill $g ($sx + 1) 1 4 3 'T'; Fill $g ($sx + 1) 4 4 1 'd'
    Fill $g ($sx + 2) 5 2 6 'k'
}
# Body top 10x6 at (0,12): the lid seen from above.
Fill $g 0 12 10 6 'T'; Frame $g 0 12 10 6 'o'; Fill $g 1 16 8 1 'd'
# Body bottom 10x6 at (10,12).
Fill $g 10 12 10 6 'S'; Frame $g 10 12 10 6 'o'
# Handle 4x1 at (20,12), handle end 1x1 at (24,12).
Fill $g 20 12 4 1 'T'; Fill $g 24 12 1 1 'd'
# Front pocket 8x6 at (0,18): flap with trim and a clasp.
Panel $g 0 18 8 6
Fill $g 1 19 6 2 'T'; Fill $g 3 20 2 2 'B'
# Pocket sides 2x6 at (8,18).
Fill $g 8 18 2 6 'M'; Fill $g 8 18 2 1 'T'; Fill $g 8 23 2 1 'o'
# Pocket top/bottom 8x2 at (10,18).
Fill $g 10 18 8 2 'T'; Fill $g 10 19 8 1 'd'

$blockRows = $g | ForEach-Object { -join $_ }

$leather = '#9c6b3c'
$tiers = [ordered]@{
    leather   = @{ body = $leather;  trim = '#6e4523'; clasp = '#c8a060' }
    copper    = @{ body = $leather;  trim = '#c26a3e'; clasp = '#e8a27a' }
    iron      = @{ body = $leather;  trim = '#c9c9c9'; clasp = '#f2f2f2' }
    gold      = @{ body = $leather;  trim = '#f2c43b'; clasp = '#fff08a' }
    diamond   = @{ body = $leather;  trim = '#4fd6cf'; clasp = '#b5f7f2' }
    netherite = @{ body = '#4a3f45'; trim = '#2b2427'; clasp = '#8c7a84' }
}
$blocks = Join-Path $root 'assets/pack/rp/textures/blocks'
foreach ($name in $tiers.Keys) {
    $t = $tiers[$name]
    $pal = @{
        o = Shade-Color $t.body 0.42
        H = Shade-Color $t.body 1.18
        M = $t.body
        S = Shade-Color $t.body 0.78
        k = Shade-Color $t.body 0.6
        T = $t.trim
        d = Shade-Color $t.trim 0.72
        B = $t.clasp
    }
    Save-PixelArt -Rows $blockRows -Palette $pal -Path (Join-Path $blocks "placed_${name}_backpack.png")
    if ($Preview) { Save-PixelArt -Rows $blockRows -Palette $pal -Scale 8 -Path (Join-Path $PSScriptRoot "preview/placed_${name}_backpack.png") }
}

# ---- hopper upgrades -------------------------------------------------------------------------------
# A small hopper (funnel + spout) on the upgrade plate. g hopper body, l light edge, n inside, p spout
$hopperSymbol = @(
    '................',
    '................',
    '................',
    '................',
    '....gggggggg....',
    '....glnnnnlg....',
    '....glnnnnlg....',
    '....gggggggg....',
    '.....gllllg.....',
    '......gllg......',
    '.......pp.......',
    '.......pp.......',
    '................',
    '................',
    '................',
    '................'
)
$items = Join-Path $root 'assets/pack/rp/textures/items'
$variants = [ordered]@{
    hopper_upgrade          = @{ g = '#3a3a3a'; l = '#8a8a8a'; n = '#1c1c1c'; p = '#5a5a5a' }
    advanced_hopper_upgrade = @{ g = '#7a5a10'; l = '#f2c43b'; n = '#2a2008'; p = '#fff08a' }
}
foreach ($name in $variants.Keys) {
    $rows = Merge-Rows $UpgradeBaseRows $hopperSymbol
    $pal = $UpgradeBasePalette + $variants[$name]
    Save-PixelArt -Rows $rows -Palette $pal -Path (Join-Path $items "$name.png")
    if ($Preview) { Save-PixelArt -Rows $rows -Palette $pal -Scale 8 -Path (Join-Path $PSScriptRoot "preview/$name.png") }
}
Write-Host "wrote $($tiers.Count) placed backpack textures and $($variants.Count) hopper upgrade textures"
