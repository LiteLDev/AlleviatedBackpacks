# Draws the six backpack item textures (16x16, original pixel art).
#   pwsh tools/textures/backpacks.ps1            -> assets/pack/rp/textures/items/<tier>_backpack.png
#   pwsh tools/textures/backpacks.ps1 -Preview   -> also writes 8x previews to tools/textures/preview/
param([switch]$Preview)

. "$PSScriptRoot/lib.ps1"

$root = (Resolve-Path "$PSScriptRoot/../..").Path
$out = Join-Path $root 'assets/pack/rp/textures/items'

# o outline, H highlight, M body, S shadow, T trim (tier metal), d trim shadow, B clasp
$rows = @(
    '................',
    '.....oooooo.....',
    '.....o....o.....',
    '..oooooooooooo..',
    '..oTTTTTTTTTTo..',
    '..oHMMMMMMMMSo..',
    '..oHMMMMMMMMSo..',
    '..odddddBBdddo..',
    '..oHMMMMBBMMSo..',
    '..oHMMMMMMMMSo..',
    '..oHMooooooMSo..',
    '..oHMoTTTToMSo..',
    '..oHMoMMMMoMSo..',
    '..oSSooooooSSo..',
    '...oooooooooo...',
    '................'
)

$leather = '#9c6b3c'
$tiers = [ordered]@{
    leather   = @{ body = $leather;  trim = '#6e4523'; clasp = '#c8a060' }
    copper    = @{ body = $leather;  trim = '#c26a3e'; clasp = '#e8a27a' }
    iron      = @{ body = $leather;  trim = '#c9c9c9'; clasp = '#f2f2f2' }
    gold      = @{ body = $leather;  trim = '#f2c43b'; clasp = '#fff08a' }
    diamond   = @{ body = $leather;  trim = '#4fd6cf'; clasp = '#b5f7f2' }
    netherite = @{ body = '#4a3f45'; trim = '#2b2427'; clasp = '#8c7a84' }
}

foreach ($name in $tiers.Keys) {
    $t = $tiers[$name]
    $pal = @{
        o = Shade-Color $t.body 0.42
        H = Shade-Color $t.body 1.2
        M = $t.body
        S = Shade-Color $t.body 0.75
        T = $t.trim
        d = Shade-Color $t.trim 0.72
        B = $t.clasp
    }
    Save-PixelArt -Rows $rows -Palette $pal -Path (Join-Path $out "${name}_backpack.png")
    if ($Preview) { Save-PixelArt -Rows $rows -Palette $pal -Scale 8 -Path (Join-Path $PSScriptRoot "preview/${name}_backpack.png") }
}
Write-Host "wrote $($tiers.Count) backpack textures to $out"
