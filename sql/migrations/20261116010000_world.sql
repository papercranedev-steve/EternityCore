DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261116010000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261116010000');
-- Add your query below.
-- Holy Light ranks 1-3: the level penalty for spells below level 20 (user, 2026-11-16).
-- 20200515174239_world.sql ("Fix holy light bonus") set every rank to 0.714, and a coefficient in
-- the template skips SpellCaster::CalculateLevelPenalty, so rank 1 healed ~450 for 35 mana with
-- 576 +healing and beat every Flash of Light rank per mana (strat-20261116-002926). Same rule as
-- the other classes' low ranks (Healing Touch / Lesser Heal / Healing Wave rank 1: 0.123):
-- 2.5 / 3.5 x (1 - (20 - spellLevel) x 0.0375).
UPDATE `spell_template` SET `effectBonusCoefficient1`=0.205 WHERE `entry`=635;   -- rank 1, level 1
UPDATE `spell_template` SET `effectBonusCoefficient1`=0.339 WHERE `entry`=639;   -- rank 2, level 6
UPDATE `spell_template` SET `effectBonusCoefficient1`=0.553 WHERE `entry`=647;   -- rank 3, level 14
-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
