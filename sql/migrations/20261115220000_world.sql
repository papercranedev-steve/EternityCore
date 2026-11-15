DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261115220000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261115220000');
-- Add your query below.
-- PartyBot level 60 warrior fury-dw-pve 17/34/0 (premade template 1): the arms point in
-- Improved Overpower 1/2 goes to Improved Heroic Strike 3/3 (Heroic Strike is the fury rage
-- dump, Overpower needs Battle Stance and is not cast from Berserker Stance). Arms stays 17:
-- Improved Heroic Strike 3, Improved Rend 3, Tactical Mastery 5, Anger Management 1,
-- Deep Wounds 3, Impale 2 (15 points before Impale's row).
DELETE FROM `player_premade_spell` WHERE `entry`=1 AND `spell` IN (12282, 12663, 12664, 12290, 12963);
INSERT INTO `player_premade_spell` (`entry`, `spell`) VALUES (1, 12664);
-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
