DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261115235000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261115235000');
-- Add your query below.
-- PartyBot hunter specs (premade templates 25-28 and 3): beast mastery 31/20/0 at 60 and the
-- way there, instead of the marksmanship / survival builds (user, 2026-11-15: beast mastery by
-- default, players change their bot's talents themselves). Points checked against Talent.dbc
-- 5875 (5 points per row in the tree, Frenzy needs Ferocity 5/5, Bestial Wrath needs
-- Intimidation, Mortal Shots needs Lethal Shots 5/5).
-- 60 (51): Beast Mastery 31: Improved Aspect of the Hawk 5, Endurance Training 5, Unleashed
--   Fury 5, Ferocity 5, Spirit Bond 2, Intimidation 1, Bestial Discipline 2, Frenzy 5, Bestial
--   Wrath 1 (Thick Hide and Improved Revive Pet only help a pet that tanks).
--   Marksmanship 20: Efficiency 5, Improved Hunter's Mark 1, Lethal Shots 5, Aimed Shot 1
--   (+ trainer rank 6), Hawk Eye 3, Mortal Shots 5.
-- 19 (10): Improved Aspect of the Hawk 5, Endurance Training 5.
-- 29 (20): + Unleashed Fury 5, Ferocity 5.
-- 39 (30): + Spirit Bond 2, Intimidation 1, Bestial Discipline 2, Frenzy 5.
-- 49 (40): + Bestial Wrath 1; Efficiency 5, Lethal Shots 4.
-- Between two levels the points left follow the next template (CombatBotBaseAI::
-- LearnTalentsTowards). The trainer spells of the old templates stay, but the trainer ranks of
-- talent spells no longer taken (Aimed Shot below 60, Trueshot Aura, Counterattack, Wyvern Sting).
UPDATE `player_premade_spell_template` SET `name`='bm-19' WHERE `entry`=25;
UPDATE `player_premade_spell_template` SET `name`='bm-29' WHERE `entry`=26;
UPDATE `player_premade_spell_template` SET `name`='bm-39' WHERE `entry`=27;
UPDATE `player_premade_spell_template` SET `name`='bm-49' WHERE `entry`=28;
UPDATE `player_premade_spell_template` SET `name`='bm-mm-pve' WHERE `entry`=3;
-- every rank of every hunter talent (the three hunter tabs of Talent.dbc), and the trainer ranks of
-- Aimed Shot, Trueshot Aura, Counterattack and Wyvern Sting
DELETE FROM `player_premade_spell` WHERE `entry` IN (25, 26, 27, 28, 3) AND `spell` IN (
19549, 19550, 19551, 24386, 24387, 19552, 19553, 19554, 19555, 19556, 19559, 19560, 19572, 19573,
19574, 19577, 19578, 20895, 19583, 19584, 19585, 19586, 19587, 19590, 19592, 19596, 19598, 19599,
19600, 19601, 19602, 19609, 19610, 19612, 19616, 19617, 19618, 19619, 19620, 19621, 19622, 19623,
19624, 19625, 19557, 19558, 24443, 19575,
19407, 19412, 19413, 19414, 19415, 19416, 19417, 19418, 19419, 19420, 19421, 19422, 19423, 19424,
19425, 19426, 19427, 19429, 19430, 19431, 19434, 19454, 19455, 19456, 19457, 19458, 19461, 19462,
24691, 19464, 19465, 19466, 19467, 19468, 19485, 19487, 19488, 19489, 19490, 19491, 19493, 19494,
19498, 19499, 19500, 19503, 19506, 19507, 19508, 19509, 19510, 19511,
19151, 19152, 19153, 19168, 19180, 19181, 24296, 24297, 19184, 19387, 19388, 19389, 19390, 19228,
19232, 19233, 19234, 19235, 19239, 19245, 19263, 19286, 19287, 19290, 19294, 24283, 19295, 19297,
19298, 19301, 19300, 19306, 19370, 19371, 19373, 19376, 19377, 19386, 19159, 19160, 19255, 19256,
19257, 19258, 19259, 24293, 24294, 24295,
20900, 20901, 20902, 20903, 20904, 20905, 20906, 20909, 20910, 24132, 24133);
INSERT INTO `player_premade_spell` (`entry`, `spell`) VALUES
-- 19: Improved Aspect of the Hawk 5, Endurance Training 5
(25, 19556), (25, 19587),
-- 29: + Unleashed Fury 5, Ferocity 5
(26, 19556), (26, 19587), (26, 19620), (26, 19602),
-- 39: + Spirit Bond 2, Intimidation, Bestial Discipline 2, Frenzy 5
(27, 19556), (27, 19587), (27, 19620), (27, 19602), (27, 20895), (27, 19577), (27, 19592), (27, 19625),
-- 49: + Bestial Wrath; Efficiency 5, Lethal Shots 4
(28, 19556), (28, 19587), (28, 19620), (28, 19602), (28, 20895), (28, 19577), (28, 19592), (28, 19625),
(28, 19574), (28, 19420), (28, 19430),
-- 60: beast mastery 31; Efficiency 5, Improved Hunter's Mark 1, Lethal Shots 5, Aimed Shot
-- (+ rank 6), Hawk Eye 3, Mortal Shots 5
(3, 19556), (3, 19587), (3, 19620), (3, 19602), (3, 20895), (3, 19577), (3, 19592), (3, 19625),
(3, 19574), (3, 19420), (3, 19421), (3, 19431), (3, 19434), (3, 20904), (3, 19500), (3, 19490);
-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
