-- Procedurality (mod-procedurality), milestone 2 (wowproject docs/procedurality.md): Wanted rares. kind 0 = bounty,
-- 1 = Wanted; a Wanted row names its rare's reserved creature_template entry, which is rebuilt from
-- target_entry at startup.
ALTER TABLE `procedurality_bounty`
  ADD COLUMN `kind` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  ADD COLUMN `rare_entry` INT UNSIGNED NOT NULL DEFAULT 0,
  ADD COLUMN `rare_name` VARCHAR(100) NOT NULL DEFAULT '',
  ADD COLUMN `rare_level` TINYINT UNSIGNED NOT NULL DEFAULT 0;
