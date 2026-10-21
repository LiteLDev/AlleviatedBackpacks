# Draws the Tool Swapper upgrade textures (16x16): the upgrade plate with a pickaxe and a swap arrow.
# Basic: iron head + green arrow. Advanced: diamond head + gold arrow.

. "$PSScriptRoot/upgrade_base.ps1" -NoWrite

# p pick head, d head shade, w handle, j joint, a arrow
$symbol = @(
    '................',
    '................',
    '................',
    '.....pppp.......',
    '.........p......',
    '........j.d.....',
    '.......w..d.....',
    '......w...d.....',
    '.....w....d.....',
    '....w...........',
    '...w.......a....',
    '.....aaaaaaaa...',
    '...........a....',
    '................',
    '................',
    '................'
)
$rows = Merge-Rows $UpgradeBaseRows $symbol
$root = (Resolve-Path "$PSScriptRoot/../..").Path

$variants = @(
    @{ File = 'tool_swapper_upgrade'; Head = '#d8d8d8'; Arrow = '#4cd964' },
    @{ File = 'advanced_tool_swapper_upgrade'; Head = '#5fe3dc'; Arrow = '#f5c542' }
)
foreach ($v in $variants) {
    $palette = $UpgradeBasePalette.Clone()
    $palette['p'] = $v.Head
    $palette['d'] = (Shade-Color $v.Head 0.72)
    $palette['w'] = '#7a5230'
    $palette['j'] = '#4a3020'
    $palette['a'] = $v.Arrow
    $path = Join-Path $root "assets/pack/rp/textures/items/$($v.File).png"
    Save-PixelArt -Rows $rows -Palette $palette -Path $path
    Write-Host "wrote $path"
}
