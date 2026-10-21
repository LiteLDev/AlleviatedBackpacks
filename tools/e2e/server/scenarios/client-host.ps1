# Host for the client harness's remote modes (tools/e2e/client/run.ps1 -Mode remote-mod |
# remote-vanilla). Not a test on its own: it keeps the server running for a real game client.
#
# Contract with the client harness, through the folder in $env:ABP_E2E_HOST_DIR:
#   ready.json      written once the server is up: {"port": <udp port>, "slot": n}
#   cmd-*.txt       a console command to run (processed in name order, then deleted)
#   stop            the client is done: stop the server
$hostDir = $env:ABP_E2E_HOST_DIR
if (-not $hostDir) { throw 'client-host needs $env:ABP_E2E_HOST_DIR' }
New-Item -ItemType Directory -Force $hostDir | Out-Null
@{ port = $script:ServerPort; slot = $script:Slot } | ConvertTo-Json | Set-Content (Join-Path $hostDir 'ready.json') -Encoding UTF8
Write-Step "client host ready on port $script:ServerPort ($hostDir)"
$until = (Get-Date).AddHours(2)
while (-not (Test-Path (Join-Path $hostDir 'stop'))) {
    if (-not (Test-ServerRunning)) { throw 'the server exited while hosting the client' }
    if ((Get-Date) -gt $until) { throw 'client host timed out (2 h)' }
    foreach ($cmd in Get-ChildItem $hostDir -Filter 'cmd-*.txt' -ErrorAction SilentlyContinue | Sort-Object Name) {
        $text = (Get-Content $cmd.FullName -Raw).Trim()
        Remove-Item $cmd.FullName -Force
        if ($text) { Send-Console $text }
    }
    Wait-Seconds 1
}
Write-Step 'client done; stopping the host'
