# Generates the backpack tier recipes (U1) into assets/pack/bp/recipes/tiers/.
#   pwsh tools/recipes/tiers.ps1
#
# - Copper / Iron / Gold / Diamond: the previous tier surrounded by 8 of the tier's material.
# - Netherite: smithing table (template + diamond backpack + netherite ingot), or the same three
#   items shapeless in a crafting grid.
# - Dyeing: any backpack + one dye (shapeless). Where the dye sits decides the colour (handled by
#   the mod's crafting hook): before the backpack (left / row above) = main, after = accent.
# The leather backpack recipe (assets/pack/bp/recipes/leather_backpack.json) is hand-written.
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/../..").Path
$out = Join-Path $root 'assets/pack/bp/recipes/tiers'
if (Test-Path $out) { Remove-Item -Recurse -Force $out }
New-Item -ItemType Directory -Force $out | Out-Null

$ns = 'alleviatedbackpacks'
function Save-Recipe([string]$name, $obj) {
    $json = $obj | ConvertTo-Json -Depth 10
    [IO.File]::WriteAllText((Join-Path $out "$name.json"), $json + "`n")
}

$upgrades = [ordered]@{
    copper  = @{ from = 'leather'; material = 'minecraft:copper_ingot' }
    iron    = @{ from = 'copper';  material = 'minecraft:iron_ingot' }
    gold    = @{ from = 'iron';    material = 'minecraft:gold_ingot' }
    diamond = @{ from = 'gold';    material = 'minecraft:diamond' }
}
foreach ($tier in $upgrades.Keys) {
    $u = $upgrades[$tier]
    Save-Recipe "${tier}_backpack" ([ordered]@{
        format_version          = '1.20.10'
        'minecraft:recipe_shaped' = [ordered]@{
            description = @{ identifier = "${ns}:${tier}_backpack" }
            tags        = @('crafting_table')
            pattern     = @('MMM', 'MBM', 'MMM')
            key         = [ordered]@{
                M = @{ item = $u.material }
                B = @{ item = "${ns}:$($u.from)_backpack" }
            }
            unlock      = @(@{ item = "${ns}:$($u.from)_backpack" })
            result      = @{ item = "${ns}:${tier}_backpack" }
        }
    })
}

Save-Recipe 'netherite_backpack_smithing' ([ordered]@{
    format_version                       = '1.20.10'
    'minecraft:recipe_smithing_transform' = [ordered]@{
        description = @{ identifier = "${ns}:netherite_backpack_smithing" }
        tags        = @('smithing_table')
        template    = 'minecraft:netherite_upgrade_smithing_template'
        base        = "${ns}:diamond_backpack"
        addition    = 'minecraft:netherite_ingot'
        result      = "${ns}:netherite_backpack"
    }
})
Save-Recipe 'netherite_backpack' ([ordered]@{
    format_version              = '1.20.10'
    'minecraft:recipe_shapeless' = [ordered]@{
        description = @{ identifier = "${ns}:netherite_backpack" }
        tags        = @('crafting_table')
        ingredients = @(
            @{ item = 'minecraft:netherite_upgrade_smithing_template' },
            @{ item = "${ns}:diamond_backpack" },
            @{ item = 'minecraft:netherite_ingot' }
        )
        unlock      = @(@{ item = "${ns}:diamond_backpack" })
        result      = @{ item = "${ns}:netherite_backpack" }
    }
})

$tiers = 'leather', 'copper', 'iron', 'gold', 'diamond', 'netherite'
$dyes = 'white', 'orange', 'magenta', 'light_blue', 'yellow', 'lime', 'pink', 'gray',
        'light_gray', 'cyan', 'purple', 'blue', 'brown', 'green', 'red', 'black'
$n = 0
foreach ($tier in $tiers) {
    foreach ($dye in $dyes) {
        Save-Recipe "dye_${tier}_${dye}" ([ordered]@{
            format_version              = '1.20.10'
            'minecraft:recipe_shapeless' = [ordered]@{
                description = @{ identifier = "${ns}:${tier}_backpack_dye_${dye}" }
                tags        = @('crafting_table')
                ingredients = @(@{ item = "${ns}:${tier}_backpack" }, @{ item = "minecraft:${dye}_dye" })
                unlock      = @(@{ item = "${ns}:${tier}_backpack" })
                result      = @{ item = "${ns}:${tier}_backpack" }
            }
        })
        $n++
    }
}
Write-Host "wrote $($upgrades.Count + 2 + $n) recipes to $out"
