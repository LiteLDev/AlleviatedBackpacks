# P2-14: put back the options input.prelaunch.ps1 changed.
$opts = Get-ChildItem "$env:APPDATA\levilauncher.exe\versions\1.26.51.01\Minecraft Bedrock\Users" -Recurse -Filter options.txt -ErrorAction SilentlyContinue
foreach ($f in $opts) {
    $backup = "$($f.FullName).abp-input-backup"
    if (Test-Path $backup) {
        Copy-Item $backup $f.FullName -Force
        Remove-Item $backup -Force
        Write-Host "[e2e] input: restored $($f.FullName)"
    }
}
