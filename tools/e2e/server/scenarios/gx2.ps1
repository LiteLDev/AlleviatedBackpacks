# GX-2 (upgrade and world behaviour gaps): alchemy's loop and splash, Everlasting vs. Wither /
# Ender Dragon, mob catcher with linked storage, mob conversions and creepers, the Ender Linker
# crafting recipe, Inception's combined inventory, jukebox mobs, compacting remainders. The suites
# of the touched units run whole (their older tests guard against regressions).
# `$env:ABP_GX2_GLOBS` ("a.*,b.*") narrows the run.
$globs = @('alchemy.*', 'everlasting.*', 'mobcatcher.*', 'wearable.*', 'linked.*', 'inception.*', 'compacting.*', 'jukebox.*', 'pickup.*', 'restock.*', 'deposit.*', 'stash.*', 'cooking.*', 'placed.*')
if ($env:ABP_GX2_GLOBS) { $globs = $env:ABP_GX2_GLOBS -split ',' }
foreach ($g in $globs) {
    $r = Invoke-SelfTest $g -TimeoutSeconds 900
    if ($r.Fail -gt 0) { Write-Host "FAILED in ${g}: $($r.Failed | ForEach-Object { "$($_.Name): $($_.Reason)" })" }
}
