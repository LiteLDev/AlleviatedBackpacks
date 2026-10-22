# P2-14: put back the options input.prelaunch.ps1 changed.
# Every instance (slot clones too): only files with our backup next to them are touched.
$opts = Get-ChildItem "$env:APPDATA\levilauncher.exe\versions\*\Minecraft Bedrock\Users\*\games\com.mojang\minecraftpe\options.txt" -ErrorAction SilentlyContinue
foreach ($f in $opts) {
    $backup = "$($f.FullName).abp-input-backup"
    if (Test-Path $backup) {
        Remove-Item $f.FullName -Force
        Move-Item $backup $f.FullName -Force
        Write-Host "[e2e] input: restored $($f.FullName)"
    }
}
