# Builds AlleviatedBackpacks for the server (BDS), the client, or both.
#
#   pwsh tools/build.ps1                    # both platforms, release
#   pwsh tools/build.ps1 -Target server     # one platform
#   pwsh tools/build.ps1 -Tests             # also build and run the unit tests (abp-tests)
#
# Each platform has its own build dir (build/<target>); outputs land in
#   bin/<target>/AlleviatedBackpacks/  (dll, pdb, manifest.json, lang/, behavior_packs/, resource_packs/)
# Copy that folder to `plugins/` (BDS) or `mods/` (LeviLauncher client).
param(
    [ValidateSet('server', 'client', 'all')][string]$Target = 'all',
    [ValidateSet('release', 'debug')][string]$Mode = 'release',
    [switch]$Tests
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/..").Path
Push-Location $root
try {
    # Fresh shells may not have the tools on PATH yet.
    $env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' + [Environment]::GetEnvironmentVariable('Path', 'User') + ';' + $env:Path
    if (-not (Get-Command clang-cl -ErrorAction SilentlyContinue)) {
        foreach ($dir in "$env:ProgramFiles\LLVM\bin", "${env:ProgramFiles(x86)}\LLVM\bin") {
            if (Test-Path (Join-Path $dir 'clang-cl.exe')) { $env:Path = "$dir;$env:Path"; break }
        }
    }
    if (-not (Get-Command xmake -ErrorAction SilentlyContinue)) { throw 'xmake not found on PATH' }
    git config --global core.longpaths true | Out-Null

    # xmake needs to be told about Visual Studio 18 (2026) explicitly.
    $vsArgs = @()
    foreach ($pf in $env:ProgramFiles, ${env:ProgramFiles(x86)}) {
        if ($pf -and (Test-Path (Join-Path $pf 'Microsoft Visual Studio\18'))) { $vsArgs = @('--vs=2026'); break }
    }

    $targets = if ($Target -eq 'all') { @('server', 'client') } else { @($Target) }
    foreach ($t in $targets) {
        Write-Host "==> configuring $t ($Mode)"
        xmake f -a x64 -m $Mode -p windows --target_type=$t -o "build/$t" @vsArgs -y
        if ($LASTEXITCODE) { throw "xmake config failed for $t" }
        Write-Host "==> building $t"
        xmake build -y AlleviatedBackpacks
        if ($LASTEXITCODE) { throw "build failed for $t" }
        $out = Join-Path $root "bin/$t/AlleviatedBackpacks"
        if (-not (Test-Path (Join-Path $out 'AlleviatedBackpacks.dll'))) { throw "missing output $out" }
        Write-Host "==> $t ready: $out"
    }
    if ($Tests) {
        Write-Host '==> unit tests'
        xmake build -y abp-tests
        if ($LASTEXITCODE) { throw 'abp-tests build failed' }
        xmake run abp-tests
        if ($LASTEXITCODE) { throw 'unit tests failed' }
    }
} finally {
    Pop-Location
}
