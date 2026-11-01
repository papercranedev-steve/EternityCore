DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261101235000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261101235000');
-- Add your query below.
-- PartyBot level 60 mage spec (premade template 7): deep frost 18/0/33 instead of the
-- Arcane Power / frost 31/0/20 build (user, 2026-11-01: Ice Block, Winter's Chill and the
-- cheap Ice Barrier). Points checked against Talent.dbc 5875 (tiers, Ice Barrier needs Ice Block).
-- Arcane 18: Arcane Subtlety 2, Arcane Focus 3, Arcane Concentration 5, Magic Absorption 2,
--   Improved Arcane Explosion 3, Arcane Meditation 3.
-- Frost 33: Improved Frostbolt 5, Elemental Precision 3, Ice Shards 5, Improved Frost Nova 2,
--   Piercing Ice 3, Cold Snap 1, Improved Blizzard 1, Arctic Reach 2, Frost Channeling 3,
--   Ice Block 1, Improved Cone of Cold 1, Winter's Chill 5, Ice Barrier 1 (+ trainer rank 4).
-- The trainer spells are the ones of the old template.
UPDATE `player_premade_spell_template` SET `name`='deep-frost-pve' WHERE `entry`=7;
DELETE FROM `player_premade_spell` WHERE `entry`=7;
INSERT INTO `player_premade_spell` (`entry`, `spell`) VALUES
-- trainer
(7, 130), (7, 475), (7, 759), (7, 1953), (7, 2139), (7, 2855), (7, 3552), (7, 3561), (7, 3562),
(7, 3565), (7, 7301), (7, 10053), (7, 10054), (7, 10059), (7, 10139), (7, 10157), (7, 10161),
(7, 10170), (7, 10174), (7, 10187), (7, 10193), (7, 10199), (7, 10202), (7, 10207), (7, 10216),
(7, 10220), (7, 10225), (7, 10230), (7, 11416), (7, 11419), (7, 12051), (7, 12826), (7, 22783),
(7, 23028), (7, 25304), (7, 25306), (7, 25345), (7, 28272), (7, 28609), (7, 28612),
(7, 13033),
-- arcane talents
(7, 12592), (7, 12840), (7, 12577), (7, 29444), (7, 12469), (7, 18464),
-- frost talents
(7, 16766), (7, 29440), (7, 15053), (7, 12475), (7, 12953), (7, 12472), (7, 11185), (7, 16758),
(7, 12519), (7, 11958), (7, 11190), (7, 28595), (7, 11426);
-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
