# Learner test data

Cuts of real recordings as decrypted messages, for the learner. Both sides are kept, the learner
needs what the game sends too. No login in the cuts (7.2-login.bin is the login's first message on
its own). Every text field and every player UID is blanked at the same length; entity ids, gadget,
group, config and item ids and positions are as the game sent them.

Format: per message a u64 unix ms, a u8 side (0 received, 1 sent), a u16 message id, a u32 size,
then the body (little endian).

| File | From | Messages | What happens |
|---|---|---|---|
| 7.1-pickups.bin | 7.1, a pickups walk, 250 to 600 s | 7551 | walking, a teleport, pickups, objects changing state; no chest |
| 7.1-puzzles.bin | 7.1, puzzles, 40 to 700 s | 7980 | walking, a teleport, puzzles solved, chests opened, pickups |
| 7.2-login.bin | 7.2, the first message the game sends | 1 | the login, all text blanked but "OSRELWin7.2.0" |
| 7.2-walk.bin | 7.2, a walk, 5 to 200 s | 2503 | the world loading, walking |

## The answers

7.1, found by hand on the recordings against the game's data tables (gadget, material, chest
groups) and what was done in game:

| Kind | Id | Fields |
|---|---|---|
| objects near | 27685 | entries in 8: entity 2, position part 4 (x y z in its 1), gadget part 13 with gadget 1, group 2, config 3 |
| item got | 652 | from 11, items in 13: item 1, count 2 |
| state changed | 22292 | entity 3, state 7 |
| object gone | 24935 | entity 5, reason 11 |
| interact (sent) | 5765 | entity 11, gadget 4 |
| interact answer | 881 | entity 15, gadget 3 |
| chest opened | 28616 | group 12, config 11 |
| player moved (sent) | 2246 | the part at 8, 6, 9, 15: entity 1, position part 2 |
| scene changed | 9582 | the new position 8 |

7.2: objects near is 4469 (entries in 2: entity 2, position 4, gadget part 13 with 1, 2, 3), player
moved is 24007 (the part at 12, 14, 14, 8: entity 1, position 2). The other 7.2 kinds aren't in
these recordings (no interact, no pickup). 7.2-learned.json is the learned file of a PC that
played 7.2 with the overlay (its 7.2 part): the same two, and the other seven kinds as learned in
play.

What each file teaches with nothing known: 7.1-pickups all but chest opened, 7.1-puzzles all nine,
7.2-login then 7.2-walk the version 7.2.0, objects near and player moved.
