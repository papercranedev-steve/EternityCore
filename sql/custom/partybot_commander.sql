-- PartyBot Commander hand-written overrides (src/game/PlayerBots/Commander/Perception/ProfileStore).
-- Import into the WORLD database (mangos). Safe to run again.
-- Only for cases the automatic profile gets wrong; see .kiro/docs/2026-11-01-value-scoring.md 4.4.
-- After editing rows: .partybot ai reload overrides

CREATE TABLE IF NOT EXISTS `custom_partybot_spell_override` (
  `spell_id`       INT UNSIGNED     NOT NULL,
  `interrupt_mult` FLOAT            NOT NULL DEFAULT 1 COMMENT 'x interrupt benefit; 0 = never interrupt, 10 = always worth it',
  `dispel`         TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0 auto, 1 dispel at once, 2 never, 3 only after combat',
  `comment`        VARCHAR(255)     NOT NULL DEFAULT '',
  PRIMARY KEY (`spell_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE IF NOT EXISTS `custom_partybot_creature_override` (
  `entry`          INT UNSIGNED     NOT NULL,
  `interrupt_mult` FLOAT            NOT NULL DEFAULT 1 COMMENT 'x interrupt benefit for every cast of this creature',
  `taunt_mult`     FLOAT            NOT NULL DEFAULT 1 COMMENT 'x taunt benefit; 0 = never taunt it off someone',
  `comment`        VARCHAR(255)     NOT NULL DEFAULT '',
  PRIMARY KEY (`entry`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
