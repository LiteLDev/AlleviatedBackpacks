# Chest GUI switching on the real client (menu framework): install three upgrades with normal
# clicks (their buttons must appear without a view switch), then open each upgrade's settings menu
# from the backpack, change a toggle and go Back, five rounds in a row; chat must keep working.
# menus.prelaunch.ps1 turns on the packet log (config/menu.json debugPackets).
Invoke-Chat '/clear @s'
Invoke-Chat '/replaceitem entity @s slot.hotbar 0 alleviatedbackpacks:diamond_backpack'
Invoke-Chat '/replaceitem entity @s slot.hotbar 1 alleviatedbackpacks:void_upgrade'
Invoke-Chat '/replaceitem entity @s slot.hotbar 2 alleviatedbackpacks:pickup_upgrade'
Invoke-Chat '/replaceitem entity @s slot.hotbar 3 alleviatedbackpacks:compacting_upgrade'
Press-Key '1'
Start-Sleep -Seconds 1

# Fractions of the 1600x900 client area. Slots are 0.035 wide and 0.0622 high.
function Slot-Double([int]$i) { @((0.365 + ($i % 9) * 0.035), (0.232 + [Math]::Floor($i / 9) * 0.0622)) }
function Hotbar-Double([int]$i) { @((0.365 + $i * 0.035), 0.819) }
function Slot-Single([int]$i) { @((0.365 + ($i % 9) * 0.035), (0.325 + [Math]::Floor($i / 9) * 0.0622)) }
function Click-At($p) {
    Click $p[0] $p[1] -Relative
    Start-Sleep -Milliseconds 500
}

# True when a chest screen is drawn: the middle of the window is mostly the neutral gray of the
# container UI (the world view behind it is sky / grass).
Add-Type -AssemblyName System.Drawing
function Test-ChestScreen([string]$path) {
    $bmp = [System.Drawing.Bitmap]::FromFile($path)
    try {
        $n = 0; $gray = 0
        for ($y = [int]($bmp.Height * 0.30); $y -lt [int]($bmp.Height * 0.70); $y += 6) {
            for ($x = [int]($bmp.Width * 0.36); $x -lt [int]($bmp.Width * 0.64); $x += 6) {
                $c = $bmp.GetPixel($x, $y); $n++
                if ([Math]::Abs($c.R - $c.G) -le 5 -and [Math]::Abs($c.G - $c.B) -le 5 -and $c.R -ge 120 -and $c.R -le 210) { $gray++ }
            }
        }
        return ($gray / $n) -gt 0.5
    } finally { $bmp.Dispose() }
}
# Screenshot + check that the chest screen is really there.
function Assert-Screen([string]$shot, [string]$what) {
    $path = Save-Screenshot $shot
    if (-not (Test-ChestScreen $path)) { throw "$what is not shown on the client (see $shot.png)" }
}
function Wait-Shown([string]$pattern, $mark, [string]$shot) {
    Wait-LogLine "menu shown: $pattern" 10 -Since $mark | Out-Null
    Start-Sleep -Milliseconds 700
    Assert-Screen $shot "menu '$pattern'"
}
function Assert-Chat([string]$tag) {
    $mark = Get-LogMark
    Invoke-Chat '/backpack info'
    Wait-LogLine 'backpack info: ' 10 -Since $mark | Out-Null
    Write-Step "chat works ($tag)"
}

# 1. Open the backpack, upgrades page.
$mark = Get-LogMark
Click 0.5 0.5 -Relative -Right
Wait-Shown 'Diamond Backpack \(54 slots\)' $mark 'backpack'
Click-At (Slot-Double 52)
Start-Sleep -Milliseconds 300
Save-Screenshot 'upgrades-empty' | Out-Null

# 2. Normal click: pick each upgrade up from the hotbar and put it into upgrade slot u. Its
#    settings button (slot 18 + u) must be synced right away, without switching views.
for ($u = 0; $u -lt 3; $u++) {
    $mark = Get-LogMark
    Click-At (Hotbar-Double ($u + 1))
    Click-At (Slot-Double $u)
    Wait-LogLine "\[menu-dbg\] sync id=\d+ .*\b$(18 + $u):(minecraft:)?repeater" 5 -Since $mark | Out-Null
    Start-Sleep -Milliseconds 300
    Save-Screenshot "installed-$u" | Out-Null
}

# 3. Settings -> toggle -> Back, for every upgrade, five rounds.
for ($round = 1; $round -le 5; $round++) {
    for ($u = 0; $u -lt 3; $u++) {
        $mark = Get-LogMark
        Click-At (Slot-Double (18 + $u))
        Wait-Shown '.* \(27 slots\)' $mark "r$round-u$u-settings"
        # Control row slot 0 (menu slot 18): allow / deny list.
        $mark = Get-LogMark
        Click-At (Slot-Single 18)
        Wait-LogLine '\[menu-dbg\] sync id=\d+ .*\b18:(minecraft:)?(white|black)_wool' 5 -Since $mark | Out-Null
        if ($round -eq 1) { Start-Sleep -Milliseconds 300; Save-Screenshot "r$round-u$u-toggled" | Out-Null }
        # Back (control slot 8 = menu slot 26) returns to the backpack's upgrades page.
        $mark = Get-LogMark
        Click-At (Slot-Single 26)
        Wait-Shown 'Diamond Backpack \(54 slots\)' $mark "r$round-u$u-back"
    }
}

# 4. Chat after the menus, then the whole round trip once more after a chat command.
Close-Screen
Assert-Chat 'after 5 rounds'
$mark = Get-LogMark
Click 0.5 0.5 -Relative -Right
Wait-Shown 'Diamond Backpack \(54 slots\)' $mark 'after-chat-backpack'
Click-At (Slot-Double 52)
$mark = Get-LogMark
Click-At (Slot-Double 19)
Wait-Shown '.* \(27 slots\)' $mark 'after-chat-settings'
$mark = Get-LogMark
Click-At (Slot-Single 26)
Wait-Shown 'Diamond Backpack \(54 slots\)' $mark 'after-chat-back'

# 5. Removing an upgrade by hand refreshes the page too (its buttons go away).
$mark = Get-LogMark
Click-At (Slot-Double 2)
Wait-LogLine '\[menu-dbg\] sync id=\d+ .*\b20:(minecraft:)?gray_stained_glass_pane' 5 -Since $mark | Out-Null
Start-Sleep -Milliseconds 300
Save-Screenshot 'removed' | Out-Null
Click-At (Hotbar-Double 3)   # put the upgrade back into the hotbar
Close-Screen
Assert-Chat 'at the end'
Assert-NoErrors
