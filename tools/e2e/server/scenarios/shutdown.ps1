# Server stop with open backpack menus that hold a cursor item (MenuManager's shutdown path):
# one simulated player with room in its inventory, one with a full inventory. run.ps1 then sends
# `stop`; the run fails on a crash (logs\crash, ERR/FTL lines, non-zero exit). The shutdown path
# logs where each cursor item went ("menu shutdown: cursor ... -> inventory|backpack|cursor").
Invoke-SelfTest 'menu.shutdown_keeps_cursor' | Out-Null
Send-Console 'backpack menudebug shutdownprobe'
Wait-LogLine 'shutdown probe: ready player=abp_shutdown_probe ' 15 | Out-Null
Send-Console 'backpack menudebug shutdownprobe full'
Wait-LogLine 'shutdown probe: ready player=abp_shutdown_probe_full ' 15 | Out-Null
Wait-Seconds 2
