DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261115210000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261115210000');
-- Add your query below.
-- PartyBot rogue specs of levels 19 / 29 / 39 / 49 (premade templates 29-32): combat swords,
-- on the way to the level 60 combat-swords-pve 19/32/0 (template 4), instead of the
-- assassination daggers twink builds (user, 2026-11-15: all combat, no daggers). The gear
-- templates of these levels carry swords (Zealot Blade, Sword of Omen, Doomforged
-- Straightedge), so Backstab, Ambush and the dagger talents never worked.
-- Points checked against Talent.dbc 5875 (5 points per row in the tree, Dual Wield
-- Specialization needs Precision 5/5, Weapon Expertise needs Blade Flurry).
-- 19 (10): Improved Gouge 3, Improved Sinister Strike 2, Precision 5.
-- 29 (20): + Improved Evasion 2, Improved Sprint 2, Lightning Reflexes 1, Dual Wield
--   Specialization 5.
-- 39 (30): + Blade Flurry 1, Sword Specialization 5, Aggression 3, Weapon Expertise 1.
-- 49 (40): Weapon Expertise 2, Adrenaline Rush 1 (combat 32, as at 60); Malice 5, Murder 2,
--   Improved Slice and Dice 1.
-- Between two levels the points left follow the next template (CombatBotBaseAI::
-- LearnTalentsTowards). The trainer spells of the old templates stay.
UPDATE `player_premade_spell_template` SET `name`='combat-swords-19' WHERE `entry`=29;
UPDATE `player_premade_spell_template` SET `name`='combat-swords-29' WHERE `entry`=30;
UPDATE `player_premade_spell_template` SET `name`='combat-swords-39' WHERE `entry`=31;
UPDATE `player_premade_spell_template` SET `name`='combat-swords-49' WHERE `entry`=32;
-- every rank of every rogue talent (Talent.dbc tabs 181 / 182 / 183)
DELETE FROM `player_premade_spell` WHERE `entry` IN (29, 30, 31, 32) AND `spell` IN (
13741, 13793, 13792, 13732, 13863, 13712, 13788, 13789, 13790, 13791, 13733, 13865, 13866, 13713,
13853, 13854, 13855, 13856, 13705, 13832, 13843, 13844, 13845, 13742, 13872, 14251, 13743, 13875,
13754, 13867, 13706, 13804, 13805, 13806, 13807, 13715, 13848, 13849, 13851, 13852, 13709, 13800,
13801, 13802, 13803, 13877, 13960, 13961, 13962, 13963, 13964, 13707, 13966, 13967, 13968, 13969,
30919, 30920, 18427, 18428, 18429, 13750, 14162, 14163, 14164, 14144, 14148, 14138, 14139, 14140,
14141, 14142, 14156, 14160, 14161, 14158, 14159, 14165, 14166, 14167, 14179, 14168, 14169, 14128,
14132, 14135, 14136, 14137, 16513, 16514, 16515, 16719, 16720, 14113, 14114, 14115, 14116, 14117,
14177, 14174, 14175, 14176, 14186, 14190, 14193, 14194, 14195, 14983, 13958, 13970, 13971, 13972,
13973, 14057, 14072, 14073, 14074, 14075, 30892, 30893, 13981, 14066, 13975, 14062, 14063, 14064,
14065, 13976, 13979, 13980, 14278, 14079, 14080, 14081, 13983, 14070, 14071, 14076, 14094, 14095,
14171, 14172, 14173, 30894, 30895, 14185, 14082, 14083, 16511, 30902, 30903, 30904, 30905, 30906,
14183);
INSERT INTO `player_premade_spell` (`entry`, `spell`) VALUES
-- 19: Improved Gouge 3, Improved Sinister Strike 2, Precision 5
(29, 13792), (29, 13863), (29, 13845),
-- 29: + Improved Evasion 2, Improved Sprint 2, Lightning Reflexes 1, Dual Wield Specialization 5
(30, 13792), (30, 13863), (30, 13845), (30, 13872), (30, 13875), (30, 13712), (30, 13852),
-- 39: + Blade Flurry, Sword Specialization 5, Aggression 3, Weapon Expertise 1
(31, 13792), (31, 13863), (31, 13845), (31, 13872), (31, 13875), (31, 13712), (31, 13852),
(31, 13877), (31, 13964), (31, 18429), (31, 30919),
-- 49: Weapon Expertise 2, Adrenaline Rush; Malice 5, Murder 2, Improved Slice and Dice 1
(32, 13792), (32, 13863), (32, 13845), (32, 13872), (32, 13875), (32, 13712), (32, 13852),
(32, 13877), (32, 13964), (32, 18429), (32, 30920), (32, 13750),
(32, 14142), (32, 14159), (32, 14165);
-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
