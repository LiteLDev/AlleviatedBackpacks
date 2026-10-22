# Generates every crafting recipe of the mod into assets/pack/bp/recipes/ (P2-12).
#   pwsh tools/recipes/recipes.ps1
#
# The table follows Sophisticated Backpacks' recipes (behaviour reference: their shapes and
# ingredients; no files are copied). Java item tags map to the closest Bedrock item or tag:
#   c:ingots/* -> <metal>_ingot, c:gems/diamond -> diamond, c:gems/lapis -> lapis_lazuli,
#   c:dusts/redstone -> redstone, c:ender_pearls -> ender_pearl, c:strings -> string,
#   c:leathers -> leather, c:chests(/wooden) -> chest, c:storage_blocks/* -> <metal>_block,
#   c:rods/wooden -> stick, c:nether_stars -> nether_star, c:obsidians -> obsidian,
#   c:glass_blocks -> glass, minecraft:logs -> the Bedrock "minecraft:logs" item tag.
#
# Special recipes:
# - Backpack tier upgrades are plain shaped recipes; the mod's crafting hook (src/feature/tiers)
#   carries the consumed backpack's identity (uuid, colours, upgrades, contents) into the result.
# - "Next tier" upgrade recipes (SC `upgrade_next_tier`, e.g. Advanced Pickup from Pickup) are
#   plain shaped recipes; the same hook copies the consumed upgrade's data (settings) over.
# - Backpack dyeing (SB `backpack_dye`): one backpack plus 1..8 dyes, shapeless, through the item
#   tag "alleviatedbackpacks:dye" (the mod matches the 16 dyes against it). The hook computes the
#   colours from the dyes' grid columns.
# - Netherite backpack: smithing table only (template + diamond backpack + netherite ingot).
#
# Hopper upgrades (not part of Sophisticated Backpacks) keep their hand-written recipes.
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/../..").Path
$out  = Join-Path $root 'assets/pack/bp/recipes'
$ns   = 'alleviatedbackpacks'

# Generated files are tracked in a manifest so stale ones can be removed on the next run.
$manifestPath = Join-Path $PSScriptRoot 'generated.txt'
if (Test-Path $manifestPath) {
    foreach ($rel in Get-Content $manifestPath) {
        $p = Join-Path $out $rel
        if ($rel -and (Test-Path $p)) { Remove-Item $p }
    }
}
$written = [System.Collections.Generic.List[string]]::new()

function Save-Recipe([string]$rel, $obj) {
    $path = Join-Path $out "$rel.json"
    New-Item -ItemType Directory -Force (Split-Path $path) | Out-Null
    $json = ($obj | ConvertTo-Json -Depth 10) -replace "`r`n", "`n"
    [IO.File]::WriteAllText($path, $json + "`n")
    $written.Add("$rel.json")
}

function Ingredient([string]$name) {
    if ($name.StartsWith('#')) { return [ordered]@{ tag = $name.Substring(1) } }
    if (-not $name.Contains(':')) { $name = "${ns}:$name" }
    return [ordered]@{ item = $name }
}

# Shaped recipe. $key maps pattern letters to item ids ("#tag" for tags, bare names are ours).
function Shaped([string]$rel, [string]$id, [string]$result, [string[]]$pattern, $key, [string]$unlock) {
    $k = [ordered]@{}
    foreach ($letter in $key.Keys) { $k[$letter] = Ingredient $key[$letter] }
    if (-not $unlock) {
        $ours = @($key.Values | Where-Object { -not $_.Contains(':') -and -not $_.StartsWith('#') })
        $unlock = if ($ours.Count) { $ours[0] } else { @($key.Values)[0] }
    }
    Save-Recipe $rel ([ordered]@{
        format_version            = '1.20.10'
        'minecraft:recipe_shaped' = [ordered]@{
            description = @{ identifier = "${ns}:$id" }
            tags        = @('crafting_table')
            pattern     = $pattern
            key         = $k
            unlock      = @(Ingredient $unlock)
            result      = [ordered]@{ item = "${ns}:$result"; count = 1 }
        }
    })
}
function Recipe([string]$id, [string[]]$pattern, $key, [string]$result = $id) { Shaped $id $id $result $pattern $key $null }

$I = 'minecraft:iron_ingot'; $G = 'minecraft:gold_ingot'; $D = 'minecraft:diamond'; $R = 'minecraft:redstone'
$E = 'minecraft:ender_pearl'; $S = 'minecraft:string'; $L = 'minecraft:leather'; $C = 'minecraft:chest'
$B = 'upgrade_base'
$adv = @(' D ', 'G#G', 'RRR')
function Advanced([string]$id, [string]$from) { Recipe $id $adv ([ordered]@{ D = $D; G = $G; '#' = $from; R = $R }) }

# ---- base items ----------------------------------------------------------------------------------
Recipe 'upgrade_base' @('SIS', 'ILI', 'SIS') ([ordered]@{ S = $S; I = $I; L = $L })
Shaped 'leather_backpack' 'leather_backpack' 'leather_backpack' @('SLS', 'SCS', 'LLL') ([ordered]@{ S = $S; L = $L; C = $C }) $L

# ---- upgrades ------------------------------------------------------------------------------------
Recipe 'pickup_upgrade' @(' P ', 'SBS', 'RRR') ([ordered]@{ P = 'minecraft:sticky_piston'; S = $S; B = $B; R = $R })
Advanced 'advanced_pickup_upgrade' 'pickup_upgrade'
Recipe 'filter_upgrade' @('RSR', 'SBS', 'RSR') ([ordered]@{ R = $R; S = $S; B = $B })
Recipe 'advanced_filter_upgrade' @('GPG', 'RRR') ([ordered]@{ G = $G; P = 'filter_upgrade'; R = $R })
Recipe 'magnet_upgrade' @('EIE', 'IPI', 'R L') ([ordered]@{ E = $E; I = $I; P = 'pickup_upgrade'; R = $R; L = 'minecraft:lapis_lazuli' })
Recipe 'advanced_magnet_upgrade' @('EIE', 'IPI', 'R L') ([ordered]@{ E = $E; I = $I; P = 'advanced_pickup_upgrade'; R = $R; L = 'minecraft:lapis_lazuli' })
Shaped 'advanced_magnet_upgrade_from_magnet' 'advanced_magnet_upgrade_from_magnet' 'advanced_magnet_upgrade' $adv ([ordered]@{ D = $D; G = $G; '#' = 'magnet_upgrade'; R = $R }) $null
Recipe 'feeding_upgrade' @(' C ', 'ABM', ' E ') ([ordered]@{ C = 'minecraft:golden_carrot'; A = 'minecraft:golden_apple'; B = $B; M = 'minecraft:glistering_melon_slice'; E = $E })
Advanced 'advanced_feeding_upgrade' 'feeding_upgrade'
Recipe 'compacting_upgrade' @('IPI', 'PBP', 'RPR') ([ordered]@{ I = $I; P = 'minecraft:piston'; B = $B; R = $R })
Advanced 'advanced_compacting_upgrade' 'compacting_upgrade'
Recipe 'void_upgrade' @(' E ', 'OBO', 'ROR') ([ordered]@{ E = $E; O = 'minecraft:obsidian'; B = $B; R = $R })
Advanced 'advanced_void_upgrade' 'void_upgrade'
Recipe 'restock_upgrade' @(' P ', 'IBI', 'RCR') ([ordered]@{ P = 'minecraft:sticky_piston'; I = $I; B = $B; R = $R; C = $C })
Advanced 'advanced_restock_upgrade' 'restock_upgrade'
Recipe 'deposit_upgrade' @(' P ', 'IBI', 'RCR') ([ordered]@{ P = 'minecraft:piston'; I = $I; B = $B; R = $R; C = $C })
Advanced 'advanced_deposit_upgrade' 'deposit_upgrade'
Recipe 'refill_upgrade' @(' E ', 'IBI', 'RCR') ([ordered]@{ E = $E; I = $I; B = $B; R = $R; C = $C })
Advanced 'advanced_refill_upgrade' 'refill_upgrade'
Recipe 'inception_upgrade' @('ESE', 'DBD', 'EDE') ([ordered]@{ E = 'minecraft:ender_eye'; S = 'minecraft:nether_star'; D = $D; B = $B })
Recipe 'everlasting_upgrade' @('CSC', 'SBS', 'CSC') ([ordered]@{ C = 'minecraft:end_crystal'; S = 'minecraft:nether_star'; B = $B })
Recipe 'smelting_upgrade' @('RIR', 'IBI', 'RFR') ([ordered]@{ R = $R; I = $I; B = $B; F = 'minecraft:furnace' })
Recipe 'smoking_upgrade' @('RIR', 'IBI', 'RSR') ([ordered]@{ R = $R; I = $I; B = $B; S = 'minecraft:smoker' })
Shaped 'smoking_upgrade_from_smelting_upgrade' 'smoking_upgrade_from_smelting_upgrade' 'smoking_upgrade' @(' L ', 'LSL', ' L ') ([ordered]@{ L = '#minecraft:logs'; S = 'smelting_upgrade' }) $null
Recipe 'blasting_upgrade' @('RIR', 'IBI', 'RFR') ([ordered]@{ R = $R; I = $I; B = $B; F = 'minecraft:blast_furnace' })
Shaped 'blasting_upgrade_from_smelting_upgrade' 'blasting_upgrade_from_smelting_upgrade' 'blasting_upgrade' @('III', 'ISI', 'TTT') ([ordered]@{ I = $I; S = 'smelting_upgrade'; T = 'minecraft:smooth_stone' }) $null
$auto = @('DHD', 'RSH', 'GHG')
Recipe 'auto_smelting_upgrade' $auto ([ordered]@{ D = $D; H = 'minecraft:hopper'; R = $R; S = 'smelting_upgrade'; G = $G })
Recipe 'auto_smoking_upgrade' $auto ([ordered]@{ D = $D; H = 'minecraft:hopper'; R = $R; S = 'smoking_upgrade'; G = $G })
Recipe 'auto_blasting_upgrade' $auto ([ordered]@{ D = $D; H = 'minecraft:hopper'; R = $R; S = 'blasting_upgrade'; G = $G })
Shaped 'auto_smoking_upgrade_from_auto_smelting_upgrade' 'auto_smoking_upgrade_from_auto_smelting_upgrade' 'auto_smoking_upgrade' @(' L ', 'LSL', ' L ') ([ordered]@{ L = '#minecraft:logs'; S = 'auto_smelting_upgrade' }) $null
Shaped 'auto_blasting_upgrade_from_auto_smelting_upgrade' 'auto_blasting_upgrade_from_auto_smelting_upgrade' 'auto_blasting_upgrade' @('III', 'ISI', 'TTT') ([ordered]@{ I = $I; S = 'auto_smelting_upgrade'; T = 'minecraft:smooth_stone' }) $null
Recipe 'crafting_upgrade' @(' T ', 'IBI', ' C ') ([ordered]@{ T = 'minecraft:crafting_table'; I = $I; B = $B; C = $C })
Recipe 'stonecutter_upgrade' @(' S ', 'IBI', ' R ') ([ordered]@{ S = 'minecraft:stonecutter_block'; I = $I; B = $B; R = $R })
Recipe 'jukebox_upgrade' @(' J ', 'IBI', ' R ') ([ordered]@{ J = 'minecraft:jukebox'; I = $I; B = $B; R = $R })
Advanced 'advanced_jukebox_upgrade' 'jukebox_upgrade'
Recipe 'tool_swapper_upgrade' @('RWR', 'PBA', 'ISI') ([ordered]@{ R = $R; W = 'minecraft:wooden_sword'; P = 'minecraft:wooden_pickaxe'; B = $B; A = 'minecraft:wooden_axe'; I = $I; S = 'minecraft:wooden_shovel' })
Advanced 'advanced_tool_swapper_upgrade' 'tool_swapper_upgrade'
Recipe 'anvil_upgrade' @('ADA', 'IBI', ' C ') ([ordered]@{ A = 'minecraft:anvil'; D = $D; I = $I; B = $B; C = $C })
Recipe 'smithing_upgrade' @(' S ', 'IBI', ' C ') ([ordered]@{ S = 'minecraft:smithing_table'; I = $I; B = $B; C = $C })
Recipe 'alchemy_upgrade' @('TGF', 'IBI', 'RPR') ([ordered]@{ T = 'minecraft:ghast_tear'; G = 'minecraft:glass_bottle'; F = 'minecraft:fermented_spider_eye'; I = $I; B = $B; R = 'minecraft:blaze_rod'; P = 'minecraft:ender_pearl' })
Advanced 'advanced_alchemy_upgrade' 'alchemy_upgrade'
Recipe 'mob_catcher_upgrade' @(' E ', 'IBI', 'L L') ([ordered]@{ E = $E; I = $I; B = $B; L = 'minecraft:lead' })
# Next tier (keeps the basic catcher's components through the tier crafting hook).
Recipe 'advanced_mob_catcher_upgrade' @('HDE', 'GBG', 'SSS') ([ordered]@{ H = 'minecraft:wither_skeleton_skull'; D = $D; E = 'minecraft:ender_eye'; G = $G; B = 'mob_catcher_upgrade'; S = 'minecraft:soul_sand' })
Recipe 'battery_upgrade' @('GRG', 'RBR', 'GRG') ([ordered]@{ G = $G; R = 'minecraft:redstone_block'; B = $B })
Recipe 'tank_upgrade' @('GGG', 'GBG', 'GGG') ([ordered]@{ G = 'minecraft:glass'; B = $B })
Recipe 'pump_upgrade' @('GUG', 'PBS', 'GUG') ([ordered]@{ G = 'minecraft:glass'; U = 'minecraft:bucket'; P = 'minecraft:piston'; B = $B; S = 'minecraft:sticky_piston' })
Recipe 'advanced_pump_upgrade' @('DID', 'GPG', 'RRR') ([ordered]@{ D = $D; I = 'minecraft:dispenser'; G = $G; P = 'pump_upgrade'; R = $R })
Recipe 'xp_pump_upgrade' @('RER', 'CPC', 'RER') ([ordered]@{ R = $R; E = 'minecraft:ender_eye'; C = 'minecraft:experience_bottle'; P = 'advanced_pump_upgrade' })

# ---- stack upgrades ------------------------------------------------------------------------------
function Around([string]$id, [string]$material, [string]$center, [string]$result = $id) {
    Shaped $id $id $result @('MMM', 'MCM', 'MMM') ([ordered]@{ M = $material; C = $center }) $null
}
Around 'stack_starter_upgrade' 'minecraft:copper_block' $B
Around 'stack_tier_1_upgrade' 'minecraft:iron_block' $B
Shaped 'stack_tier_1_upgrade_from_starter' 'stack_tier_1_upgrade_from_starter' 'stack_tier_1_upgrade' @(' I ', 'ISI', ' I ') ([ordered]@{ I = 'minecraft:iron_block'; S = 'stack_starter_upgrade' }) $null
Around 'stack_tier_2_upgrade' 'minecraft:gold_block' 'stack_tier_1_upgrade'
Around 'stack_tier_3_upgrade' 'minecraft:diamond_block' 'stack_tier_2_upgrade'
Around 'stack_tier_4_upgrade' 'minecraft:netherite_block' 'stack_tier_3_upgrade'
Recipe 'stack_omega_upgrade' @('SSS', 'SSS', 'SSS') ([ordered]@{ S = 'stack_tier_4_upgrade' })
$stick = 'minecraft:stick'; $flint = 'minecraft:flint'
Recipe 'stack_downgrade_tier_1_upgrade' @('SFS', 'SBS', 'FSF') ([ordered]@{ S = $stick; F = $flint; B = $B })
Recipe 'stack_downgrade_tier_2_upgrade' @('FSF', 'SBS', 'FSF') ([ordered]@{ F = $flint; S = $stick; B = $B })
Recipe 'stack_downgrade_tier_3_upgrade' @('SFS', 'FBF', 'FSF') ([ordered]@{ S = $stick; F = $flint; B = $B })
Shaped 'stack_conversion_starter_to_tier_1' 'stack_conversion_starter_to_tier_1' 'stack_conversion_starter_to_tier_1' @(' I ', 'ILI', ' I ') ([ordered]@{ I = 'minecraft:iron_block'; L = $L }) $L
Around 'stack_conversion_starter_to_tier_2' 'minecraft:gold_block' 'stack_conversion_starter_to_tier_1'
Around 'stack_conversion_starter_to_tier_3' 'minecraft:diamond_block' 'stack_conversion_starter_to_tier_2'
Around 'stack_conversion_starter_to_tier_4' 'minecraft:netherite_block' 'stack_conversion_starter_to_tier_3'
Shaped 'stack_conversion_tier_1_to_tier_2' 'stack_conversion_tier_1_to_tier_2' 'stack_conversion_tier_1_to_tier_2' @('MMM', 'MLM', 'MMM') ([ordered]@{ M = 'minecraft:gold_block'; L = $L }) $L
Around 'stack_conversion_tier_1_to_tier_3' 'minecraft:diamond_block' 'stack_conversion_tier_1_to_tier_2'
Around 'stack_conversion_tier_1_to_tier_4' 'minecraft:netherite_block' 'stack_conversion_tier_1_to_tier_3'
Shaped 'stack_conversion_tier_2_to_tier_3' 'stack_conversion_tier_2_to_tier_3' 'stack_conversion_tier_2_to_tier_3' @('MMM', 'MLM', 'MMM') ([ordered]@{ M = 'minecraft:diamond_block'; L = $L }) $L
Around 'stack_conversion_tier_2_to_tier_4' 'minecraft:netherite_block' 'stack_conversion_tier_2_to_tier_3'
Shaped 'stack_conversion_tier_3_to_tier_4' 'stack_conversion_tier_3_to_tier_4' 'stack_conversion_tier_3_to_tier_4' @('MMM', 'MLM', 'MMM') ([ordered]@{ M = 'minecraft:netherite_block'; L = $L }) $L

# ---- backpack tiers ------------------------------------------------------------------------------
function TierUp([string]$id, [string]$from, [string]$material, [string]$result, [string[]]$pattern = @('MMM', 'MBM', 'MMM')) {
    Shaped "tiers/$id" $id $result $pattern ([ordered]@{ M = $material; B = "${from}_backpack" }) "${from}_backpack"
}
TierUp 'copper_backpack' 'leather' 'minecraft:copper_ingot' 'copper_backpack'
TierUp 'iron_backpack' 'leather' $I 'iron_backpack'
TierUp 'iron_backpack_from_copper' 'copper' $I 'iron_backpack' @(' M ', 'MBM', ' M ')
TierUp 'gold_backpack' 'iron' $G 'gold_backpack'
TierUp 'diamond_backpack' 'gold' $D 'diamond_backpack'
Save-Recipe 'tiers/netherite_backpack' ([ordered]@{
    format_version                        = '1.20.10'
    'minecraft:recipe_smithing_transform' = [ordered]@{
        description = @{ identifier = "${ns}:netherite_backpack" }
        tags        = @('smithing_table')
        template    = 'minecraft:netherite_upgrade_smithing_template'
        base        = "${ns}:diamond_backpack"
        addition    = 'minecraft:netherite_ingot'
        result      = "${ns}:netherite_backpack"
    }
})

# ---- dyeing --------------------------------------------------------------------------------------
foreach ($tier in 'leather', 'copper', 'iron', 'gold', 'diamond', 'netherite') {
    for ($dyes = 1; $dyes -le 8; $dyes++) {
        $ingredients = @([ordered]@{ item = "${ns}:${tier}_backpack" })
        for ($n = 0; $n -lt $dyes; $n++) { $ingredients += [ordered]@{ tag = "${ns}:dye" } }
        Save-Recipe "dye/${tier}_backpack_dye_$dyes" ([ordered]@{
            format_version               = '1.20.10'
            'minecraft:recipe_shapeless' = [ordered]@{
                description = @{ identifier = "${ns}:${tier}_backpack_dye_$dyes" }
                tags        = @('crafting_table')
                ingredients = $ingredients
                unlock      = @([ordered]@{ item = "${ns}:${tier}_backpack" })
                result      = [ordered]@{ item = "${ns}:${tier}_backpack" }
            }
        })
    }
}

[IO.File]::WriteAllText($manifestPath, (($written | Sort-Object) -join "`n") + "`n")
Write-Host "wrote $($written.Count) recipes to $out"
