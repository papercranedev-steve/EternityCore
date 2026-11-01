DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261101120000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261101120000');
-- Add your query below.


-- AH bot: random stack size and weighted selection.
-- `stack_min` 0 keeps the old fixed stack; otherwise the stack is random in [stack_min, stack]
-- and `bid` / `buyout` (prices for `stack` items) are scaled to the chosen size.
-- `weight` is the relative chance of the row being picked for a new listing (0 = never).
ALTER TABLE `auctionhousebot`
    ADD COLUMN `stack_min` TINYINT(3) UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Min Stack Size (0 = fixed stack)' AFTER `stack`,
    ADD COLUMN `weight` INT(11) UNSIGNED NOT NULL DEFAULT 1 COMMENT 'Relative Listing Chance' AFTER `buyout`;


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
