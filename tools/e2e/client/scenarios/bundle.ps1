# Bundles and SB `canBePlacedInContainerItems` with a real client (`-Mode local` and `-Mode remote-mod`):
#   1. default (false): a backpack on the cursor right-clicked onto a bundle stays out of it, and the
#      client predicts the refusal (its bundle validation asks the rule the server announced);
#   2. `canBePlacedInContainerItems = true` written to the config file + `/backpack reload`: the
#      client is told the new rule and the same click puts the backpack into the bundle (it fills
#      the bundle: weight 64 of 64).
# The backpack is full, so SB right-click stashing has nothing to offer and the click reaches the
# bundle. Positions come from the mod's "stash: vanilla slots ..." line.

Wait-LogLine 'companion: server ready' 90 | Out-Null

$script:V = $null
function Read-VanillaSlots($mark) {
    Wait-LogLine 'stash: vanilla slots screen=' 15 -Since $mark | Out-Null
    Start-Sleep -Milliseconds 1500
    $line = (Get-LeviLog -Since $mark | Select-String -Pattern 'stash: vanilla slots screen=' | Select-Object -Last 1).Line
    if ($line -notmatch 'screen=(\S*) invGuiScale=([\d.]+)') { throw "unexpected slots line: $line" }
    $slots = @{}
    foreach ($m in [regex]::Matches($line, '([\w.]+:\d+)=(-?[\d.]+),(-?[\d.]+),([\d.]+)')) {
        $slots[$m.Groups[1].Value] = @([double]$m.Groups[2].Value, [double]$m.Groups[3].Value, [double]$m.Groups[4].Value)
    }
    $script:V = [pscustomobject]@{ Screen = $Matches[1]; Inv = [double]$Matches[2]; Slots = $slots }
    Write-Step ("vanilla screen {0}: {1}" -f $V.Screen, (($slots.Keys | Sort-Object) -join ' '))
}
function V-Pos([string]$key) {
    if (-not $V.Slots.ContainsKey($key)) { throw "vanilla slot $key not seen on $($V.Screen)" }
    $r = $V.Slots[$key]
    @((($r[0] + 8 * $r[2]) / $V.Inv), (($r[1] + 8 * $r[2]) / $V.Inv))
}
function V-Click([string]$key, [switch]$Right) {
    $p = V-Pos $key; Move-Mouse ($p[0] + 3) ($p[1] + 3); Start-Sleep -Milliseconds 150; Move-Mouse $p[0] $p[1]; Start-Sleep -Milliseconds 250
    $b = if ($Right) { 1 } else { 0 }
    [AbpE2E.Native]::MouseButton($b, $false); Start-Sleep -Milliseconds 200; [AbpE2E.Native]::MouseButton($b, $true); Start-Sleep -Milliseconds 600
}
function Invoke-Checked([string]$command, [string]$answer, [int]$timeout = 10) {
    for ($attempt = 1; $attempt -le 3; $attempt++) {
        $mark = Get-LogMark
        Invoke-Chat $command
        try { return (Wait-LogLine $answer $timeout -Since $mark) } catch {
            Write-Step "$command : no answer (attempt $attempt)"
            Press-Key ESC
            Start-Sleep -Milliseconds 1500
        }
    }
    throw "$command got no answer matching '$answer'"
}
function Get-BundleStatus {
    $line = Invoke-Checked '/backpack bundle status' 'bundle status: weight='
    if ($line -notmatch 'weight=(\d+) backpacks=(\d+)') { throw "unexpected status: $line" }
    [pscustomobject]@{ Weight = [int]$Matches[1]; Backpacks = [int]$Matches[2]; Line = $line }
}
# Backpack (inventory:1) onto the cursor, right-click on the bundle (inventory:0), then the
# screen is closed (the cursor item goes back into the inventory).
function Try-Insert([string]$shot) {
    Invoke-Checked '/backpack bundle setup' 'bundle setup: backpack=' | Out-Null
    Start-Sleep -Milliseconds 800
    $mark = Get-LogMark
    Open-Inventory
    Read-VanillaSlots $mark
    $mark = Get-LogMark
    V-Click 'inventory:1'
    V-Click 'inventory:0' -Right
    Start-Sleep -Milliseconds 800
    Save-Screenshot $shot | Out-Null
    $script:ClickLog = @(Get-LeviLog -Since $mark)
    Close-Screen
    Start-Sleep -Milliseconds 800
    Get-BundleStatus
}

Invoke-Chat '/gamemode survival'
Wait-LogLine 'bundle: server rule canBePlacedInContainerItems=false' 30 | Out-Null

# ---- 1. default: refused ----------------------------------------------------------------------------
$s = Try-Insert 'refused'
Write-Step "default: $($s.Line)"
if ($s.Weight -ne 0 -or $s.Backpacks -ne 1) { throw "the backpack went into the bundle with canBePlacedInContainerItems=false: $($s.Line)" }
if (-not ($ClickLog -match 'bundle: client refuses alleviatedbackpacks:leather_backpack')) { throw 'the client did not predict the refusal' }

# ---- 2. config true + /backpack reload: allowed -------------------------------------------------------
$mark = Get-LogMark
Invoke-Checked '/backpack bundle allow' 'bundle file: canBePlacedInContainerItems=true' | Out-Null
Invoke-Checked '/backpack reload' 'configuration reloaded|bundle: server rule' | Out-Null
Wait-LogLine 'bundle: server rule canBePlacedInContainerItems=true' 15 -Since $mark | Out-Null
$s = Try-Insert 'allowed'
Write-Step "allowed: $($s.Line)"
if ($s.Weight -ne 64 -or $s.Backpacks -ne 0) { throw "the backpack did not go into the bundle with canBePlacedInContainerItems=true: $($s.Line)" }
if (-not ($ClickLog -match 'bundle: client allows alleviatedbackpacks:leather_backpack')) { throw 'the client did not predict the insert' }
Open-Inventory
Start-Sleep -Milliseconds 500
Save-Screenshot 'allowed-inventory' | Out-Null
Close-Screen

# ---- back to the default ----------------------------------------------------------------------------
$mark = Get-LogMark
Invoke-Checked '/backpack bundle refuse' 'bundle file: canBePlacedInContainerItems=false' | Out-Null
Invoke-Checked '/backpack reload' 'configuration reloaded|bundle: server rule' | Out-Null
Wait-LogLine 'bundle: server rule canBePlacedInContainerItems=false' 15 -Since $mark | Out-Null
