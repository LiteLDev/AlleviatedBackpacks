# Shared drawing for the Deposit / Restock upgrade textures (16x16): the upgrade base plate with a
# small chest and an arrow (into the chest: deposit, out of it: restock). Advanced variants get a
# gold frame and diamond rivets. Dot-sourced by deposit.ps1 / restock.ps1.

. "$PSScriptRoot/upgrade_base.ps1" -NoWrite

function Save-TransferUpgrade([string[]]$Arrow, $ArrowLight, $ArrowDark, [string]$Name) {
    $box = @(
        '................',
        '................',
        '................',
        '................',
        '................',
        '................',
        '................',
        '................',
        '................',
        '....cccccccc....',
        '....cddkkddc....',
        '....cccccccc....',
        '................',
        '................',
        '................',
        '................'
    )
    $rows = Merge-Rows (Merge-Rows $UpgradeBaseRows $box) $Arrow
    $pal  = $UpgradeBasePalette.Clone()
    $pal['a'] = $ArrowLight
    $pal['b'] = $ArrowDark
    $pal['c'] = '#4a2f14'
    $pal['d'] = '#a5702e'
    $pal['k'] = '#e3c35a'
    $root = (Resolve-Path "$PSScriptRoot/../..").Path
    $path = Join-Path $root "assets/pack/rp/textures/items/${Name}_upgrade.png"
    Save-PixelArt -Rows $rows -Palette $pal -Path $path
    Write-Host "wrote $path"

    $adv = $pal.Clone()
    $adv['F'] = '#8a6a12'
    $adv['H'] = '#f0cf4a'
    $adv['S'] = '#b08a1c'
    $adv['R'] = '#62e6dc'
    $path = Join-Path $root "assets/pack/rp/textures/items/advanced_${Name}_upgrade.png"
    Save-PixelArt -Rows $rows -Palette $adv -Path $path
    Write-Host "wrote $path"
}
