# The parity scenario aims at blocks with the crosshair: force the first-person camera
# (options.txt game_thirdperson:0) in this slot's instance. parity.restore.ps1 puts the previous
# value back. The state file is per instance, so parallel runs on other slots don't mix them up.
$state = Join-Path $env:TEMP "abp-e2e-parity-camera-$script:VersionName.txt"
foreach ($opt in Get-ChildItem (Get-GameDataDir) -Recurse -Filter options.txt -ErrorAction SilentlyContinue) {
    $lines = Get-Content $opt.FullName
    $old = $lines | Where-Object { $_ -like 'game_thirdperson:*' } | Select-Object -First 1
    if (-not $old -or $old -eq 'game_thirdperson:0') { continue }
    Add-Content $state "$($opt.FullName)|$old"
    ($lines | ForEach-Object { if ($_ -like 'game_thirdperson:*') { 'game_thirdperson:0' } else { $_ } }) | Set-Content $opt.FullName
    Write-Host "[e2e] first-person camera for this run ($($opt.FullName), was $old)"
}
