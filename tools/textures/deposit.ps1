# Draws the Deposit / Advanced Deposit upgrade textures: a green arrow pointing into a chest.
. "$PSScriptRoot/deposit_restock_common.ps1"

$arrow = @(
    '................',
    '................',
    '................',
    '.......ab.......',
    '.......ab.......',
    '.......ab.......',
    '.....aaabbb.....',
    '......aabb......',
    '.......ab.......',
    '................',
    '................',
    '................',
    '................',
    '................',
    '................',
    '................'
)
Save-TransferUpgrade $arrow '#5fd86f' '#2e8f3e' 'deposit'
