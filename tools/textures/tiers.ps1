# Draws the "dyed" icon variants of the six backpacks (16x16, original pixel art).
#   pwsh tools/textures/tiers.ps1            -> assets/pack/rp/textures/items/<tier>_backpack_dyed.png
#   pwsh tools/textures/tiers.ps1 -Preview   -> also writes 8x previews to tools/textures/preview/
#
# Bedrock tints the whole "dyed" texture of a `minecraft:dyeable` item with its colour, so the
# fabric is drawn in light greys (it takes the main colour) and the trim keeps a desaturated hint
# of the tier metal. Same silhouette as tools/textures/backpacks.ps1.
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

$body = '#e6e6e6'
$tiers = [ordered]@{
    leather   = @{ trim = '#8a7a6c'; clasp = '#d8d0c0' }
    copper    = @{ trim = '#c89c86'; clasp = '#f0d8cc' }
    iron      = @{ trim = '#cfcfcf'; clasp = '#ffffff' }
    gold      = @{ trim = '#e8d48c'; clasp = '#fff6c8' }
    diamond   = @{ trim = '#9ce0dc'; clasp = '#e0fbf9' }
    netherite = @{ trim = '#5a5458'; clasp = '#a89ca4' }
}

foreach ($name in $tiers.Keys) {
    $t = $tiers[$name]
    $pal = @{
        o = Shade-Color $body 0.38
        H = '#ffffff'
        M = $body
        S = Shade-Color $body 0.74
        T = $t.trim
        d = Shade-Color $t.trim 0.72
        B = $t.clasp
    }
    Save-PixelArt -Rows $rows -Palette $pal -Path (Join-Path $out "${name}_backpack_dyed.png")
    if ($Preview) { Save-PixelArt -Rows $rows -Palette $pal -Scale 8 -Path (Join-Path $PSScriptRoot "preview/${name}_backpack_dyed.png") }
}
Write-Host "wrote $($tiers.Count) dyed backpack textures to $out"
