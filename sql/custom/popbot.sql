-- World population bots (src/game/PlayerBots/Population). Import into the WORLD database (mangos).
-- Safe to run again.

-- City scene points, recorded in game with: .popbot point add <city> <type> [route] [capacity]
CREATE TABLE IF NOT EXISTS `custom_popbot_point` (
  `id`       INT UNSIGNED      NOT NULL AUTO_INCREMENT,
  `city`     VARCHAR(32)       NOT NULL COMMENT 'ironforge stormwind orgrimmar undercity thunderbluff darnassus world',
  `type`     VARCHAR(16)       NOT NULL COMMENT 'ah bank mail idle route showcase hub (hub: city world, a gathering place)',
  `route`    VARCHAR(32)       NOT NULL DEFAULT '' COMMENT 'route name, type route only',
  `seq`      INT UNSIGNED      NOT NULL DEFAULT 0  COMMENT 'step inside the route',
  `map`      SMALLINT UNSIGNED NOT NULL,
  `x`        FLOAT             NOT NULL,
  `y`        FLOAT             NOT NULL,
  `z`        FLOAT             NOT NULL,
  `o`        FLOAT             NOT NULL,
  `capacity` TINYINT UNSIGNED  NOT NULL DEFAULT 1  COMMENT 'how many bots can use the point at once',
  PRIMARY KEY (`id`),
  KEY `idx_city` (`city`, `type`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

-- Extra English names. race 0 = any race, otherwise the race id (1 Human, 2 Orc, 3 Dwarf,
-- 4 Night Elf, 5 Undead, 6 Tauren, 7 Gnome, 8 Troll). Letters only, max 12 characters.
-- Used half of the time when names exist for the race; built-in names otherwise.
CREATE TABLE IF NOT EXISTS `custom_popbot_name` (
  `name` VARCHAR(12)      NOT NULL,
  `race` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`name`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
