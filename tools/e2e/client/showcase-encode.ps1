# Turns the captures of the `showcase` client scenario (client/out/showcase/) into the gallery
# media in docs/showcase/: stills -> WebP, frame sequences -> MP4 (true speed) + WebP poster
# (+ an animated WebP for the README). Needs ffmpeg (on PATH or -FFmpeg).
#
#   pwsh tools/e2e/client/showcase-encode.ps1
param(
    [string]$FFmpeg = $(if (Get-Command ffmpeg -ErrorAction SilentlyContinue) { 'ffmpeg' } else {
        Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Packages\Gyan.FFmpeg_Microsoft.Winget.Source_8wekyb3d8bbwe\ffmpeg-9.0.2-full_build\bin\ffmpeg.exe' }),
    [string]$In = (Join-Path $PSScriptRoot 'out\showcase'),
    [string]$Out = (Join-Path $PSScriptRoot '..\..\..\docs\showcase')
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force $Out | Out-Null

function FF { & $FFmpeg -hide_banner -loglevel error -y @args; if ($LASTEXITCODE) { throw "ffmpeg failed: $args" } }

# Stills: <capture name> = <file in docs/showcase> [crop w:h:x:y in 1280x720 pixels, scaled back to 1280 wide]
$stills = [ordered]@{
    'placed-lineup'  = @{ File = 'tiers-placed.webp' }
    'screen-full'    = @{ File = 'backpack-screen.webp' }
    'screen-crafting'= @{ File = 'crafting-tab.webp' }
    'screen-feeding' = @{ File = 'feeding-tab.webp' }
    'worn-back'      = @{ File = 'worn.webp' }
    'crafting-table' = @{ File = 'recipe.webp' }
    'tooltip'        = @{ File = 'contents-tooltip.webp' }
}
foreach ($k in $stills.Keys) {
    $src = Join-Path $In "$k.png"
    if (-not (Test-Path $src)) { Write-Host "skip $k (not captured)"; continue }
    $vf = 'scale=1280:720:flags=lanczos'
    if ($stills[$k].Crop) { $vf = "crop=$($stills[$k].Crop),$vf" }
    FF -i $src -vf $vf '-c:v' libwebp -quality 85 -compression_level 6 (Join-Path $Out $stills[$k].File)
}

# Videos: frames-<name>/frames.txt lists "<file> <seconds>"; each frame lasts until the next one.
$videos = [ordered]@{
    'magnet' = @{ File = 'magnet'; Anim = $true }
}
foreach ($k in $videos.Keys) {
    $dir = Join-Path $In "frames-$k"
    if (-not (Test-Path $dir)) { Write-Host "skip video $k (not captured)"; continue }
    $rows = Get-Content (Join-Path $dir 'frames.txt') | ForEach-Object { $f, $t = $_ -split ' '; [pscustomobject]@{ F = $f; T = [double]$t } }
    $list = [System.Collections.Generic.List[string]]::new()
    for ($i = 0; $i -lt $rows.Count; $i++) {
        $d = if ($i + 1 -lt $rows.Count) { $rows[$i + 1].T - $rows[$i].T } else { 0.1 }
        $list.Add("file '$((Join-Path $dir $rows[$i].F) -replace '\\', '/')'")
        $list.Add(('duration {0:F4}' -f $d))
    }
    $list.Add("file '$((Join-Path $dir $rows[-1].F) -replace '\\', '/')'")
    $concat = Join-Path $dir 'concat.txt'
    [IO.File]::WriteAllLines($concat, $list)
    $base = $videos[$k].File
    FF -f concat -safe 0 -i $concat -vf 'scale=1280:720:flags=lanczos:out_range=tv,fps=30,format=yuv420p' '-c:v' libx264 -pix_fmt yuv420p -preset slow -crf 24 -an -movflags +faststart (Join-Path $Out "$base.mp4")
    $poster = $videos[$k].Poster
    $pf = if ($poster) { Join-Path $dir $poster } else { Join-Path $dir $rows[[int]($rows.Count * 0.15)].F }
    FF -i $pf -vf 'scale=1280:720:flags=lanczos' '-c:v' libwebp -quality 85 (Join-Path $Out "$base.webp")
    if ($videos[$k].Anim) {
        # README demo: smaller and slower frame rate, looping.
        FF -f concat -safe 0 -i $concat -vf 'scale=854:480:flags=lanczos,fps=15' '-c:v' libwebp_anim -quality 70 -compression_level 6 -loop 0 -an (Join-Path $Out "$base-anim.webp")
    }
}
Get-ChildItem $Out | Select-Object Name, @{ n = 'KB'; e = { [int]($_.Length / 1KB) } } | Format-Table -AutoSize
