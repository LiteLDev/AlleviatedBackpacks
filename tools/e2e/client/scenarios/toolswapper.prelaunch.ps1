# The scenario looks down at a block and mines it: make sure the camera is in first person (the
# perspective is a persisted client option that an earlier session may have left in third person).
$opts = Get-ChildItem "$env:APPDATA\levilauncher.exe\versions\1.26.51.01\Minecraft Bedrock\Users" -Recurse -Filter options.txt -ErrorAction SilentlyContinue
foreach ($f in $opts) {
    $text = Get-Content -Raw $f.FullName
    if ($text -match 'game_thirdperson:[1-9]') {
        [System.IO.File]::WriteAllText($f.FullName, ($text -replace 'game_thirdperson:\d+', 'game_thirdperson:0'))
        Write-Host "[e2e] reset camera perspective to first person in $($f.FullName)"
    }
}
