# Full: every selftest the mod registers.
Invoke-SelfTest -TimeoutSeconds 1800 | Out-Null
Save-Screenshot 'selftest' | Out-Null
