# Smoke: the mod is enabled (run.ps1 already waited for "AlleviatedBackpacks enabled") and the
# core selftests pass.
Invoke-SelfTest 'core.*' | Out-Null
