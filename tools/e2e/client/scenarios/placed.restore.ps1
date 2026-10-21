# Puts back the camera perspective that placed.prelaunch.ps1 changed.
$state = Join-Path $env:TEMP 'abp-e2e-placed-camera.txt'
if (-not (Test-Path $state)) { return }
foreach ($entry in Get-Content $state) {
    $file, $old = $entry -split '\|', 2
    if (-not (Test-Path $file)) { continue }
    (Get-Content $file | ForEach-Object { if ($_ -like 'game_thirdperson:*') { $old } else { $_ } }) | Set-Content $file
}
Remove-Item $state
