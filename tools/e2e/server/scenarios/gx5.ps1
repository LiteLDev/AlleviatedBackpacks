# GX-5 parity gaps: SB upgrade limits, piglins vs gold backpacks, dispensers placing backpacks,
# empty storages dropped on load, everlasting vs explosion knock-back, SB config keys, tag
# namespaces, template leftovers.
foreach ($glob in @(
        'admin.sb_upgrade_limits', 'admin.max_upgrades', 'admin.drop_empty_on_load',
        'crafting.two_upgrades_independent', 'stonecutter.two_upgrades_independent',
        'cooking.info', 'cooking.one_per_backpack', 'stack.items_registered', 'jukebox.*',
        'mainscreen.error_overlay', 'companion.upgrade_slots',
        'wearable.piglin_neutral', 'placed.dispenser_*', 'everlasting.explosion_knockback',
        'alchemy.config_keys_migrate', 'alchemy.filter_slots', 'filter.*', 'templates.*')) {
    Invoke-SelfTest $glob -TimeoutSeconds 600 | Out-Null
}
