# Worn-backpack render assets (original art + model):
#   assets/pack/rp/textures/entity/alleviatedbackpacks/<tier>_backpack.png   32x32 model textures
#   assets/pack/rp/models/entity/alleviatedbackpacks_backpack.geo.json      3D backpack model
#   assets/pack/rp/attachables/alleviatedbackpacks_<tier>_backpack.json     one attachable per tier
#   assets/pack/rp/render_controllers/alleviatedbackpacks_backpack.render_controllers.json
#
#   pwsh tools/textures/wearable.ps1 [-Preview]
#
# The model is a simple bag + lid + front pocket + clasp on the back (bone "body", which binds to
# the wearer's body bone by name like vanilla armor) and shoulder straps on the front. In the hand
# the item keeps its normal icon. The attachable's script asks the owner whether this item is in
# the chest slot (q.is_item_name_any; c.item_slot is not usable there); the render controller hides the model otherwise.
param([switch]$Preview)

. "$PSScriptRoot/lib.ps1"

$root = (Resolve-Path "$PSScriptRoot/../..").Path
$rp = Join-Path $root 'assets/pack/rp'

# ---- texture --------------------------------------------------------------------------------
$W = 32; $H = 32

function New-Grid { , (1..$H | ForEach-Object { , ([char[]]('.' * $W)) }) }
function Fill($g, [int]$x, [int]$y, [int]$w, [int]$h, [char]$c) {
    for ($j = $y; $j -lt $y + $h; $j++) { for ($i = $x; $i -lt $x + $w; $i++) { $g[$j][$i] = $c } }
}
function Put($g, [int]$x, [int]$y, [char]$c) { $g[$y][$x] = $c }

function Draw-Backpack {
    $g = New-Grid
    # (0,0) 8x9 bag face: fabric, light left edge, dark right edge/bottom, trim band + stitches
    Fill $g 0 0 8 9 'M'
    Fill $g 0 0 1 9 'H'
    Fill $g 7 0 1 9 'S'
    Fill $g 0 8 8 1 'S'
    Fill $g 0 0 8 1 'T'
    foreach ($x in 1, 3, 5) { Put $g $x 4 'S' }
    Put $g 2 6 'H'; Put $g 5 2 'H'
    # (8,0) 4x9 bag side
    Fill $g 8 0 4 9 'M'
    Fill $g 8 0 4 1 'T'
    Fill $g 11 1 1 8 'S'
    Fill $g 8 8 4 1 'S'
    Put $g 9 4 'S'
    # (12,0) 8x4 bag top/bottom
    Fill $g 12 0 8 4 'S'
    Fill $g 12 0 8 1 'M'
    # (0,9) 9x2 lid front, (9,9) 5x2 lid side
    Fill $g 0 9 9 2 'T'
    Fill $g 0 10 9 1 'd'
    Fill $g 9 9 5 2 'T'
    Fill $g 9 10 5 1 'd'
    # (0,11) 9x5 lid top
    Fill $g 0 11 9 5 'T'
    Fill $g 0 11 9 1 'L'
    Fill $g 0 15 9 1 'd'
    Put $g 2 13 'L'; Put $g 6 12 'L'
    # (14,9) 6x4 pocket front, (20,9) 1x4 pocket side, (14,13) 6x1 pocket top
    Fill $g 14 9 6 4 'M'
    Fill $g 14 9 6 1 'T'
    Fill $g 14 12 6 1 'S'
    Fill $g 14 10 1 2 'H'
    Fill $g 20 9 1 4 'S'
    Fill $g 14 13 6 1 'T'
    # (20,4) 2x2 clasp
    Fill $g 20 4 2 2 'B'
    Put $g 21 5 'd'
    # (24,0) 2x12 strap with a buckle
    Fill $g 24 0 2 12 'K'
    Fill $g 25 0 1 12 'q'
    Fill $g 24 7 2 1 'B'
    , ($g | ForEach-Object { -join $_ })
}

$tiers = [ordered]@{
    leather   = @{ body = '#9c6b3c'; trim = '#6e4523'; clasp = '#c8a060'; strap = '#4a2f18' }
    copper    = @{ body = '#9c6b3c'; trim = '#c26a3e'; clasp = '#e8a27a'; strap = '#4a2f18' }
    iron      = @{ body = '#9c6b3c'; trim = '#c9c9c9'; clasp = '#f2f2f2'; strap = '#4a2f18' }
    gold      = @{ body = '#9c6b3c'; trim = '#f2c43b'; clasp = '#fff08a'; strap = '#4a2f18' }
    diamond   = @{ body = '#9c6b3c'; trim = '#4fd6cf'; clasp = '#b5f7f2'; strap = '#4a2f18' }
    netherite = @{ body = '#4a3f45'; trim = '#2b2427'; clasp = '#8c7a84'; strap = '#211b1e' }
}

$rows = Draw-Backpack
$texDir = Join-Path $rp 'textures/entity/alleviatedbackpacks'
foreach ($name in $tiers.Keys) {
    $t = $tiers[$name]
    $pal = @{
        M = $t.body
        H = Shade-Color $t.body 1.18
        S = Shade-Color $t.body 0.72
        T = $t.trim
        L = Shade-Color $t.trim 1.25
        d = Shade-Color $t.trim 0.7
        B = $t.clasp
        K = $t.strap
        q = Shade-Color $t.strap 0.75
    }
    Save-PixelArt -Rows $rows -Palette $pal -Path (Join-Path $texDir "${name}_backpack.png")
    if ($Preview) { Save-PixelArt -Rows $rows -Palette $pal -Scale 12 -Path (Join-Path $PSScriptRoot "preview/worn_${name}_backpack.png") }
}

# ---- geometry -------------------------------------------------------------------------------
function Face([double]$u, [double]$v, [double]$w, [double]$h) { [ordered]@{ uv = @($u, $v); uv_size = @($w, $h) } }
function Uv6($ns, $ew, $ud) {
    [ordered]@{ north = $ns; south = $ns; east = $ew; west = $ew; up = $ud; down = $ud }
}
# A cube from [x,y,z] with size [w,h,d], scaled around $center by $s and moved by $offset.
function Cube($origin, $size, $uv, [double]$s = 1, $center = @(0, 0, 0), $offset = @(0, 0, 0)) {
    $o = @(0, 1, 2 | ForEach-Object { [Math]::Round(($origin[$_] - $center[$_]) * $s + $center[$_] + $offset[$_], 4) })
    $z = @(0, 1, 2 | ForEach-Object { [Math]::Round($size[$_] * $s, 4) })
    [ordered]@{ origin = $o; size = $z; uv = $uv }
}

$bagUv    = Uv6 (Face 0 0 8 9) (Face 8 0 4 9) (Face 12 0 8 4)
$lidUv    = Uv6 (Face 0 9 9 2) (Face 9 9 5 2) (Face 0 11 9 5)
$pocketUv = Uv6 (Face 14 9 6 4) (Face 20 9 1 4) (Face 14 13 6 1)
$claspUv  = Uv6 (Face 20 4 2 2) (Face 20 4 2 2) (Face 20 4 2 2)
$strapUv  = Uv6 (Face 24 0 2 12) (Face 24 0 2 12) (Face 24 0 2 4)

# Bag parts in player model space (body: x -4..4, y 12..24, z -2..2; the back is +z).
$parts = @(
    @{ o = @(-4, 12.5, 2);   s = @(8, 9, 4);   uv = $bagUv },
    @{ o = @(-4.5, 21, 1.5); s = @(9, 2, 5);   uv = $lidUv },
    @{ o = @(-3, 13.5, 6);   s = @(6, 4, 1);   uv = $pocketUv },
    @{ o = @(-1, 19.5, 6);   s = @(2, 2, 1);   uv = $claspUv }
)
$straps = @(
    @{ o = @(-3, 12.5, -2.5); s = @(1.5, 11.5, 0.5); uv = $strapUv },
    @{ o = @(1.5, 12.5, -2.5); s = @(1.5, 11.5, 0.5); uv = $strapUv },
    @{ o = @(-3, 24, -2.5);   s = @(1.5, 0.5, 4.5); uv = $strapUv },
    @{ o = @(1.5, 24, -2.5);  s = @(1.5, 0.5, 4.5); uv = $strapUv }
)

$worn = @($parts + $straps | ForEach-Object { Cube $_.o $_.s $_.uv })

$geo = [ordered]@{
    format_version      = '1.16.0'
    'minecraft:geometry' = @(
        [ordered]@{
            description = [ordered]@{
                identifier            = 'geometry.alleviatedbackpacks.backpack'
                texture_width         = $W
                texture_height        = $H
                visible_bounds_width  = 3
                visible_bounds_height = 3
                visible_bounds_offset = @(0, 1.5, 0)
            }
            bones       = @(
                [ordered]@{ name = 'body'; pivot = @(0, 24, 0); cubes = $worn }
            )
        }
    )
}
$geoPath = Join-Path $rp 'models/entity/alleviatedbackpacks_backpack.geo.json'
New-Item -ItemType Directory -Force (Split-Path $geoPath) | Out-Null
$geo | ConvertTo-Json -Depth 20 | Set-Content -Path $geoPath -Encoding utf8NoBOM

# ---- render controller + attachables ----------------------------------------------------------
$rc = [ordered]@{
    format_version     = '1.8.0'
    render_controllers = [ordered]@{
        'controller.render.alleviatedbackpacks.backpack' = [ordered]@{
            geometry        = 'Geometry.default'
            materials       = @(@{ '*' = 'Material.default' })
            textures        = @('Texture.default')
            part_visibility = @(
                @{ body = 'v.is_worn' }
            )
        }
    }
}
$rcPath = Join-Path $rp 'render_controllers/alleviatedbackpacks_backpack.render_controllers.json'
New-Item -ItemType Directory -Force (Split-Path $rcPath) | Out-Null
$rc | ConvertTo-Json -Depth 20 | Set-Content -Path $rcPath -Encoding utf8NoBOM

$attDir = Join-Path $rp 'attachables'
New-Item -ItemType Directory -Force $attDir | Out-Null
foreach ($name in $tiers.Keys) {
    $att = [ordered]@{
        format_version          = '1.10.0'
        'minecraft:attachable' = [ordered]@{
            description = [ordered]@{
                identifier         = "alleviatedbackpacks:${name}_backpack"
                materials          = [ordered]@{ default = 'entity_alphatest' }
                textures           = [ordered]@{ default = "textures/entity/alleviatedbackpacks/${name}_backpack" }
                geometry           = [ordered]@{ default = 'geometry.alleviatedbackpacks.backpack' }
                scripts            = [ordered]@{ pre_animation = @("v.is_worn = q.is_item_name_any('slot.armor.chest', 0, 'alleviatedbackpacks:${name}_backpack');") }
                render_controllers = @('controller.render.alleviatedbackpacks.backpack')
            }
        }
    }
    $att | ConvertTo-Json -Depth 20 | Set-Content -Path (Join-Path $attDir "alleviatedbackpacks_${name}_backpack.json") -Encoding utf8NoBOM
}
Write-Host "wrote worn-backpack textures, model, render controller and $($tiers.Count) attachables"
