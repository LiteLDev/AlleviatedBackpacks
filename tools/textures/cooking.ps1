# Draws the cooking upgrade textures (16x16): Smelting, Smoking, Blasting and their Auto variants.
# Each is the upgrade base plate with a small furnace front; Auto variants add a green funnel mark.
param([switch]$Preview)

. "$PSScriptRoot/upgrade_base.ps1" -NoWrite

# k outline, g casing, i casing highlight, d firebox, y flame core, n flame (keys must not clash
# case-insensitively with the base palette)
$furnace = @(
    '................',
    '................',
    '................',
    '................',
    '....kkkkkkkk....',
    '....kiiiiiik....',
    '....kgkkkkgk....',
    '....kgdyddgk....',
    '....kgdnydgk....',
    '....kgnnyngk....',
    '....kgkkkkgk....',
    '....kkkkkkkk....',
    '................',
    '................',
    '................',
    '................'
)
# w smoke puffs above the smoker
$smoke = @(
    '................',
    '......w.........',
    '.......w..w.....',
    '......w..w......',
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
# a funnel mark of the Auto variants (top right corner)
$funnel = @(
    '................',
    '..........aaaa..',
    '..........abba..',
    '...........aa...',
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

$kinds = @{
    smelting = @{ Palette = @{ k = '#2b2b2b'; i = '#b5b5b5'; g = '#8a8a8a'; d = '#1a1210'; y = '#ffe066'; n = '#ff8a1f' }; Smoke = $false }
    smoking  = @{ Palette = @{ k = '#2a1d12'; i = '#9b7448'; g = '#6e4f2e'; d = '#1a1210'; y = '#ffd166'; n = '#e86a1a' }; Smoke = $true }
    blasting = @{ Palette = @{ k = '#1f1f24'; i = '#c9ccd6'; g = '#5d6068'; d = '#120e10'; y = '#fff0a0'; n = '#ff5a1f' }; Smoke = $false }
}

$root = (Resolve-Path "$PSScriptRoot/../..").Path
foreach ($name in $kinds.Keys) {
    $k     = $kinds[$name]
    $rows  = Merge-Rows $UpgradeBaseRows $furnace
    $pal   = $UpgradeBasePalette.Clone()
    foreach ($key in $k.Palette.Keys) { $pal[$key] = $k.Palette[$key] }
    if ($k.Smoke) { $rows = Merge-Rows $rows $smoke; $pal['w'] = '#c8c8c8' }
    $pal['a'] = '#2f8f2f'
    $pal['b'] = '#7be07b'
    foreach ($auto in $false, $true) {
        $r    = if ($auto) { Merge-Rows $rows $funnel } else { $rows }
        $id   = if ($auto) { "auto_${name}_upgrade" } else { "${name}_upgrade" }
        $path = Join-Path $root "assets/pack/rp/textures/items/$id.png"
        Save-PixelArt -Rows $r -Palette $pal -Path $path
        if ($Preview) { Save-PixelArt -Rows $r -Palette $pal -Path (Join-Path $env:TEMP "$id.preview.png") -Scale 16 }
        Write-Host "wrote $path"
    }
}
