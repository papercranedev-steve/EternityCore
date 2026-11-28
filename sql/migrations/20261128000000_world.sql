DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261128000000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261128000000');
-- Add your query below.
-- Baron Rivendare's Raise Dead raises all six Mindless Skeletons again. The six casts
-- (Raise Dead 1-6, one per spot around the room) went through Creature::TryToCast, which
-- refuses a guardian summon while one with the same entry is alive: only the first skeleton
-- of each wave came up. CF_FORCE_CAST (4) skips that check; CF_TRIGGERED (2) as before.
UPDATE `creature_spells_scripts` SET `datalong2` = 6
WHERE `id` = 17473 AND `command` = 15 AND `datalong` BETWEEN 17475 AND 17480;
-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
