DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261118220000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261118220000');
-- Add your query below.
-- Companion class quests, stage C1 (.kiro/docs/2026-11-18-companion-class-quests.md §2): a hand-made
-- mirror quest to check what the client shows. Id = real quest + 100000 (1498 "Path of Defense").
-- Not given by anyone, no rewards, nobody can turn it in; `.partybot mirror` puts it in the log.
-- Its two objectives are counters with own text, set by the server:
--   1: the placeholder line ("bring the bot to the quest giver"), 1 needed
--   2: an item objective turned into a counter ("Singed Scales"), 5 needed
-- The counted creatures are the GM-only waypoint (1) and spawn point (2): nobody kills them.
DELETE FROM `quest_template` WHERE `entry` = 101498;
INSERT INTO `quest_template` (`entry`, `patch`, `Method`, `ZoneOrSort`, `MinLevel`, `QuestLevel`, `Type`,
  `RequiredClasses`, `RequiredRaces`, `QuestFlags`, `SpecialFlags`,
  `Title`, `Details`, `Objectives`, `OfferRewardText`, `RequestItemsText`, `EndText`,
  `ObjectiveText1`, `ObjectiveText2`,
  `ReqCreatureOrGOId1`, `ReqCreatureOrGOCount1`, `ReqCreatureOrGOId2`, `ReqCreatureOrGOCount2`)
VALUES (101498, 0, 2, -81, 1, 10, 0,
  0, 0, 0, 0,
  '[镜像] 防御之道',
  '这是队友的职业任务，进度跟着他走，你只需要带着他。$B$B去贫瘠之地的远望堡找乌泽克，让他接下任务。然后陪他收集 5 片灼烧的鳞片。（C1 测试文本）',
  '带着队友去远望堡找乌泽克，再帮他收集 5 片灼烧的鳞片。',
  '', '', '',
  '带队友去找乌泽克', '灼烧的鳞片（队友的）',
  1, 1, 2, 5);
-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
