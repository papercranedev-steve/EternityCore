DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261115233000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261115233000');
-- Add your query below.
-- PartyBot level 60 warlock spec (premade template 8): 22/3/26 instead of SM/Ruin 30/0/21
-- (user, 2026-11-15: no Curse of Agony talents, no Shadow Mastery, Siphon Life kept, Improved
-- Imp for the phased Imp's Blood Pact, Intensity and Destructive Reach). Points checked against
-- Talent.dbc 5875 (5 points per row, Ruin needs Devastation 5/5).
-- Affliction 22: Suppression 5, Improved Corruption 5, Improved Life Tap 2,
--   Improved Drain Life 2, Fel Concentration 5, Nightfall 2, Siphon Life 1 (+ trainer rank 4).
-- Demonology 3: Improved Imp 3.
-- Destruction 26: Improved Shadow Bolt 5, Cataclysm 5, Bane 5, Devastation 5,
--   Shadowburn 1 (+ trainer rank 6), Intensity 2, Destructive Reach 2, Ruin 1.
-- The trainer spells are the ones of the old template.
UPDATE `player_premade_spell_template` SET `name`='affli-ruin-imp-pve' WHERE `entry`=8;
DELETE FROM `player_premade_spell` WHERE `entry`=8;
INSERT INTO `player_premade_spell` (`entry`, `spell`) VALUES
-- trainer
(8, 126), (8, 132), (8, 603), (8, 688), (8, 691), (8, 696), (8, 697), (8, 698), (8, 712),
(8, 2970), (8, 5500), (8, 5697), (8, 5784), (8, 6215), (8, 11675), (8, 11678), (8, 11684),
(8, 11689), (8, 11695), (8, 11700), (8, 11703), (8, 11708), (8, 11713), (8, 11717), (8, 11719),
(8, 11722), (8, 11726), (8, 11730), (8, 11735), (8, 11743), (8, 17728), (8, 17862), (8, 17923),
(8, 17924), (8, 17926), (8, 17928), (8, 17953), (8, 18647), (8, 18871), (8, 18881), (8, 20757),
(8, 23161), (8, 25307), (8, 25309), (8, 25311), (8, 28610), (8, 33391),
-- affliction talents
(8, 18178), (8, 17814), (8, 18183), (8, 17805), (8, 17787), (8, 18095), (8, 18265),
-- demonology talents
(8, 18696),
-- destruction talents
(8, 17803), (8, 17782), (8, 17792), (8, 18134), (8, 17877), (8, 18136), (8, 17918), (8, 17959);
-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
