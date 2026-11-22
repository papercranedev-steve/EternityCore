DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261122002000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261122002000');
-- Add your query below.
-- Per-quest exceptions for bots (PlayerBots/Shared/QuestOverrides), user 2026-11-22.
CREATE TABLE IF NOT EXISTS `custom_bot_quest` (
  `quest` MEDIUMINT UNSIGNED NOT NULL COMMENT 'quest_template.entry',
  `follow_credit` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '1 = party bots near the player get his objective counts',
  `no_item_use` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '1 = bots never use the quest''s items (Life/Errands/QuestItemUse)',
  `comment` VARCHAR(255) NOT NULL DEFAULT '',
  PRIMARY KEY (`quest`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='bot quest exceptions';
INSERT IGNORE INTO `custom_bot_quest` (`quest`, `follow_credit`, `no_item_use`, `comment`) VALUES
-- Objectives bots cannot do themselves: they complete with the player.
(2118, 1, 0, 'Plagued Lands: bear trap, the bear led back'),
(3514, 1, 0, 'Horde Presence: Kadrak''s Flag'),
(1657, 1, 0, 'Stinking Up Southshore: Hallow''s End stink bomb'),
-- Items that summon a boss or start an event: the player decides when (spell focus quests that
-- QuestItemUse would otherwise use on its own).
(5056, 0, 1, 'Shy-Rotam: summons Shy-Rotam'),
(4771, 0, 1, 'Dawn''s Gambit: Scholomance event'),
(5721, 0, 1, 'The Battle of Darrowshire: event'),
(7629, 0, 1, 'Imp Delivery: Dreadsteed chain, Scholomance'),
(7640, 0, 1, 'Exorcising Terrordale: summons the spirits'),
(4971, 0, 1, 'A Matter of Time: summons the parasites'),
(9257, 0, 1, 'Atiesh: summons Atiesh'),
(9269, 0, 1, 'Atiesh: summons Atiesh'),
(9270, 0, 1, 'Atiesh: summons Atiesh'),
(9271, 0, 1, 'Atiesh: summons Atiesh'),
(8966, 0, 1, 'Lord Valthalak: brazier summons a boss'),
(8967, 0, 1, 'Lord Valthalak: brazier summons a boss'),
(8968, 0, 1, 'Lord Valthalak: brazier summons a boss'),
(8969, 0, 1, 'Lord Valthalak: brazier summons a boss'),
(8989, 0, 1, 'Lord Valthalak: brazier summons a boss'),
(8990, 0, 1, 'Lord Valthalak: brazier summons a boss'),
(8991, 0, 1, 'Lord Valthalak: brazier summons a boss'),
(8992, 0, 1, 'Lord Valthalak: brazier summons a boss'),
(8995, 0, 1, 'Mea Culpa, Lord Valthalak: summons Valthalak'),
(9015, 0, 1, 'The Challenge: summons a boss'),
(8996, 0, 1, 'Return to Bodley: summons Valthalak'),
(9018, 0, 1, 'Anthion''s Parting Words: summons Thelrin'),
(881, 0, 1, 'Echeyakee: summons Echeyakee'),
(656, 0, 1, 'Summoning the Princess: summons Myzrael'),
(2937, 0, 1, 'Summoning Shadra: summons Shadra'),
(3528, 0, 1, 'The God Hakkar: Sunken Temple event'),
(6621, 0, 1, 'King of the Foulweald: the banner calls Chief Murgut'),
(7603, 0, 1, 'Kroshius'' Infernal Core: wakes Kroshius'),
(7668, 0, 1, 'The Darkreaver Menace: summons Death Knight Darkreaver'),
(8258, 0, 1, 'The Darkreaver Menace: summons Death Knight Darkreaver'),
(8481, 0, 1, 'The Root of All Evil: summons Xiggs'' demon'),
(8729, 0, 1, 'The Wrath of Neptulon: Molten Core'),
(8315, 0, 1, 'The Calling: AQ glyphs event'),
(2520, 0, 1, 'Sathrah''s Sacrifice: summons Sathrah'),
(4507, 0, 1, 'Pawn Captures Queen: the lure calls the queen'),
(1920, 0, 1, 'Investigate the Blue Recluse: summons the spirit'),
(1960, 0, 1, 'Investigate the Alchemist Shop: summons the spirit'),
(2845, 0, 1, 'Wandering Shay: the bell calls the escorted Shay'),
(5321, 0, 1, 'The Sleeper Has Awakened: Kerlonian escort'),
(4491, 0, 1, 'A Little Help From My Friends: Ringo escort'),
(3909, 0, 1, 'The Videre Elixir: the elixir kills who drinks it'),
(7583, 0, 1, 'Suppression: crystal prison event'),
(4299, 0, 1, 'not in the game (NYI)');
-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
