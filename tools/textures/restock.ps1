# Draws the Restock / Advanced Restock upgrade textures: a blue arrow pointing out of a chest.
. "$PSScriptRoot/deposit_restock_common.ps1"

$arrow = @(
    '................',
    '................',
    '................',
    '.......ab.......',
    '......aabb......',
    '.....aaabbb.....',
    '.......ab.......',
    '.......ab.......',
    '.......ab.......',
    '................',
    '................',
    '................',
    '................',
    '................',
    '................',
    '................'
)
Save-TransferUpgrade $arrow '#62b0ff' '#2f69bd' 'restock'
