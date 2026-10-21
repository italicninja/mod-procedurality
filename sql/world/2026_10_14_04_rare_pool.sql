-- Procedurality (mod-procedurality), milestone 2 (wowproject docs/procedurality.md): 1000 creature_template rows
-- reserved for Wanted rares (190100-191099). The worldserver rebuilds a row as a named rare copy of a camp
-- creature the first time it uses it, and never reuses one (the client caches creature names by entry).
-- The placeholders copy Defias Thug (38) so they load without errors; nothing spawns them.
DELETE FROM `creature_template_model` WHERE `CreatureID` BETWEEN 190100 AND 191099;
DELETE FROM `creature_template` WHERE `entry` BETWEEN 190100 AND 191099;

DROP TEMPORARY TABLE IF EXISTS `procedurality_rare_slots`;
CREATE TEMPORARY TABLE `procedurality_rare_slots` AS
  WITH RECURSIVE `seq` (`n`) AS (SELECT 0 UNION ALL SELECT `n` + 1 FROM `seq` WHERE `n` < 999)
  SELECT `ct`.*, `seq`.`n` AS `slot_n` FROM `creature_template` `ct` CROSS JOIN `seq` WHERE `ct`.`entry` = 38;
UPDATE `procedurality_rare_slots` SET `entry` = 190100 + `slot_n`, `name` = 'Procedurality Rare Slot', `subname` = '',
  `AIName` = '', `ScriptName` = '', `lootid` = 0, `pickpocketloot` = 0, `skinloot` = 0, `KillCredit1` = 0, `KillCredit2` = 0;
ALTER TABLE `procedurality_rare_slots` DROP COLUMN `slot_n`;
INSERT INTO `creature_template` SELECT * FROM `procedurality_rare_slots`;
DROP TEMPORARY TABLE `procedurality_rare_slots`;

INSERT INTO `creature_template_model` (`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`, `VerifiedBuild`)
  SELECT `entry`, 0, (SELECT `CreatureDisplayID` FROM `creature_template_model` WHERE `CreatureID` = 38 AND `Idx` = 0), 1, 1, 0
  FROM `creature_template` WHERE `entry` BETWEEN 190100 AND 191099;
