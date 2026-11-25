# Game table test data

Whole rows of the game's 7.2 tables (animegamedata2, ExcelBinOutput, CNRELWin7.2.0):

- GadgetExcelConfigData.json: 70211101, a chest (type Chest, jsonName SceneObj_Chest_Default_Lv1);
  40200002, type AirflowField with no jsonName; 70540003, type GatherObject, jsonName
  SceneObj_Gather_Small.
- GatherExcelConfigData.json: the two rows of gadget 70540003 (item 100001 Apple, point types 2006
  and 3001, refreshId 999900) and the first row of the table (gadget 70520001, item 101001 Iron
  Chunk, point type 1001, refreshId 999930); the Hydroculus row (gadget 70590074, item 107023,
  point type 4016), which has none; the first Crystal Chunk row (gadget 70520003, item 101003,
  point type 1003, refreshId 999932); a Quenepa Berry row (gadget 70540076, item 101249, point type
  2078), a Natlan specialty the table gives no refreshId though it grows back.
- MaterialExcelConfigData.json: 100001 Apple (ITEM_MATERIAL, MATERIAL_NOTICE_ADD_HP, rank 90000),
  101001 Iron Chunk (MATERIAL_EXCHANGE, rank 301), 101003 Crystal Chunk (rank 301), 101249 Quenepa
  Berry (rank 316), 107023 Hydroculus (MATERIAL_ELEM_CRYSTAL, rank 420001), 101004 Magical Crystal
  Chunk (rank 301), 101009 Condessence Crystal (MATERIAL_EXCHANGE, rank 301), which no gather
  row gives: Fontaine's rocks are rows of Crystal Chunk, the chunk they drop gives it.

When they grow back (fandom wiki, Reset page): Apple and Iron Chunk at the next 0:00 on the
server's clock, Crystal Chunk 72 h after the pickup, Quenepa Berry 48 h after it, Magical Crystal
when the mining outcrops move at 6:00 on the server's clock; the Hydroculus never.

- TextMapEN.json, TextMap_MediumEN.json (animegamedata2 main, TextMap, 2026-11-24): the first two entries of
  each, and the names of the seven materials above, all in the Medium map: Apple, Iron Chunk,
  Crystal Chunk, Quenepa Berry, Hydroculus, Magical Crystal Chunk, and Condessence Crystal (2026-11-25).
