# Draws the Ender Linker item texture (16x16), original pixel art: an obsidian ring around a
# glowing ender pearl, held by two blaze-gold clasps (the recipe's obsidian, pearls and blaze rods).
. "$PSScriptRoot/lib.ps1"

# o outline, b obsidian, p obsidian purple sheen, g gold clasp, h gold shade, e pearl dark,
# m pearl, w pearl highlight, c pearl core
$Rows = @(
    '................',
    '.....oooooo.....',
    '....obbppbbo....',
    '...obpoooopbo...',
    '..obpoemmeopbo..',
    '.ggopomwmmeopogg',
    '.ghobemwcmmebohg',
    '..gobemccmmebog.',
    '..gobemmcmmebog.',
    '.ghobemmmmmebohg',
    '.ggopoemmmeopogg',
    '..obpoeeeeopbo..',
    '...obpoooopbo...',
    '....obbppbbo....',
    '.....oooooo.....',
    '................'
)
$Palette = @{
    o = '#120d1a'
    b = '#2b2140'
    p = '#4f3b78'
    g = '#f2c84b'
    h = '#b8862a'
    e = '#0d4f47'
    m = '#1f8f7d'
    w = '#c8fff0'
    c = '#5ee8c8'
}

$root = (Resolve-Path "$PSScriptRoot/../..").Path
$path = Join-Path $root 'assets/pack/rp/textures/items/ender_linker.png'
Save-PixelArt -Rows $Rows -Palette $Palette -Path $path
Write-Host "wrote $path"
