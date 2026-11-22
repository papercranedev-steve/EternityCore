DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261122000000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261122000000');
-- Add your query below.
-- Lazy Peons (5441): Foreman's Blackjack (16114) is used on a Lazy Peon (10556), alive. Without
-- the row bots holding the quest do not use it (Life/Errands/QuestItemUse reads this table), and
-- the item can be used on anything.
DELETE FROM `item_required_target` WHERE `entry`=16114;
INSERT INTO `item_required_target` (`entry`, `type`, `target_entry`) VALUES (16114, 1, 10556);
-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
