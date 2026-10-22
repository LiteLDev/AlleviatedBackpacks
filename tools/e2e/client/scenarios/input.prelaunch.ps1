# P2-14: bind the keys the scenario needs that SB leaves unbound (Tool Swap = V, Toggle Upgrade 3 =
# N) and add a second binding for Open Backpack on the mouse side button X1 (-96). The original
# options are put back by input.restore.ps1.
$opts = Get-ChildItem (Join-Path (Get-GameDataDir) "Users") -Recurse -Filter options.txt -ErrorAction SilentlyContinue  # this slot's instance
foreach ($f in $opts) {
    $backup = "$($f.FullName).abp-input-backup"
    if (-not (Test-Path $backup)) { Copy-Item $f.FullName $backup }
    $text = Get-Content -Raw $f.FullName
    $set = @{
        'AlleviatedBackpacks.open_backpack'    = '66,-96'
        'AlleviatedBackpacks.tool_swap'        = '86'
        'AlleviatedBackpacks.toggle_upgrade_3' = '78'
    }
    foreach ($k in $set.Keys) {
        $pattern = "(?m)^keyboard_type_0_key\.$([regex]::Escape($k)):.*$"
        $line    = "keyboard_type_0_key.$($k):$($set[$k])"
        if ($text -match $pattern) { $text = $text -replace $pattern, $line } else { $text = $text.TrimEnd() + "`n" + $line + "`n" }
    }
    # A slot clone may hard-link options.txt to the base instance: write a new file, never in place.
    Remove-Item $f.FullName -Force
    [System.IO.File]::WriteAllText($f.FullName, $text)
    Write-Host "[e2e] input: key bindings set in $($f.FullName)"
}
