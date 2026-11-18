DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261118230000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261118230000');
-- Add your query below.
-- Companion class quests, stage C2: mirror quests are made in code at startup
-- (PlayerBots/Shared/QuestMirror, ids 100000+ and 200000+ the real quest). The hand-made test
-- mirror of stage C1 (20261118220000_world.sql) would take the id of the mirror of 1498.
DELETE FROM `quest_template` WHERE `entry` = 101498;
-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
