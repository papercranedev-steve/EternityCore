DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261122001000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261122001000');
-- Add your query below.
-- Quest items used on one creature (the item's spell targets a single unit) that had no
-- item_required_target row, so bots holding the quest never used them (Life/Errands/QuestItemUse
-- reads this table). Found by a query over quest source / required items with an on-use spell;
-- the quest's ReqCreatureOrGOId is often the result (Cured Deer, Tamed Kodo, Enthralled Rat), so
-- the targets come from the scripts and spell_script_target. Type 1 alive, 2 dead.
DELETE FROM `item_required_target` WHERE `entry` IN (15826, 17117, 13892, 13289, 10699, 11522);
INSERT INTO `item_required_target` (`entry`, `type`, `target_entry`) VALUES
-- Curing the Sick (6124, 6129): Curative Animal Salve on a Sickly Deer / Sickly Gazelle (npcs_special.cpp)
(15826, 1, 12298),
(15826, 1, 12296),
-- Deeprun Rat Roundup (6661): Rat Catcher's Flute on a Deeprun Rat (spell_item.cpp, MelodiusRaptureScript)
(17117, 1, 13016),
-- Kodo Roundup (5561): Kodo Kombobulator on an Aged / Dying / Ancient Kodo
(13892, 1, 4700),
(13892, 1, 4701),
(13892, 1, 4702),
-- The Restless Souls (5282): Egan's Blaster on a Restless Soul (stratholme.cpp)
(13289, 1, 11122),
-- Screecher Spirits (3520): Yeh'kinya's Bramble on a dead Vale Screecher (spell_script_target of 12699)
(10699, 2, 5307),
(10699, 2, 5308),
-- It's Dangerous to Go Alone (3962): Silver Totem of Aquementas on Blazerunner (spell_script_target of 14247)
(11522, 1, 9376);
-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
