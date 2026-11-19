# Finds test data

Cuts of real 7.1 recordings as decrypted messages, cut and blanked the same way as
tests/data/learner (see its answers.md for the format). No login in the cuts.

| File | From | Messages | What happens |
|---|---|---|---|
| 7.1-oculi.bin | 7.1, pickups2-check, 300 to 640 s | 7516 | a Hydroculus and a Dendroculus picked up, ores and plants gathered |
| 7.1-stonepile.bin | 7.1, stonepile, 2080 to 2420 s | 2459 | a Dendro Rock broken, the Stone Pile Mora Chest under it opened |
| 7.1-oculi-late.bin | 7.1, pickups2-check, 345 to 400 s | 691 | the Hydroculus of 7.1-oculi.bin picked up, the cut starts after it appeared |

items/ is a cut of HoYoLAB's map items (the items pack made on 2026-11-19) in the item files'
folder format: the whole tree.json and areas.json, the points of the Chests, Special Items,
Experience and Puzzle Chest groups, Geo Sigil and Dendro Rock within 100 map units of every object
the cuts show (1151 points), and the game's rows (gadgets, gathers, materials) of every gadget
in the cuts and the items they give.

## The answers

Found by hand on the recordings and HoYoLAB's map:

| File | Map item | Type | How | Object |
|---|---|---|---|---|
| 7.1-oculi.bin | 53912 | Hydroculus (508) | picked up, item 107023 | 70590074, 133409366/366001 |
| 7.1-oculi.bin | 29159 | Dendroculus (403) | picked up | 70590063, 133301245/245010 |
| 7.1-stonepile.bin | none | Dendro Rock (415) | broken, no item, not a find | 70330218, 133302314/314007 |
| 7.1-stonepile.bin | 32859 | Stone Pile Mora Chest (359) | opened | 70217020, 133302314/314002 |
| 7.1-oculi-late.bin | none | | the Hydroculus was there before the cut | |
