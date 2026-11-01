DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261101130000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261101130000');
-- Add your query below.
-- Keep the time an auction was posted across restarts (it used to be reset to 0 on load).
-- The AH buyer bot schedules its purchases relative to this time.
-- 0 = unknown (rows from before this migration); the server then uses its load time.
ALTER TABLE `auction`
    ADD COLUMN `deposit_time` BIGINT(40) NOT NULL DEFAULT 0 COMMENT 'Unix time the auction was posted' AFTER `deposit`;
-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
