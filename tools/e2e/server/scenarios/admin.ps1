# Admin unit (U20): admin commands, config rules, keep-on-death.
$r = Invoke-SelfTest 'admin.*'
if ($r.Fail -gt 0 -or $r.Pass -lt 12) { throw "admin selftests: pass=$($r.Pass) fail=$($r.Fail)" }

# The real commands from the console, asserting on their log lines.
$mark = Get-LogMark
Send-Console 'backpack stats'
Wait-LogLine 'admin: stats stored=\d+' 10 -Since $mark | Out-Null

$mark = Get-LogMark
Send-Console 'backpack list'
Wait-LogLine 'admin: list -> \d+ backpacks' 10 -Since $mark | Out-Null

$mark = Get-LogMark
Send-Console 'backpack cleanup'
Wait-LogLine 'admin: cleanup -> \d+' 10 -Since $mark | Out-Null

$mark = Get-LogMark
Send-Console 'backpack removenonplayer'
Wait-LogLine 'admin: removenonplayer -> \d+' 10 -Since $mark | Out-Null

# Unknown / too short uuids are rejected without errors in the log.
Send-Console 'backpack remove 00000000-0000-4000-8000-000000000000'
Send-Console 'backpack info ab'
Wait-Seconds 1

$mark = Get-LogMark
Send-Console 'backpack reload'
Wait-LogLine 'configuration reloaded' 10 -Since $mark | Out-Null

# The config file exists with its defaults.
$cfg = Join-Path $script:SlotDir 'plugins\AlleviatedBackpacks\config\admin.json'
if (-not (Test-Path $cfg)) { throw "missing $cfg" }
$json = Get-Content $cfg -Raw | ConvertFrom-Json
if ($json.keepBackpacksOnDeath -ne $false) { throw 'keepBackpacksOnDeath must default to false' }
if ($json.commandPermissionLevel -ne 1) { throw 'commandPermissionLevel must default to 1' }
