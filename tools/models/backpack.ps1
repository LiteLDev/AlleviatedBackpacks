# Generates the backpack model (Bedrock geometry), its original textures and the 3D GUI icons.
#
#   pwsh tools/models/backpack.ps1
#
# Shape: Sophisticated Backpacks' part system with SB's element boxes (backpack_parts.ps1).
# Everything else (UV layout, pixel art, icon rendering) is ours: textures are painted
# procedurally per material, icons are rendered from the same geometry with SB's "gui" display
# transform (rotation 30/225/0, translation 0/1.25/0, scale 0.9) by a small rasterizer (Raster.cs).
#
# Outputs (assets/pack/rp/):
#   models/entity/alleviatedbackpacks_backpack.geo.json   geometry.alleviatedbackpacks.backpack
#   textures/entity/alleviatedbackpacks/backpack/*.png     solid, cloth, border, fixed_<tier>, glass, fluid_*
#   textures/items/<tier>_backpack.png                     default 3D icon per tier (minecraft:icon)
#   textures/ui/alleviatedbackpacks/icons/*.png            icon layers for per-instance tinting
#   (and tools/models/out/bones.json: bone list for the render controller generator)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/backpack_parts.ps1"
. "$PSScriptRoot/../textures/lib.ps1"
Add-Type -Path "$PSScriptRoot/Raster.cs" -ErrorAction SilentlyContinue

$root  = (Resolve-Path "$PSScriptRoot/../..").Path
$rp    = Join-Path $root 'assets/pack/rp'
$texDir = Join-Path $rp 'textures/entity/alleviatedbackpacks/backpack'
$iconDir = Join-Path $rp 'textures/ui/alleviatedbackpacks/icons'
New-Item -ItemType Directory -Force $texDir, $iconDir, (Join-Path $rp 'animations'), (Join-Path $rp 'render_controllers'), (Join-Path $rp 'attachables') | Out-Null

$tiers = 'leather', 'copper', 'iron', 'gold', 'diamond', 'netherite'

# ---- boxes ---------------------------------------------------------------------------------------
# Material -> texture group of the Bedrock passes.
function GroupOf([string]$mat) {
    switch ($mat) { 'cloth' { 'cloth' } 'border' { 'border' } 'glass' { 'glass' } default { 'fixed' } }
}

$boxes = [System.Collections.Generic.List[object]]::new()
foreach ($part in $BackpackParts.Keys) {
    foreach ($b in $BackpackParts[$part]) {
        $boxes.Add([pscustomobject]@{ part = $part; name = $b.n; from = $b.f; to = $b.t; mat = $b.m; faces = $b.x; bone = "$($part)_$(GroupOf $b.m)" })
    }
}
# Battery charge: 4 slices growing from high x (SB fills from the low-U end of the north face).
$c = $BatteryCharge
for ($k = 1; $k -le $c.steps; $k++) {
    $w = ($c.t[0] - $c.f[0]) * $k / $c.steps
    $boxes.Add([pscustomobject]@{ part = 'charge'; name = "charge_$k"; from = @(($c.t[0] - $w), $c.f[1], $c.f[2]); to = $c.t; mat = 'charge'; faces = 'n'; bone = "charge_$k" })
}
# Tank fluids: 10 levels per side, normal (rising) and lighter-than-air (hanging from the top).
foreach ($side in 'left', 'right') {
    $fl = $TankFluid[$side]
    $h = $fl.t[1] - $fl.f[1]
    for ($k = 1; $k -le $fl.steps; $k++) {
        $hh = $h * $k / $fl.steps
        $top = if ($k -lt $fl.steps) { 'u' } else { '' }
        $bot = if ($k -lt $fl.steps) { 'd' } else { '' }
        $s = $side.Substring(0, 1)
        $boxes.Add([pscustomobject]@{ part = "fluid_$s"; name = "fluid_$($s)_$k"; from = $fl.f; to = @($fl.t[0], ($fl.f[1] + $hh), $fl.t[2]); mat = 'fluid'; faces = ($fl.x + $top); bone = "fluid_$($s)_$k" })
        $boxes.Add([pscustomobject]@{ part = "fluid_$s"; name = "fluid_$($s)_${k}_up"; from = @($fl.f[0], ($fl.t[1] - $hh), $fl.f[2]); to = $fl.t; mat = 'fluid'; faces = ($fl.x + $bot); bone = "fluid_$($s)_$($k)_up" })
    }
}

# ---- UV atlas ------------------------------------------------------------------------------------
# Face size in texels (1 texel per model pixel, at least 1).
function FaceDims($b, [char]$f) {
    $sx = $b.to[0] - $b.from[0]; $sy = $b.to[1] - $b.from[1]; $sz = $b.to[2] - $b.from[2]
    switch ($f) {
        'n' { , @($sx, $sy) } 's' { , @($sx, $sy) }
        'e' { , @($sz, $sy) } 'w' { , @($sz, $sy) }
        'u' { , @($sx, $sz) } 'd' { , @($sx, $sz) }
    }
}
$faces = [System.Collections.Generic.List[object]]::new()
foreach ($b in $boxes) {
    foreach ($f in $b.faces.ToCharArray()) {
        $d = FaceDims $b $f
        if ($d[0] -le 0 -or $d[1] -le 0) { continue }
        $faces.Add([pscustomobject]@{ box = $b; dir = $f; w = $d[0]; h = $d[1]; pw = [Math]::Max(1, [int][Math]::Ceiling($d[0] - 1e-6)); ph = [Math]::Max(1, [int][Math]::Ceiling($d[1] - 1e-6)); u = 0; v = 0 })
    }
}
$W = 128; $H = 128
$x = 0; $y = 0; $row = 0
foreach ($f in ($faces | Sort-Object -Property @{ e = 'ph'; Descending = $true }, @{ e = 'pw'; Descending = $true })) {
    if ($x + $f.pw -gt $W) { $x = 0; $y += $row; $row = 0 }
    $f.u = $x; $f.v = $y
    $x += $f.pw
    $row = [Math]::Max($row, $f.ph)
}
if ($y + $row -gt $H) { throw "atlas overflow ($($y + $row) > $H)" }
Write-Host "atlas: $($faces.Count) faces, used height $($y + $row)"

# ---- texture painting ----------------------------------------------------------------------------
function New-Buf { , (New-Object 'uint32[]' ($W * $H)) }
function Argb([int]$a, [int]$r, [int]$g, [int]$b) { [uint32](([uint32]$a -shl 24) -bor ([uint32]$r -shl 16) -bor ([uint32]$g -shl 8) -bor [uint32]$b) }
function Gray([double]$t, [int]$a = 255) { $v = [int][Math]::Round([Math]::Max(0, [Math]::Min(1, $t)) * 255); Argb $a $v $v $v }
function FromColor($col) { $c = ConvertTo-Color $col; Argb $c.A $c.R $c.G $c.B }
function Shade($col, [double]$k) { FromColor (Shade-Color $col $k) }
# Deterministic per-texel noise in [0,1).
function Noise([int]$x, [int]$y, [int]$s) { $h = ($x * 374761393 + $y * 668265263 + $s * 2147483647) -band 0x7fffffff; $h = (($h -bxor ($h -shr 13)) * 1274126177) -band 0x7fffffff; ($h % 1000) / 1000.0 }

# tint: red = cloth intensity (main colour), green = border intensity (accent colour); drawn with
# entity_alphatest_multicolor_tint whose change colours the client mod sets per backpack.
$buf = @{ tint = New-Buf; glass = New-Buf }
foreach ($t in $tiers) { $buf["fixed_$t"] = New-Buf }
# Fluid textures, one per FluidLook (core/render/BackpackModel.h), colour baked in.
$fluidLooks = [ordered]@{
    water = @{ base = '#3f76e4'; hi = '#6d9bf0' }; lava = @{ base = '#d4561a'; hi = '#ffb43a' }
    milk = @{ base = '#ececec'; hi = '#ffffff' }; powder_snow = @{ base = '#e3eef9'; hi = '#ffffff' }
    honey = @{ base = '#e8961c'; hi = '#ffc451' }; experience = @{ base = '#6fcf2c'; hi = '#c6ff6a' }
    potion = @{ base = '#b13fb8'; hi = '#e07ae6' }
}
foreach ($k in $fluidLooks.Keys) { $buf["fluid_$k"] = New-Buf }
# Gray value maps (icons): cloth/border/fluid as 0..1 (stored as gray pixels).
$gray = @{ cloth = New-Buf; border = New-Buf; fluid = New-Buf }

$clipPalettes = @{
    leather   = @{ base = '#6b4a2b'; hi = '#8a6440'; lo = '#4a321d'; metal = '#a8a8a8' }
    copper    = @{ base = '#c46a3c'; hi = '#e7955f'; lo = '#8e4628'; metal = '#d98b5a' }
    iron      = @{ base = '#c8c8c8'; hi = '#ececec'; lo = '#8e8e8e'; metal = '#d8d8d8' }
    gold      = @{ base = '#e8b923'; hi = '#fbe36a'; lo = '#b0851a'; metal = '#f5d24a' }
    diamond   = @{ base = '#3fd6cf'; hi = '#a2f4ee'; lo = '#1f9a95'; metal = '#66e4dc' }
    netherite = @{ base = '#4a4247'; hi = '#6b6268'; lo = '#2e292c'; metal = '#5d5559' }
}

function Put($b, [int]$px, [int]$py, [uint32]$v) { $b[$py * $W + $px] = $v }

foreach ($f in $faces) {
    $b = $f.box
    for ($j = 0; $j -lt $f.ph; $j++) {
        for ($i = 0; $i -lt $f.pw; $i++) {
            $px = $f.u + $i; $py = $f.v + $j
            $edge = ($i -eq 0 -or $j -eq 0 -or $i -eq $f.pw - 1 -or $j -eq $f.ph - 1) -and $f.pw -gt 2 -and $f.ph -gt 2
            $n = Noise $px $py 1
            switch ($b.mat) {
                'cloth' {
                    # canvas weave: alternating light/dark texels, seams darker
                    $t = 0.9 + 0.06 * ((($px + $py) % 2) * 2 - 1) + 0.04 * ($n - 0.5)
                    if ($edge) { $t -= 0.12 }
                    Put $buf.tint $px $py (Argb 255 ([int][Math]::Round($t * 255)) 0 0)
                    Put $gray.cloth $px $py (Gray $t)
                }
                'border' {
                    # stitched strap: light stitch every other texel along the middle
                    $t = 0.86 + 0.05 * ($n - 0.5)
                    if (($i + $j) % 3 -eq 0) { $t += 0.06 }
                    if ($edge) { $t -= 0.14 }
                    Put $buf.tint $px $py (Argb 255 0 ([int][Math]::Round($t * 255)) 0)
                    Put $gray.border $px $py (Gray $t)
                }
                'glass' {
                    $a = if (($i - $j) % 5 -eq 0) { 120 } else { 60 }
                    Put $buf.glass $px $py (Argb $a 220 236 245)
                }
                'fluid' {
                    # gentle diagonal ripples (lava: bright blobs)
                    $tw = 0.86 + 0.08 * [Math]::Sin(($px * 0.9 + $py * 1.7)) + 0.06 * ($n - 0.5)
                    Put $gray.fluid $px $py (Gray $tw)
                    foreach ($k in $fluidLooks.Keys) {
                        $look = $fluidLooks[$k]
                        $col = if ($k -eq 'lava') {
                            if ((Noise ([int]($px / 2)) ([int]($py / 2)) 7) -gt 0.62) { FromColor $look.hi } else { Shade $look.base (0.92 + 0.16 * $n) }
                        } elseif ($tw -gt 0.92) { FromColor $look.hi } else { Shade $look.base $tw }
                        Put $buf["fluid_$k"] $px $py $col
                    }
                }
                default {
                    foreach ($tier in $tiers) {
                        $p = $clipPalettes[$tier]
                        $col = switch ($b.mat) {
                            'clips' {
                                if ($b.name -match 'clip') { if ($edge) { Shade $p.metal 0.8 } elseif ($n -gt 0.7) { FromColor $p.hi } else { FromColor $p.metal } }
                                elseif ($edge) { FromColor $p.lo } elseif ($n -gt 0.8) { FromColor $p.hi } else { FromColor $p.base }
                            }
                            'plain' { $t = 0.62 + 0.06 * ($n - 0.5); if (($i + $j) % 3 -eq 0) { $t += 0.05 }; Gray $t }
                            'charge' { if ($n -gt 0.5) { FromColor '#5cff6a' } else { FromColor '#2fd241' } }
                            'modules' {
                                switch -Regex ($b.name) {
                                    '^black_wire' { Shade '#262626' (0.9 + 0.2 * $n) }
                                    '^red_wire' { Shade '#b3241f' (0.9 + 0.2 * $n) }
                                    '^connection' { if ($edge) { FromColor '#8c7a3c' } else { FromColor '#c9ad52' } }
                                    '^battery' { if ($edge) { FromColor '#3a3d42' } elseif ($j -eq 1 -and $b.from[2] -lt 3) { FromColor '#1d1f22' } else { Shade '#565a61' (0.92 + 0.15 * $n) } }
                                    '^back' { Shade '#3b3f45' (0.9 + 0.2 * $n) }
                                    default { if ($edge) { FromColor '#6c7076' } else { Shade '#a3a8ae' (0.92 + 0.15 * $n) } }
                                }
                            }
                        }
                        Put $buf["fixed_$tier"] $px $py $col
                    }
                }
            }
        }
    }
}

function Save-Buf($b, [string]$path, [int]$w = $W, [int]$h = $H) {
    $bmp = New-Object System.Drawing.Bitmap $w, $h, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    try {
        $ints = New-Object 'int32[]' ($w * $h)
        [Buffer]::BlockCopy([uint32[]]$b, 0, $ints, 0, $w * $h * 4)
        $data = $bmp.LockBits((New-Object System.Drawing.Rectangle 0, 0, $w, $h), [System.Drawing.Imaging.ImageLockMode]::WriteOnly, $bmp.PixelFormat)
        [System.Runtime.InteropServices.Marshal]::Copy($ints, 0, $data.Scan0, $w * $h)
        $bmp.UnlockBits($data)
        $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    } finally { $bmp.Dispose() }
}
foreach ($k in $buf.Keys) { if ($k -notlike 'fluid_*') { Save-Buf $buf[$k] (Join-Path $texDir "$k.png") } }
foreach ($k in $fluidLooks.Keys) { Remove-Item -ErrorAction SilentlyContinue (Join-Path $texDir "fluid_$k.png") }

# Tank fluids (GX-1): SB textures the tank fluid with the fluid's own still sprite (animated, tinted
# like the fluid type), so the fluid parts have their own geometry in 16x16 sprite UV space
# (geometry.alleviatedbackpacks.backpack_fluid) and their render controllers pick the texture:
# water, milk and potions the game's textures/blocks/water_still_grey (32 frames), lava
# textures/blocks/lava_still (20 frames) - flipbook frames chosen with uv_anim, tinted through the
# controller's colour - and for fluids without a vanilla still texture our own 16x16 sprites.
$fluidSprites = [ordered]@{
    water       = @{ path = 'textures/blocks/water_still'; frames = 32; ticks = 2 }
    lava        = @{ path = 'textures/blocks/lava_still'; frames = 20; ticks = 16 }
    milk        = @{ path = 'textures/blocks/water_still_grey'; frames = 32; ticks = 2 }
    powder_snow = @{ path = 'textures/entity/alleviatedbackpacks/backpack/fluid_powder_snow'; frames = 1; ticks = 1 }
    honey       = @{ path = 'textures/entity/alleviatedbackpacks/backpack/fluid_honey'; frames = 1; ticks = 1 }
    experience  = @{ path = 'textures/entity/alleviatedbackpacks/backpack/fluid_experience'; frames = 1; ticks = 1 }
    potion      = @{ path = 'textures/blocks/water_still_grey'; frames = 32; ticks = 2 }
}
foreach ($k in 'powder_snow', 'honey', 'experience') {
    $look = $fluidLooks[$k]
    $sb   = New-Object 'uint32[]' (16 * 16)
    for ($py = 0; $py -lt 16; $py++) {
        for ($px = 0; $px -lt 16; $px++) {
            $n  = Noise $px $py 1
            $tw = 0.86 + 0.08 * [Math]::Sin(($px * 0.9 + $py * 1.7)) + 0.06 * ($n - 0.5)
            $sb[$py * 16 + $px] = if ($tw -gt 0.92) { FromColor $look.hi } else { Shade $look.base $tw }
        }
    }
    Save-Buf $sb (Join-Path $texDir "fluid_$k.png") 16 16
}

# ---- geometry ------------------------------------------------------------------------------------
# Model frame "P" (placed backpack): x = 8 - sbX (SB "left" = high x = the backpack's own right
# when it faces you), y = sbY, z = sbZ - 8 (front = -z). Bone xf carries the per-context transform.
$dirName = @{ n = 'north'; s = 'south'; e = 'west'; w = 'east'; u = 'up'; d = 'down' } # x is mirrored
$bones = [ordered]@{}
foreach ($b in $boxes) { if (-not $bones.Contains($b.bone)) { $bones[$b.bone] = [System.Collections.Generic.List[object]]::new() } }
foreach ($b in $boxes) {
    $uv = [ordered]@{}
    foreach ($f in ($faces | Where-Object { $_.box -eq $b })) {
        $uv[$dirName["$($f.dir)"]] = [ordered]@{ uv = @($f.u, $f.v); uv_size = @([Math]::Round($f.w, 4), [Math]::Round($f.h, 4)) }
    }
    if ($uv.Count -eq 0) { continue }
    $size = @(($b.to[0] - $b.from[0]), ($b.to[1] - $b.from[1]), ($b.to[2] - $b.from[2]))
    $origin = @((8 - $b.to[0]), $b.from[1], ($b.from[2] - 8))
    $bones[$b.bone].Add([ordered]@{ origin = @($origin | ForEach-Object { [Math]::Round($_, 4) }); size = @($size | ForEach-Object { [Math]::Round($_, 4) }); uv = $uv })
}
$geoBones = [System.Collections.Generic.List[object]]::new()
$geoBones.Add([ordered]@{ name = 'root'; binding = 'q.item_slot_to_bone_name(c.item_slot)'; pivot = @(0, 24, 0) })
$geoBones.Add([ordered]@{ name = 'xf'; parent = 'root'; pivot = @(0, 0, 0) })
foreach ($name in $bones.Keys) {
    if ($name -like 'fluid_*') { continue }
    $geoBones.Add([ordered]@{ name = $name; parent = 'xf'; pivot = @(0, 0, 0); cubes = $bones[$name] })
}
# Display item anchor: a locator at the centre of SB's anchor quad.
$a = $DisplayAnchor
$geoBones.Add([ordered]@{ name = 'display'; parent = 'xf'; pivot = @((8 - ($a.f[0] + $a.t[0]) / 2), (($a.f[1] + $a.t[1]) / 2), ($a.f[2] - 8)) })
$geo = [ordered]@{
    format_version      = '1.16.0'
    'minecraft:geometry' = @(
        [ordered]@{
            description = [ordered]@{ identifier = 'geometry.alleviatedbackpacks.backpack'; texture_width = $W; texture_height = $H; visible_bounds_width = 3; visible_bounds_height = 3; visible_bounds_offset = @(0, 1.5, 0) }
            bones       = $geoBones
        }
    )
}
$geoPath = Join-Path $rp 'models/entity/alleviatedbackpacks_backpack.geo.json'
$geo | ConvertTo-Json -Depth 20 -Compress | Set-Content -NoNewline -Encoding utf8 $geoPath
# Fluid geometry: the same root / xf bones (bound and animated like the main geometry) and the
# fluid boxes, every face mapped onto the top-left of one 16x16 sprite frame.
$fluidBones = [System.Collections.Generic.List[object]]::new()
$fluidBones.Add([ordered]@{ name = 'root'; binding = 'q.item_slot_to_bone_name(c.item_slot)'; pivot = @(0, 24, 0) })
$fluidBones.Add([ordered]@{ name = 'xf'; parent = 'root'; pivot = @(0, 0, 0) })
foreach ($b in ($boxes | Where-Object { $_.mat -eq 'fluid' })) {
    $uv = [ordered]@{}
    foreach ($f in ($faces | Where-Object { $_.box -eq $b })) {
        $uv[$dirName["$($f.dir)"]] = [ordered]@{ uv = @(0, 0); uv_size = @([Math]::Round([Math]::Min(16.0, [double]$f.w), 4), [Math]::Round([Math]::Min(16.0, [double]$f.h), 4)) }
    }
    $size   = @(($b.to[0] - $b.from[0]), ($b.to[1] - $b.from[1]), ($b.to[2] - $b.from[2]))
    $origin = @((8 - $b.to[0]), $b.from[1], ($b.from[2] - 8))
    $fluidBones.Add([ordered]@{ name = $b.bone; parent = 'xf'; pivot = @(0, 0, 0); cubes = @([ordered]@{ origin = @($origin | ForEach-Object { [Math]::Round($_, 4) }); size = @($size | ForEach-Object { [Math]::Round($_, 4) }); uv = $uv }) })
}
[ordered]@{
    format_version       = '1.16.0'
    'minecraft:geometry' = @(
        [ordered]@{
            description = [ordered]@{ identifier = 'geometry.alleviatedbackpacks.backpack_fluid'; texture_width = 16; texture_height = 16; visible_bounds_width = 3; visible_bounds_height = 3; visible_bounds_offset = @(0, 1.5, 0) }
            bones       = $fluidBones
        }
    )
} | ConvertTo-Json -Depth 20 -Compress | Set-Content -NoNewline -Encoding utf8 (Join-Path $rp 'models/entity/alleviatedbackpacks_backpack_fluid.geo.json')
New-Item -ItemType Directory -Force "$PSScriptRoot/out" | Out-Null
$bones.Keys | ConvertTo-Json | Set-Content -Encoding utf8 "$PSScriptRoot/out/bones.json"
Write-Host "geometry: $($bones.Count) bones -> $geoPath"

# ---- GUI icons -----------------------------------------------------------------------------------
$N = 64
$ras = [AbpModels.Raster]::new($N)
$ras.SetRotation(30, 225, 0)
$ras.S = 0.9
$ras.Ty = 1.25 / 16
$samplers = @{}
foreach ($k in 'cloth', 'border', 'fluid') { $samplers[$k] = $ras.AddTexture($gray[$k], $W, $H) }
foreach ($t in $tiers) { $samplers["fixed_$t"] = $ras.AddTexture($buf["fixed_$t"], $W, $H) }
$samplers.glass = $ras.AddTexture($buf.glass, $W, $H)

# Face corners in SB block space + UV corners.
function Quads-For($b, [int]$layer, [int]$sampler, [bool]$occluder) {
    $f0 = $b.from; $t0 = $b.to
    foreach ($f in ($faces | Where-Object { $_.box -eq $b })) {
        $q = [AbpModels.Quad]::new()
        $x0 = $f0[0]; $y0 = $f0[1]; $z0 = $f0[2]; $x1 = $t0[0]; $y1 = $t0[1]; $z1 = $t0[2]
        # counter-clockwise seen from outside (Java coordinates, y up, z towards south)
        $pts = switch ("$($f.dir)") {
            'n' { @(@($x1, $y1, $z0), @($x0, $y1, $z0), @($x0, $y0, $z0), @($x1, $y0, $z0)) }
            's' { @(@($x0, $y1, $z1), @($x1, $y1, $z1), @($x1, $y0, $z1), @($x0, $y0, $z1)) }
            'e' { @(@($x1, $y1, $z1), @($x1, $y1, $z0), @($x1, $y0, $z0), @($x1, $y0, $z1)) }
            'w' { @(@($x0, $y1, $z0), @($x0, $y1, $z1), @($x0, $y0, $z1), @($x0, $y0, $z0)) }
            'u' { @(@($x0, $y1, $z0), @($x1, $y1, $z0), @($x1, $y1, $z1), @($x0, $y1, $z1)) }
            'd' { @(@($x0, $y0, $z1), @($x1, $y0, $z1), @($x1, $y0, $z0), @($x0, $y0, $z0)) }
        }
        $uvs = @(@($f.u, $f.v), @(($f.u + $f.w), $f.v), @(($f.u + $f.w), ($f.v + $f.h)), @($f.u, ($f.v + $f.h)))
        for ($i = 0; $i -lt 4; $i++) {
            $q.X[$i] = $pts[$i][0]; $q.Y[$i] = $pts[$i][1]; $q.Z[$i] = $pts[$i][2]
            $q.U[$i] = $uvs[$i][0]; $q.V[$i] = $uvs[$i][1]
        }
        $q.Layer = $layer; $q.Sampler = $sampler; $q.Occluder = $occluder
        $ras.Draw($q)
    }
}

$layerId = @{ cloth = 0; border = 1; fixed = 2; fluid = 3; charge = 4 }
# Renders one configuration; returns per-layer buffers (cloth/border gray, fixed rgba, glass, fluid mask).
function Render-Config([bool]$battery, [bool]$ltank, [bool]$rtank, [string]$tier, [int]$lfill, [int]$rfill, [int]$charge) {
    $ras.Clear()
    $parts = @('base', $(if ($battery) { 'battery' } else { 'front_pouch' }), $(if ($ltank) { 'left_tank' } else { 'left_pouch' }), $(if ($rtank) { 'right_tank' } else { 'right_pouch' }), 'straps')
    foreach ($b in $boxes) {
        $draw = $false; $layer = 0; $sampler = 0; $occ = $true
        if ($parts -contains $b.part) {
            $draw = $true
            switch ($b.mat) {
                'cloth' { $layer = 0; $sampler = $samplers.cloth }
                'border' { $layer = 1; $sampler = $samplers.border }
                'glass' { $layer = 5; $sampler = $samplers.glass; $occ = $false }
                default { $layer = 2; $sampler = $samplers["fixed_$tier"] }
            }
        } elseif ($b.part -eq 'charge' -and $battery -and $b.name -eq "charge_$charge") {
            $draw = $true; $layer = 4; $sampler = $samplers["fixed_$tier"]
        } elseif ($b.part -eq 'fluid_l' -and $ltank -and $b.name -eq "fluid_l_$lfill") {
            $draw = $true; $layer = 3; $sampler = $samplers.fluid
        } elseif ($b.part -eq 'fluid_r' -and $rtank -and $b.name -eq "fluid_r_$rfill") {
            $draw = $true; $layer = 3; $sampler = $samplers.fluid
        }
        if ($draw -and $occ) { Quads-For $b $layer $sampler $true }
    }
    foreach ($b in $boxes) { if ($b.mat -eq 'glass' -and $parts -contains $b.part) { Quads-For $b 5 $samplers.glass $false } }
}

function Save-Layer([int]$layer, [string]$path, [switch]$Over, [switch]$Always) {
    $out = New-Object 'uint32[]' ($N * $N)
    $any = $false
    for ($i = 0; $i -lt $out.Length; $i++) {
        if ($Over) { if ($ras.Over[$i] -ne 0) { $out[$i] = $ras.Over[$i]; $any = $true } }
        elseif ($ras.Owner[$i] -eq $layer) { $out[$i] = $ras.Color[$i]; $any = $true }
    }
    if ($any -or $Always) { Save-Buf $out $path $N $N }
    return $any
}

$mainDefault = '#cc613a'; $accentDefault = '#622e1a'
function Tint([uint32]$c, $col) {
    $k = ConvertTo-Color $col
    $a = ($c -shr 24) -band 255; $r = ($c -shr 16) -band 255; $g = ($c -shr 8) -band 255; $bb = $c -band 255
    Argb $a ([int]($r * $k.R / 255)) ([int]($g * $k.G / 255)) ([int]($bb * $k.B / 255))
}

# Layer sprites per part configuration: c<b><l><r> (battery, left tank, right tank).
$manifest = [ordered]@{ size = $N; configs = [ordered]@{} }
foreach ($bat in $false, $true) { foreach ($lt in $false, $true) { foreach ($rt in $false, $true) {
    $key = "c$([int]$bat)$([int]$lt)$([int]$rt)"
    $info = [ordered]@{ fluid = [ordered]@{}; charge = @() }
    Render-Config $bat $lt $rt 'leather' 0 0 0
    Save-Layer 0 (Join-Path $iconDir "$($key)_cloth.png") | Out-Null
    Save-Layer 1 (Join-Path $iconDir "$($key)_border.png") | Out-Null
    $info.glass = [bool](Save-Layer 0 (Join-Path $iconDir "$($key)_glass.png") -Over -Always)
    foreach ($t in $tiers) {
        Render-Config $bat $lt $rt $t 0 0 0
        Save-Layer 2 (Join-Path $iconDir "$($key)_fixed_$t.png") | Out-Null
    }
    if ($bat) {
        for ($k = 1; $k -le $BatteryCharge.steps; $k++) {
            Render-Config $bat $lt $rt 'leather' 0 0 $k
            if (Save-Layer 4 (Join-Path $iconDir "$($key)_charge_$k.png") -Always) { $info.charge += $k }
        }
    }
    foreach ($side in 'l', 'r') {
        if (($side -eq 'l' -and -not $lt) -or ($side -eq 'r' -and -not $rt)) { continue }
        $levels = @()
        for ($k = 1; $k -le 10; $k++) {
            $lf = if ($side -eq 'l') { $k } else { 0 }; $rf = if ($side -eq 'r') { $k } else { 0 }
            Render-Config $bat $lt $rt 'leather' $lf $rf 0
            if (Save-Layer 3 (Join-Path $iconDir "$($key)_fluid_$($side)_$k.png") -Always) { $levels += $k }
        }
        $info.fluid[$side] = $levels
    }
    $manifest.configs[$key] = $info
} } }
$manifest | ConvertTo-Json -Depth 6 | Set-Content -Encoding utf8 "$PSScriptRoot/out/icons.json"

# Default item icons (pouches, default colours) per tier = composite of the layers.
foreach ($t in $tiers) {
    Render-Config $false $false $false $t 0 0 0
    $out = New-Object 'uint32[]' ($N * $N)
    for ($i = 0; $i -lt $out.Length; $i++) {
        switch ($ras.Owner[$i]) {
            0 { $out[$i] = Tint $ras.Color[$i] $mainDefault }
            1 { $out[$i] = Tint $ras.Color[$i] $accentDefault }
            2 { $out[$i] = $ras.Color[$i] }
        }
    }
    Save-Buf $out (Join-Path $rp "textures/items/$($t)_backpack.png") $N $N
}
Write-Host "icons: $($manifest.configs.Count) configurations"

# Vanilla "dyed" icon (the game multiplies it by the item's dye colour = the main colour): the
# default icon in grey.
foreach ($t in $tiers) {
    Render-Config $false $false $false $t 0 0 0
    $out = New-Object 'uint32[]' ($N * $N)
    for ($i = 0; $i -lt $out.Length; $i++) {
        $c = $ras.Color[$i]
        if ($ras.Owner[$i] -lt 0) { continue }
        $a = ($c -shr 24) -band 255; $r = ($c -shr 16) -band 255; $g = ($c -shr 8) -band 255; $bb = $c -band 255
        $l = [int][Math]::Min(255, [Math]::Round(0.3 * $r + 0.59 * $g + 0.11 * $bb))
        if ($ras.Owner[$i] -eq 2) { $out[$i] = $c } else { $out[$i] = Argb $a $l $l $l }
    }
    Save-Buf $out (Join-Path $rp "textures/items/$($t)_backpack_dyed.png") $N $N
}

# ---- attachables, render controllers, animations --------------------------------------------------
function Cond([string]$bone) {
    switch -Regex ($bone) {
        '^base_' { 'true' }
        '^front_pouch_' { '!q.abp_model(2)' }
        '^battery_' { 'q.abp_model(2)' }
        '^left_pouch_' { '!q.abp_model(3)' }
        '^left_tank_' { 'q.abp_model(3)' }
        '^right_pouch_' { '!q.abp_model(4)' }
        '^right_tank_' { 'q.abp_model(4)' }
        '^straps_' { 'v.abp_ctx != 1' }
        '^charge_(\d+)$' { "q.abp_model(2) && q.abp_model(5) == $($Matches[1])" }
        '^fluid_l_(\d+)_up$' { "q.abp_model(3) && q.abp_model(6) == $($Matches[1]) && q.abp_model(8)" }
        '^fluid_l_(\d+)$' { "q.abp_model(3) && q.abp_model(6) == $($Matches[1]) && !q.abp_model(8)" }
        '^fluid_r_(\d+)_up$' { "q.abp_model(4) && q.abp_model(7) == $($Matches[1]) && q.abp_model(9)" }
        '^fluid_r_(\d+)$' { "q.abp_model(4) && q.abp_model(7) == $($Matches[1]) && !q.abp_model(9)" }
        default { 'false' }
    }
}
function Visibility([scriptblock]$select) {
    $list = [System.Collections.Generic.List[object]]::new()
    $list.Add([ordered]@{ '*' = $false })
    $list.Add([ordered]@{ root = $true })
    $list.Add([ordered]@{ xf = $true })
    foreach ($bone in $bones.Keys) { if (& $select $bone) { $list.Add([ordered]@{ $bone = (Cond $bone) }) } }
    , $list
}
$prefix = 'controller.render.alleviatedbackpacks.backpack'
$looks = @($fluidLooks.Keys)
# A tank side's fluid controller: the look's texture (q.abp_model(lookField)) and its flipbook frame
# (uv_anim: frame = floor(ticks / ticksPerFrame) mod frames over a strip of square frames; the
# client's animation ticks are q.abp_model(15)). Attachables get their shader constants (change
# colours) once per attachable, not per render controller, so the sprites are drawn as they are:
# water with the game's blue water_still, lava, milk / potions on the grey water_still_grey.
function FluidController([int]$lookField, [string]$bonePattern) {
    $L      = "q.abp_model($lookField)"
    $frames = ''; $ticks = ''
    for ($i = 0; $i -lt $looks.Count; $i++) {
        $frames += "$L == $i ? $($fluidSprites[$looks[$i]].frames) : "
        $ticks  += "$L == $i ? $($fluidSprites[$looks[$i]].ticks) : "
    }
    $nf = "($($frames)1)"
    $tf = "($($ticks)1)"
    [ordered]@{
        arrays                     = @{ textures = @{ 'Array.fluid' = @($looks | ForEach-Object { "Texture.fluid_$_" }) } }
        geometry                   = 'Geometry.fluid'
        materials                  = @(@{ '*' = 'Material.fluid' })
        textures                   = @("Array.fluid[$L]")
        rebuild_animation_matrices = $true
        uv_anim                    = [ordered]@{
            offset = @(0, "math.mod(math.floor(q.abp_model(15) / $tf), $nf) / $nf")
            scale  = @(1, "1 / $nf")
        }
        part_visibility            = (Visibility { param($b) $b -match $bonePattern })
    }
}
$rcs = [ordered]@{
    "$prefix.tint"    = [ordered]@{ geometry = 'Geometry.default'; materials = @(@{ '*' = 'Material.tint' }); textures = @('Texture.tint'); part_visibility = (Visibility { param($b) $b -match '_(cloth|border)$' }) }
    "$prefix.fixed"   = [ordered]@{ geometry = 'Geometry.default'; materials = @(@{ '*' = 'Material.default' }); textures = @('Texture.fixed'); part_visibility = (Visibility { param($b) $b -match '(_fixed$|^charge_)' }) }
    "$prefix.fluid_l" = (FluidController 10 '^fluid_l_')
    "$prefix.fluid_r" = (FluidController 11 '^fluid_r_')
    "$prefix.glass"   = [ordered]@{ geometry = 'Geometry.default'; materials = @(@{ '*' = 'Material.glass' }); textures = @('Texture.glass'); part_visibility = (Visibility { param($b) $b -match '_glass$' }) }
}
[ordered]@{ format_version = '1.8.0'; render_controllers = $rcs } | ConvertTo-Json -Depth 20 -Compress |
    Set-Content -NoNewline -Encoding utf8 (Join-Path $rp 'render_controllers/alleviatedbackpacks_backpack.render_controllers.json')

# Per-context placement of the model (bone xf). Model frame = placed backpack (front = -z).
#   worn:  SB BackpackLayerRenderer: 180 degree turn, (0, -0.25, -0.3) blocks from the body.
#   held:  SB "thirdperson_righthand" / "firstperson_righthand" display transforms.
# Each value can be overridden at runtime for calibration (`/backpack modeltest pose`, DebugPose.h).
function Pose([int]$ctx, $pos, $rot, $scale) {
    $b = 30 + $ctx * 8
    $o = { param($k, $v) "q.abp_model($b) ? q.abp_model($($b + $k)) : $v" }
    [ordered]@{
        position = @((& $o 1 $pos[0]), (& $o 2 $pos[1]), (& $o 3 $pos[2]))
        rotation = @((& $o 4 $rot[0]), (& $o 5 $rot[1]), (& $o 6 $rot[2]))
        scale    = (& $o 7 $scale)
    }
}
# GX-1: client-only display entities (dropped backpacks, item frames) set their own xf transform
# (SB's "ground" / "fixed" display transforms), read through q.abp_model(80 + k): k 0 = on,
# 1..3 position (px), 4..6 rotation (degrees), 7 scale.
function EntityPose($pose) {
    $o = { param($k, $v) "q.abp_model(80) ? q.abp_model($(80 + $k)) : ($v)" }
    [ordered]@{
        position = @((& $o 1 $pose.position[0]), (& $o 2 $pose.position[1]), (& $o 3 $pose.position[2]))
        rotation = @((& $o 4 $pose.rotation[0]), (& $o 5 $pose.rotation[1]), (& $o 6 $pose.rotation[2]))
        scale    = (& $o 7 $pose.scale)
    }
}
$anim = [ordered]@{
    'animation.alleviatedbackpacks.backpack.worn'    = [ordered]@{ loop = $true; bones = [ordered]@{ xf = (Pose 1 @(0, 12, 4.8) @(0, 180, 0) 1) } }
    'animation.alleviatedbackpacks.backpack.held_tp' = [ordered]@{ loop = $true; bones = [ordered]@{ xf = (Pose 2 @(2, 12, -0.9) @(0, -90, 0) 0.75) } }
    'animation.alleviatedbackpacks.backpack.held_fp' = [ordered]@{ loop = $true; bones = [ordered]@{ xf = (Pose 3 @(0, 12, -8) @(0, 0, 0) 0.5) } }
    'animation.alleviatedbackpacks.backpack.placed'  = [ordered]@{ loop = $true; bones = [ordered]@{ xf = (EntityPose (Pose 4 @(0, 0, 0) @(0, 0, 0) 1)) } }
}
[ordered]@{ format_version = '1.10.0'; animations = $anim } | ConvertTo-Json -Depth 20 -Compress |
    Set-Content -NoNewline -Encoding utf8 (Join-Path $rp 'animations/alleviatedbackpacks_backpack.animation.json')

# Context from the attachable's own Molang context (the query's slot is not known during
# pre_animation): 1 worn, 2 main hand, 3 off hand, 4 placed (display entity).
$pre = "v.abp_ctx = q.owner_identifier == 'alleviatedbackpacks:backpack_display' ? 4 : (c.item_slot == 'main_hand' ? 2 : (c.item_slot == 'off_hand' ? 3 : 1));"
foreach ($t in $tiers) {
    $textures = [ordered]@{
        tint  = 'textures/entity/alleviatedbackpacks/backpack/tint'
        fixed = "textures/entity/alleviatedbackpacks/backpack/fixed_$t"
        glass = 'textures/entity/alleviatedbackpacks/backpack/glass'
    }
    foreach ($k in $looks) { $textures["fluid_$k"] = $fluidSprites[$k].path }
    $att = [ordered]@{
        format_version        = '1.10.0'
        'minecraft:attachable' = [ordered]@{
            description = [ordered]@{
                identifier         = "alleviatedbackpacks:$($t)_backpack"
                materials          = [ordered]@{ default = 'entity_alphatest'; tint = 'entity_alphatest_multicolor_tint'; glass = 'entity_alphablend'; fluid = 'entity_alphatest' }
                textures           = $textures
                geometry           = [ordered]@{ default = 'geometry.alleviatedbackpacks.backpack'; fluid = 'geometry.alleviatedbackpacks.backpack_fluid' }
                animations         = [ordered]@{
                    worn    = 'animation.alleviatedbackpacks.backpack.worn'
                    held_tp = 'animation.alleviatedbackpacks.backpack.held_tp'
                    held_fp = 'animation.alleviatedbackpacks.backpack.held_fp'
                    placed  = 'animation.alleviatedbackpacks.backpack.placed'
                }
                scripts            = [ordered]@{
                    pre_animation = @($pre)
                    animate       = @(
                        [ordered]@{ worn = 'v.abp_ctx == 1' },
                        [ordered]@{ held_tp = '(v.abp_ctx == 2 || v.abp_ctx == 3) && !c.is_first_person' },
                        [ordered]@{ held_fp = '(v.abp_ctx == 2 || v.abp_ctx == 3) && c.is_first_person' },
                        [ordered]@{ placed = 'v.abp_ctx == 4' }
                    )
                }
                render_controllers = @("$prefix.tint", "$prefix.fixed", "$prefix.fluid_l", "$prefix.fluid_r", "$prefix.glass")
            }
        }
    }
    $att | ConvertTo-Json -Depth 20 -Compress | Set-Content -NoNewline -Encoding utf8 (Join-Path $rp "attachables/alleviatedbackpacks_$($t)_backpack.json")
}
Write-Host 'attachables, render controllers and animations written'

# ---- placed backpack display entity ----------------------------------------------------------------
# Wears the backpack (chest attachable, bound to "body") and holds the displayed item at "rightItem",
# placed on SB's display anchor (backpack_display_item.json: 8x8 px quad centred at 8, 9.6, 4.015).
$da = $DisplayAnchor
$ax = 8 - ($da.f[0] + $da.t[0]) / 2; $ay = ($da.f[1] + $da.t[1]) / 2; $az = $da.f[2] - 8
$dgeo = [ordered]@{
    format_version      = '1.16.0'
    'minecraft:geometry' = @(
        [ordered]@{
            description = [ordered]@{ identifier = 'geometry.alleviatedbackpacks.backpack_display'; texture_width = 16; texture_height = 16; visible_bounds_width = 2; visible_bounds_height = 2; visible_bounds_offset = @(0, 0.5, 0) }
            bones       = @(
                [ordered]@{ name = 'root'; pivot = @(0, 0, 0) }
                [ordered]@{ name = 'body'; parent = 'root'; pivot = @(0, 24, 0) }
                [ordered]@{ name = 'rightArm'; parent = 'body'; pivot = @($ax, $ay, $az) }
                [ordered]@{ name = 'rightItem'; parent = 'rightArm'; pivot = @($ax, $ay, $az) }
            )
        }
    )
}
$dgeo | ConvertTo-Json -Depth 20 -Compress | Set-Content -NoNewline -Encoding utf8 (Join-Path $rp 'models/entity/alleviatedbackpacks_backpack_display.geo.json')
[ordered]@{ format_version = '1.8.0'; render_controllers = [ordered]@{
    'controller.render.alleviatedbackpacks.backpack_display' = [ordered]@{ geometry = 'Geometry.default'; materials = @(@{ '*' = 'Material.default' }); textures = @('Texture.default') }
} } | ConvertTo-Json -Depth 20 -Compress | Set-Content -NoNewline -Encoding utf8 (Join-Path $rp 'render_controllers/alleviatedbackpacks_backpack_display.render_controllers.json')
$displayPose = Pose 5 @(-5, -1, 1) @(-90, 0, 0) 0.5
$displayPose.rotation[2] = "($($displayPose.rotation[2])) + v.abp_rot"
$displayPose.position[2] = "($($displayPose.position[2])) - v.abp_z * 0.5"
# rightItem: rotation around the anchor normal (SC rotation) and depth (SC z offset: 1 = one item pixel).
[ordered]@{ format_version = '1.10.0'; animations = [ordered]@{
    'animation.alleviatedbackpacks.backpack_display.item' = [ordered]@{ loop = $true; bones = [ordered]@{
        rightArm  = [ordered]@{ rotation = @(0, 0, 0) }
        rightItem = $displayPose
    } }
} } | ConvertTo-Json -Depth 20 -Compress | Set-Content -NoNewline -Encoding utf8 (Join-Path $rp 'animations/alleviatedbackpacks_backpack_display.animation.json')
Write-Host 'display entity resources written'

# ---- placed backpack blocks (one per tier) ---------------------------------------------------------
# The block itself is invisible (the display entity draws the tinted model); its geometry is the
# model's shape so the crack overlay follows the backpack, and the collision / selection box is
# the shape's bounds per part combination (Bedrock blocks have one box), rotated with facing.
$blockParts = 'base', 'front_pouch', 'battery', 'left_pouch', 'left_tank', 'right_pouch', 'right_tank', 'straps'
$bgBones = [System.Collections.Generic.List[object]]::new()
$bgBones.Add([ordered]@{ name = 'root'; pivot = @(0, 0, 0) })
foreach ($p in $blockParts) {
    $cubes = [System.Collections.Generic.List[object]]::new()
    foreach ($b in ($boxes | Where-Object { $_.part -eq $p -and $_.mat -ne 'glass' })) {
        $size = @(($b.to[0] - $b.from[0]), ($b.to[1] - $b.from[1]), ($b.to[2] - $b.from[2]))
        if (($size | Where-Object { $_ -le 0 }).Count -gt 0) { continue }
        $cubes.Add([ordered]@{ origin = @((8 - $b.to[0]), $b.from[1], ($b.from[2] - 8)); size = $size; uv = @(0, 0) })
    }
    $bgBones.Add([ordered]@{ name = $p; parent = 'root'; pivot = @(0, 0, 0); cubes = $cubes })
}
[ordered]@{ format_version = '1.16.0'; 'minecraft:geometry' = @([ordered]@{
    description = [ordered]@{ identifier = 'geometry.alleviatedbackpacks.placed_backpack'; texture_width = 16; texture_height = 16 }
    bones = $bgBones
}) } | ConvertTo-Json -Depth 20 -Compress | Set-Content -NoNewline -Encoding utf8 (Join-Path $rp 'models/blocks/alleviatedbackpacks_placed_backpack.geo.json')

function Bounds([string[]]$parts) {
    $min = @(99.0, 99.0, 99.0); $max = @(-99.0, -99.0, -99.0)
    foreach ($b in ($boxes | Where-Object { $parts -contains $_.part })) {
        $lo = @((8 - $b.to[0]), $b.from[1], ($b.from[2] - 8)); $hi = @((8 - $b.from[0]), $b.to[1], ($b.to[2] - 8))
        for ($i = 0; $i -lt 3; $i++) { $min[$i] = [Math]::Min($min[$i], $lo[$i]); $max[$i] = [Math]::Max($max[$i], $hi[$i]) }
    }
    [ordered]@{ origin = @($min | ForEach-Object { [Math]::Round($_, 3) }); size = @(0, 1, 2 | ForEach-Object { [Math]::Round($max[$_] - $min[$_], 3) }) }
}
$bpDir = Join-Path $root 'assets/pack/bp/blocks'
New-Item -ItemType Directory -Force $bpDir | Out-Null
$rotations = @{ 0 = 180; 1 = 90; 2 = 0; 3 = 270 } # facing: 0 south, 1 west, 2 north, 3 east
$S = { param($n) "q.block_state('alleviatedbackpacks:$n')" }
foreach ($t in $tiers) {
    $perms = [System.Collections.Generic.List[object]]::new()
    foreach ($f in 0..3) {
        $perms.Add([ordered]@{ condition = "$(& $S 'facing') == $f"; components = [ordered]@{ 'minecraft:transformation' = [ordered]@{ rotation = @(0, $rotations[$f], 0) } } })
    }
    foreach ($bat in $false, $true) { foreach ($lt in $false, $true) { foreach ($rt in $false, $true) {
        $parts = @('base', 'straps', $(if ($bat) { 'battery' } else { 'front_pouch' }), $(if ($lt) { 'left_tank' } else { 'left_pouch' }), $(if ($rt) { 'right_tank' } else { 'right_pouch' }))
        $box = Bounds $parts
        $cond = "$(& $S 'battery') == $($bat.ToString().ToLower()) && $(& $S 'left_tank') == $($lt.ToString().ToLower()) && $(& $S 'right_tank') == $($rt.ToString().ToLower())"
        $perms.Add([ordered]@{ condition = $cond; components = [ordered]@{ 'minecraft:collision_box' = $box; 'minecraft:selection_box' = $box } })
    } } }
    $block = [ordered]@{
        format_version   = '1.26.0'
        'minecraft:block' = [ordered]@{
            description  = [ordered]@{
                identifier    = "alleviatedbackpacks:placed_$($t)_backpack"
                menu_category = [ordered]@{ category = 'none'; is_hidden_in_commands = $true }
                states        = [ordered]@{
                    'alleviatedbackpacks:facing'     = @(0, 1, 2, 3)
                    'alleviatedbackpacks:left_tank'  = @($false, $true)
                    'alleviatedbackpacks:right_tank' = @($false, $true)
                    'alleviatedbackpacks:battery'    = @($false, $true)
                    'alleviatedbackpacks:open'       = @($false, $true)
                }
            }
            components   = [ordered]@{
                'minecraft:display_name'            = "tile.alleviatedbackpacks:placed_$($t)_backpack.name"
                'minecraft:geometry'                = [ordered]@{
                    identifier      = 'geometry.alleviatedbackpacks.placed_backpack'
                    bone_visibility = [ordered]@{
                        front_pouch = "!$(& $S 'battery')"; battery = (& $S 'battery')
                        left_pouch  = "!$(& $S 'left_tank')"; left_tank = (& $S 'left_tank')
                        right_pouch = "!$(& $S 'right_tank')"; right_tank = (& $S 'right_tank')
                    }
                }
                'minecraft:material_instances'      = [ordered]@{ '*' = [ordered]@{ texture = 'abp_placed_backpack_hidden'; render_method = 'alpha_test'; ambient_occlusion = $false; face_dimming = $false } }
                'minecraft:destruction_particles'   = [ordered]@{ texture = "abp_placed_$($t)_backpack" }
                'minecraft:collision_box'           = (Bounds @('base', 'straps', 'front_pouch', 'left_pouch', 'right_pouch'))
                'minecraft:selection_box'           = (Bounds @('base', 'straps', 'front_pouch', 'left_pouch', 'right_pouch'))
                'minecraft:destructible_by_mining'  = [ordered]@{ seconds_to_destroy = 0.8 }
                'minecraft:destructible_by_explosion' = [ordered]@{ explosion_resistance = $(if ($t -eq 'netherite') { 1200 } else { 0.8 }) }
                'minecraft:movable'                 = [ordered]@{ movement_type = 'popped' }
                'minecraft:loot'                    = 'loot_tables/blocks/abp_placed_backpack.json'
                'minecraft:light_dampening'         = 0
                'minecraft:map_color'               = '#c7c7c7'
                'minecraft:liquid_detection'        = [ordered]@{ detection_rules = @([ordered]@{ liquid_type = 'water'; can_contain_liquid = $true; on_liquid_touches = 'no_reaction' }) }
            }
            permutations = $perms
        }
    }
    $block | ConvertTo-Json -Depth 20 | Set-Content -Encoding utf8 (Join-Path $bpDir "placed_$($t)_backpack.json")
}
# Invisible block texture and per-tier break particles (cloth in the default colour + clips).
$hidden = New-Object 'uint32[]' (16 * 16)
Save-Buf $hidden (Join-Path $rp 'textures/blocks/placed_backpack_hidden.png') 16 16
foreach ($t in $tiers) {
    $p = $clipPalettes[$t]
    $part = New-Object 'uint32[]' (16 * 16)
    for ($y = 0; $y -lt 16; $y++) { for ($x = 0; $x -lt 16; $x++) {
        $n = Noise $x $y 3
        $part[$y * 16 + $x] = if ($y -ge 6 -and $y -le 7) { Shade $accentDefault (0.9 + 0.15 * $n) } elseif (($x -eq 4 -or $x -eq 11) -and $y -gt 7) { FromColor $p.base } else { Shade $mainDefault (0.85 + 0.2 * $n) }
    } }
    Save-Buf $part (Join-Path $rp "textures/blocks/placed_$($t)_backpack.png") 16 16
}
Write-Host 'placed backpack blocks written'
