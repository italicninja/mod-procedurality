-- One-time migration for databases that ran core patch 0004-procgen (wowproject, 2026-10-14 to 2026-10-20):
-- its tables move to their mod-procedurality names. Live incidents, the quest ID counter and the rare rows
-- (190100-191099) carry over unchanged. Run once, with the worldserver stopped, before the first start of
-- a build with mod-procedurality. Fresh databases run characters/ and world/ instead.

RENAME TABLE
  `acore_characters`.`procgen_bounty` TO `acore_characters`.`procedurality_bounty`,
  `acore_characters`.`procgen_quest_template` TO `acore_characters`.`procedurality_quest_template`,
  `acore_characters`.`procgen_quest_request_items` TO `acore_characters`.`procedurality_quest_request_items`,
  `acore_characters`.`procgen_quest_offer_reward` TO `acore_characters`.`procedurality_quest_offer_reward`,
  `acore_world`.`procgen_text` TO `acore_world`.`procedurality_text`;

-- Rare rows not yet used keep the placeholder name; used ones are rebuilt from procedurality_bounty at startup
UPDATE `acore_world`.`creature_template` SET `name` = 'Procedurality Rare Slot'
  WHERE `entry` BETWEEN 190100 AND 191099 AND `name` = 'Procgen Rare Slot';

-- The core's DB updater applied the 0004-procgen SQL from data/sql/custom; those files are gone now
DELETE FROM `acore_characters`.`updates` WHERE `name` LIKE '2026\_10\_14\_%procgen%';
DELETE FROM `acore_world`.`updates` WHERE `name` LIKE '2026\_10\_14\_%procgen%';
