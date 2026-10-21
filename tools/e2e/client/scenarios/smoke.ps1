# Smoke: the mod is enabled (run.ps1 waited for "AlleviatedBackpacks enabled"), the player is in
# the ABP-Test world, and the core selftests pass. Screenshots: out/smoke/{hud,inventory,selftest}.png
Save-Screenshot 'hud' | Out-Null
Invoke-SelfTest 'core.*' | Out-Null
Save-Screenshot 'selftest' | Out-Null     # chat shows the SELFTEST lines
Open-Inventory
Save-Screenshot 'inventory' | Out-Null
Close-Screen
