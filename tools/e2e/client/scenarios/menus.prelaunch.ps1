# Turn on the chest GUI packet log (config/menu.json) so the menus scenario can check what the
# server sent and what the client received.
$cfgDir = Join-Path (Join-Path $script:ModsDir $script:ModName) 'config'
New-Item -ItemType Directory -Force $cfgDir | Out-Null
'{ "version": 1, "debugPackets": true }' | Set-Content -Path (Join-Path $cfgDir 'menu.json') -Encoding UTF8
