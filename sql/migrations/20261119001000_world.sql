DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261119001000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261119001000');
-- Add your query below.
-- Companion class quest chains, first batch (.kiro/docs/2026-11-18-companion-class-quests.md §6):
-- affinity of the key ability chains where the default (by length: 1 step 2, 2-4 steps 4,
-- 5+ steps 6) does not match how much the ability matters. Keyed by the chain's last quest.
REPLACE INTO `custom_classquest_chain` (`last_quest`, `kind`, `affinity`, `skip`, `comment`) VALUES
-- warrior
(1498, NULL, 4, 0, 'Defensive Stance'),
(1678, NULL, 4, 0, 'Defensive Stance'),
(1819, NULL, 4, 0, 'Defensive Stance'),
(1665, NULL, 4, 0, 'Defensive Stance'),
(1683, NULL, 4, 0, 'Defensive Stance'),
(1719, NULL, 7, 0, 'Berserker Stance'),
-- paladin
(1661, NULL, 6, 0, 'Summon Warhorse'),
(7647, NULL, 10, 0, 'Summon Charger'),
-- hunter
(6081, NULL, 8, 0, 'Tame Beast, Beast Training (orc, troll)'),
(6086, NULL, 8, 0, 'Tame Beast, Beast Training (dwarf)'),
(6089, NULL, 8, 0, 'Tame Beast, Beast Training (tauren)'),
(6103, NULL, 8, 0, 'Tame Beast, Beast Training (night elf)'),
-- rogue
(2480, NULL, 5, 0, 'Poisons'),
(2359, NULL, 5, 0, 'Poisons'),
-- shaman
(1527, NULL, 5, 0, 'Fire Totem'),
(96,   NULL, 8, 0, 'Water Totem'),
(1531, NULL, 5, 0, 'Air Totem'),
(1532, NULL, 5, 0, 'Air Totem'),
-- warlock
(1513, NULL, 5, 0, 'Summon Succubus'),
(1474, NULL, 5, 0, 'Summon Succubus'),
(1739, NULL, 5, 0, 'Summon Succubus'),
(4490, NULL, 6, 0, 'Summon Felsteed'),
(7631, NULL, 10, 0, 'Summon Dreadsteed'),
-- druid
(6001, NULL, 5, 0, 'Bear Form (Alliance)'),
(6002, NULL, 5, 0, 'Bear Form (Horde)'),
(6125, NULL, 4, 0, 'Cure Poison (Alliance)'),
(6130, NULL, 4, 0, 'Cure Poison (Horde)');
-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
