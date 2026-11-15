DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261115200000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261115200000');
-- Add your query below.
-- PartyBot level 60 warlock spec (premade template 8): SM/Ruin 30/0/21 with the Imp instead of
-- DS/Ruin (user, 2026-11-15: an all-round spec that keeps its pet, no Demonic Sacrifice for
-- now). Points checked against Talent.dbc 5875 (5 points per row, Shadow Mastery needs Siphon
-- Life, Ruin needs Devastation 5/5).
-- Affliction 30: Suppression 5, Improved Corruption 5, Improved Life Tap 2,
--   Improved Curse of Agony 3, Amplify Curse 1, Fel Concentration 5, Nightfall 2, Grim Reach 1,
--   Siphon Life 1 (+ trainer rank 4), Shadow Mastery 5.
-- Destruction 21: Improved Shadow Bolt 5, Cataclysm 4, Bane 5, Devastation 5,
--   Shadowburn 1 (+ trainer rank 6), Ruin 1.
-- The trainer spells are the ones of the old template.
UPDATE `player_premade_spell_template` SET `name`='sm-ruin-pve' WHERE `entry`=8;
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
(8, 18178), (8, 17814), (8, 18183), (8, 18830), (8, 18288), (8, 17787), (8, 18095), (8, 18218),
(8, 18265), (8, 18275),
-- destruction talents
(8, 17803), (8, 17781), (8, 17792), (8, 18134), (8, 17877), (8, 17959);
-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
