# Showcase media: a natural world instead of the flat test world. Turns the ABP-Test fixture
# (already copied to the worlds folder) into a normal ("infinite") world with a fixed seed, no
# coordinates overlay, daytime. The world is deleted after the run as usual.
. "$PSScriptRoot/../../common/nbt.ps1"
$level = Join-Path (Join-Path (Get-WorldsDir) $script:WorldName) 'level.dat'
$ld = Read-LevelDat $level
$seed = if ($env:ABP_SHOWCASE_SEED) { [long]$env:ABP_SHOWCASE_SEED } else { 20260521L }
Set-NbtValue $ld.Root 'Generator' 3 1            # 1 = infinite (normal terrain), 2 = flat
Set-NbtValue $ld.Root 'RandomSeed' 4 $seed
Set-NbtValue $ld.Root 'showcoordinates' 1 ([byte]0)
Set-NbtValue $ld.Root 'Time' 4 6000L
Write-LevelDat $ld $level
Write-Step "showcase world: normal terrain, seed $seed"

