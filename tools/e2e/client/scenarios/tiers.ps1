# U1 tiers (client): selftests in a local world, then real crafting with real input:
#   1. dye a filled leather backpack through the inventory recipe book (2x2 grid),
#   2. upgrade it to copper in a crafting table,
# checking with `/backpack info` that the uuid and contents survive, plus lore/tint screenshots.
$r = Invoke-SelfTest 'tiers.*'
if ($r.Fail -gt 0 -or $r.Pass -lt 7) { throw "tiers selftests: pass=$($r.Pass) fail=$($r.Fail)" }

function Get-BackpackInfo {
    $mark = Get-LogMark
    Invoke-Chat '/backpack info'
    $line = Wait-LogLine 'backpack info: ' 10 -Since $mark
    if ($line -notmatch 'uuid=(\S+) tier=(\S+) slots=(\d+) items=(\d+)') { throw "bad info line: $line" }
    return [pscustomobject]@{ Uuid = $Matches[1]; Tier = $Matches[2]; Slots = [int]$Matches[3]; Items = [int]$Matches[4] }
}

Invoke-Chat '/clear @s'
Invoke-Chat '/replaceitem entity @s slot.hotbar 0 alleviatedbackpacks:leather_backpack'
Invoke-Chat '/replaceitem entity @s slot.hotbar 1 copper_ingot 8'
Invoke-Chat '/replaceitem entity @s slot.hotbar 2 red_dye 1'
Invoke-Chat '/replaceitem entity @s slot.hotbar 3 diamond 20'
Invoke-Chat '/setblock ~ ~-1 ~2 crafting_table'
Invoke-Chat '/tp @s ~ ~ ~ facing ~ ~-0.5 ~2'
Press-Key '1'
Start-Sleep -Seconds 1

# Fill the backpack: open it, shift-click the diamonds in (chest layout, hotbar slot 4).
$mark = Get-LogMark
Click 0.5 0.5 -Relative -Right
Wait-LogLine 'menu shown: Leather Backpack' 10 -Since $mark | Out-Null
Start-Sleep -Milliseconds 800
[AbpE2E.Native]::Key(0x10, $false)
Click 0.469 0.819 -Relative
[AbpE2E.Native]::Key(0x10, $true)
Start-Sleep -Milliseconds 800
Close-Screen
$before = Get-BackpackInfo
if ($before.Items -ne 20) { throw "expected 20 items in the backpack, got $($before.Items)" }
Start-Sleep -Seconds 2

# 1. Dye: inventory screen, recipe book entry for "leather backpack + red dye", take the result.
Press-Key '6'
Open-Inventory
Move-Mouse 0.506 0.729 -Relative; Start-Sleep -Milliseconds 800
Save-Screenshot 'lore-before' | Out-Null
$mark = Get-LogMark
Click 0.365 0.439 -Relative; Start-Sleep -Milliseconds 800
Save-Screenshot 'dye-grid' | Out-Null
Click 0.778 0.340 -Relative; Start-Sleep -Milliseconds 800
Click 0.506 0.729 -Relative; Start-Sleep -Milliseconds 800
Save-Screenshot 'dye-crafted' | Out-Null
Close-Screen
Wait-LogLine 'backpack craft: .* carried into the crafted backpack \(dyed\)' 10 -Since $mark | Out-Null
Start-Sleep -Seconds 2
Open-Inventory
Move-Mouse 0.506 0.729 -Relative; Start-Sleep -Milliseconds 800
Save-Screenshot 'lore-dyed' | Out-Null
Close-Screen
Press-Key '1'
Start-Sleep -Milliseconds 500
Save-Screenshot 'hud-dyed' | Out-Null
$dyed = Get-BackpackInfo
if ($dyed.Uuid -ne $before.Uuid -or $dyed.Items -ne 20 -or $dyed.Tier -ne 'leather') { throw "dyeing lost the backpack: $($dyed | Out-String)" }

# 2. Tier upgrade in a crafting table: search the recipe book for the copper backpack.
Press-Key '6'
Invoke-Chat '/tp @s ~ ~ ~ 0 40'
Start-Sleep -Milliseconds 500
Click 0.5 0.5 -Relative -Right
Start-Sleep -Seconds 2
Save-Screenshot 'table-open' | Out-Null
$mark = Get-LogMark
Click 0.30 0.31 -Relative; Start-Sleep -Milliseconds 500
Type-Text 'copper backpack'
Start-Sleep -Milliseconds 800
Save-Screenshot 'table-search' | Out-Null
Click 0.230 0.378 -Relative; Start-Sleep -Milliseconds 800
Save-Screenshot 'table-grid' | Out-Null
Click 0.730 0.368 -Relative; Start-Sleep -Milliseconds 800
Save-Screenshot 'table-output' | Out-Null
Click 0.506 0.729 -Relative; Start-Sleep -Milliseconds 800
Save-Screenshot 'table-crafted' | Out-Null
Close-Screen
Press-Key '1'
Start-Sleep -Seconds 2
Save-Screenshot 'hud-copper' | Out-Null
$copper = Get-BackpackInfo
Save-Screenshot 'info-copper' | Out-Null
if ($copper.Uuid -ne $before.Uuid -or $copper.Items -ne 20 -or $copper.Tier -ne 'copper' -or $copper.Slots -ne 45) {
    throw "tier upgrade did not keep the backpack: $($copper | Out-String)"
}
Assert-NoErrors