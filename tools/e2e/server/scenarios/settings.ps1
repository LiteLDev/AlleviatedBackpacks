# P2-2: SB backpack settings (main / item display / no sort / memory, templates persistence) and
# backpack templates (`/sbp template ...`, template packs, dynamic templates).
$r = Invoke-SelfTest 'sbsettings.*'
if ($r.Pass -lt 5 -or $r.Fail -gt 0) { throw "sbsettings selftests: pass=$($r.Pass) fail=$($r.Fail)" }
$r = Invoke-SelfTest 'templates.*'
if ($r.Pass -lt 3 -or $r.Fail -gt 0) { throw "templates selftests: pass=$($r.Pass) fail=$($r.Fail)" }
# The phase-1 GUI and the companion screen actions still pass with the SB settings.
$r = Invoke-SelfTest 'gui.*'
if ($r.Fail -gt 0) { throw "gui selftests failed" }
$r = Invoke-SelfTest 'companion.*'
if ($r.Fail -gt 0) { throw "companion selftests failed" }

# Commands from the console (operator): list, dynamic templates, errors.
$mark = Get-LogMark
Send-Console 'sbp template list'
Wait-LogLine 'Template Name, Actions' 15 -Since $mark | Out-Null
Wait-LogLine 'Local' 15 -Since $mark | Out-Null
$mark = Get-LogMark
Send-Console 'sbp template dynamic begin e2e_kit alleviatedbackpacks:iron_backpack'
Wait-LogLine "Successfully started a dynamic template 'e2e_kit'" 15 -Since $mark | Out-Null
$mark = Get-LogMark
Send-Console 'sbp template dynamic begin e2e_kit alleviatedbackpacks:iron_backpack'
Wait-LogLine "The dynamic template name 'e2e_kit' is currently in use" 15 -Since $mark | Out-Null
$mark = Get-LogMark
Send-Console 'sbp template dynamic additem e2e_kit 3 diamond 5'
Wait-LogLine "Successfully added item '.*' to 'e2e_kit'" 15 -Since $mark | Out-Null
$mark = Get-LogMark
Send-Console 'sbp template dynamic addItem e2e_kit apple 10'
Wait-LogLine "is marked to be added to 'e2e_kit' at the end" 15 -Since $mark | Out-Null
$mark = Get-LogMark
Send-Console 'sbp template dynamic addupgrade e2e_kit alleviatedbackpacks:magnet_upgrade'
Wait-LogLine "is marked to be added to 'e2e_kit' at the end" 15 -Since $mark | Out-Null
$mark = Get-LogMark
Send-Console 'sbp template dynamic end e2e_kit'
Wait-LogLine "Successfully ended the dynamic template 'e2e_kit'" 15 -Since $mark | Out-Null
$mark = Get-LogMark
Send-Console 'sbp template dynamic end e2e_kit'
Wait-LogLine "The dynamic template 'e2e_kit' is currently not in use" 15 -Since $mark | Out-Null
$mark = Get-LogMark
Send-Console 'sbp template give nothing:here'
Wait-LogLine "The template 'nothing:here' does not exist" 15 -Since $mark | Out-Null
$mark = Get-LogMark
Send-Console 'sophisticatedbackpacks template delete nothing:here'
Wait-LogLine "Template 'nothing:here' deleted successfully" 15 -Since $mark | Out-Null
