# Draws the Upgrade Base item texture (16x16): the blank plate every upgrade is crafted from.
# Upgrade units can reuse $UpgradeBaseRows / $UpgradeBasePalette and overlay their own symbol:
#
#   . "$PSScriptRoot/upgrade_base.ps1" -NoWrite
#   $rows = Merge-Rows $UpgradeBaseRows $mySymbolRows
#   Save-PixelArt -Rows $rows -Palette ($UpgradeBasePalette + @{ s = '#55ff55' }) -Path ...
param([switch]$NoWrite)

. "$PSScriptRoot/lib.ps1"

# o outline, F frame, H highlight, M plate, S shadow, R rivet
$UpgradeBaseRows = @(
    '................',
    '.oooooooooooooo.',
    '.oHHHHHHHHHHHFo.',
    '.oHRMMMMMMMMRSo.',
    '.oHMMMMMMMMMMSo.',
    '.oHMMMMMMMMMMSo.',
    '.oHMMMMMMMMMMSo.',
    '.oHMMMMMMMMMMSo.',
    '.oHMMMMMMMMMMSo.',
    '.oHMMMMMMMMMMSo.',
    '.oHMMMMMMMMMMSo.',
    '.oHMMMMMMMMMMSo.',
    '.oHRMMMMMMMMRSo.',
    '.oFSSSSSSSSSSSo.',
    '.oooooooooooooo.',
    '................'
)
$UpgradeBasePalette = @{
    o = '#2a2420'
    F = '#6b5a4a'
    H = '#9a8672'
    M = '#7d6b59'
    S = '#5a4b3e'
    R = '#c9c9c9'
}

if (-not $NoWrite) {
    $root = (Resolve-Path "$PSScriptRoot/../..").Path
    $path = Join-Path $root 'assets/pack/rp/textures/items/upgrade_base.png'
    Save-PixelArt -Rows $UpgradeBaseRows -Palette $UpgradeBasePalette -Path $path
    Write-Host "wrote $path"
}
