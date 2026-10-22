# Puts back every options.txt that misc_upgrades.prelaunch.ps1 changed in this slot's instance.
$state = Join-Path $env:TEMP "abp-e2e-misc-options-$script:VersionName"
$map = Join-Path $state 'map.txt'
if (-not (Test-Path $map)) { return }
foreach ($entry in Get-Content $map) {
    $backup, $file = $entry -split '\|', 2
    if ((Test-Path $backup) -and (Test-Path (Split-Path $file))) {
        [System.IO.File]::WriteAllText($file, [System.IO.File]::ReadAllText($backup))
        Write-Host "[e2e] restored $file"
    }
}
Remove-Item -Recurse -Force $state
