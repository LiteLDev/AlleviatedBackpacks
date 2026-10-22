# Draws the Mob Catcher and Advanced Mob Catcher upgrade textures (16x16, original pixel art) on top
# of the Upgrade Base plate.
#   pwsh tools/textures/mobcatcher.ps1
#     -> assets/pack/rp/textures/items/{mob_catcher,advanced_mob_catcher}_upgrade.png
# Overlay palette letters must not collide (case-insensitively) with the base: o F H M S R.

. "$PSScriptRoot/upgrade_base.ps1" -NoWrite

$root = (Resolve-Path "$PSScriptRoot/../..").Path
$out = Join-Path $root 'assets/pack/rp/textures/items'

# A lead looped into a lasso around a captured ender pearl (the recipe's lead + pearl).
# l rope, d rope shadow, k knot, e pearl, q pearl glint, c pearl core
$catcher = @(
    '................',
    '................',
    '.....llllll.....',
    '....ld....dl....',
    '...ld......dl...',
    '...l...ee...l...',
    '...l..eqce..l...',
    '...d..ecce..d...',
    '...dl..ee..ld...',
    '....dl....ld....',
    '.....ddkkdd.....',
    '.......kk.......',
    '......l..d......',
    '.....l....d.....',
    '................',
    '................'
)

$basicPalette = @{ l = '#ecd5ae'; d = '#a87d4c'; k = '#5a3a1c'; e = '#1f7f6a'; q = '#bff5e3'; c = '#0b3b33' }
Save-PixelArt -Rows (Merge-Rows $UpgradeBaseRows $catcher) -Path (Join-Path $out 'mob_catcher_upgrade.png') -Palette (
    $UpgradeBasePalette + $basicPalette
)

# Advanced: a golden lasso holding an eye of ender (the advanced recipe's gold and ender eye).
$advancedPalette = @{ l = '#ffd43b'; d = '#c98a12'; k = '#7a4d05'; e = '#3c8f5a'; q = '#e6f58a'; c = '#1d1d38' }
Save-PixelArt -Rows (Merge-Rows $UpgradeBaseRows $catcher) -Path (Join-Path $out 'advanced_mob_catcher_upgrade.png') -Palette (
    $UpgradeBasePalette + $advancedPalette
)
Write-Host "wrote mob_catcher_upgrade.png and advanced_mob_catcher_upgrade.png to $out"
