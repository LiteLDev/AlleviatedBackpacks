# Native screen keys: ESC and the inventory key (E) close the native backpack screen (SB), with no
# vanilla side effect (no emote wheel, no inventory screen left open), and a tab icon click opens
# the tab once (one press = one toggle). Works in `-Mode local` and `-Mode remote-mod`.

Wait-LogLine 'companion: server ready' 90 | Out-Null
Write-Step 'companion handshake done'

Invoke-Chat '/clear @s'
$mark = Get-LogMark
Invoke-Chat '/backpack stations setup'
Wait-LogLine 'stations setup: gave backpacks' 15 -Since $mark | Out-Null

function Open-Backpack {
    Start-Sleep -Milliseconds 800
    Press-Key '1'
    Start-Sleep -Milliseconds 600
    $m = Get-LogMark
    Press-Key 'B'
    Wait-LogLine "companion: native screen \d+ for 'Diamond Backpack'" 10 -Since $m | Out-Null
    $line = Wait-LogLine 'companion: screen layout session=' 15 -Since $m
    Start-Sleep -Milliseconds 700
    if ($line -notmatch 'left=([\d.]+) top=([\d.]+) width=(\d+) .*invGuiScale=([\d.]+)') { throw "unexpected layout line: $line" }
    return [pscustomobject]@{ Left = [double]$Matches[1]; Top = [double]$Matches[2]; Width = [int]$Matches[3]; Inv = [double]$Matches[4] }
}
foreach ($key in 'ESC', 'E') {
    $L = Open-Backpack
    Save-Screenshot "open-before-$key" | Out-Null
    $m = Get-LogMark
    Press-Key $key
    Wait-LogLine 'native screen action: close by' 10 -Since $m | Out-Null
    Start-Sleep -Milliseconds 1500
    Save-Screenshot "after-$key" | Out-Null
    Write-Step "$key closed the native screen"
}

# One click on the crafting tab icon (tab 1) opens it and it stays open.
$L = Open-Backpack
$m = Get-LogMark
Click (($L.Left + $L.Width + 1 + 8) / $L.Inv) (($L.Top + 4 + 25 + 4 + 8) / $L.Inv)
Start-Sleep -Milliseconds 1200
Save-Screenshot 'tab-clicked' | Out-Null
$tabLines = @(Get-LeviLog -Since $m | Where-Object { $_ -match 'native screen action: tab by' })
Write-Step "tab actions after one click: $($tabLines.Count)"
if ((Get-LeviLog -Since $m) -match 'station tab: \w+ closed') { throw 'the tab closed again right after one click' }
Press-Key 'ESC'
Start-Sleep -Milliseconds 800
