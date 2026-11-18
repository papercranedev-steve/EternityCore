DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261119000000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261119000000');
-- Add your query below.
-- Companion class quests, stage C3 (.kiro/docs/2026-11-18-companion-class-quests.md §6-§7): the chains
-- are made from quest_template at startup (PlayerBots/Shared/ClassQuestChains); this table only
-- changes what the defaults get wrong, keyed by the chain's last quest.
CREATE TABLE IF NOT EXISTS `custom_classquest_chain` (
  `last_quest` MEDIUMINT UNSIGNED NOT NULL COMMENT 'quest_template.entry of the chain''s last quest',
  `kind` TINYINT UNSIGNED NULL DEFAULT NULL COMMENT '0 = ability, 1 = gear, NULL = from the rewards',
  `affinity` TINYINT UNSIGNED NULL DEFAULT NULL COMMENT 'affinity for helping (0..100 scale), NULL = from kind and length',
  `skip` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '1 = never asked, not a chain',
  `comment` VARCHAR(255) NOT NULL DEFAULT '',
  PRIMARY KEY (`last_quest`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='companion class quest chains: overrides of the defaults';
-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
