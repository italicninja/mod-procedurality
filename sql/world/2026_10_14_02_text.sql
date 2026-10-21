-- Procedurality (mod-procedurality) (wowproject docs/procedurality.md): text for generated bounties.
-- kind: title, objectives (quest log), details (giver's dialog), request (turn-in, not done yet),
-- reward (turn-in, done), say (the giver says it when the bounty is posted).
-- type_mask: creature types the row fits, bit = 1 << creature_template.type (2 beast, 4 dragonkin,
-- 8 demon, 16 elemental, 32 giant, 64 undead, 128 humanoid, 512 mechanical); 0 = any.
-- Placeholders, filled by the server: $target, $targets (plural), $count, $area (subzone, else zone),
-- $zone, $giver. The client fills $N (name), $C (class), $R (race), $B (line break).
CREATE TABLE IF NOT EXISTS `procedurality_text` (
  `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
  `kind` VARCHAR(16) NOT NULL,
  `type_mask` INT UNSIGNED NOT NULL DEFAULT 0,
  `weight` INT UNSIGNED NOT NULL DEFAULT 1,
  `text` TEXT NOT NULL,
  PRIMARY KEY (`id`),
  KEY `kind` (`kind`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

DELETE FROM `procedurality_text`;
INSERT INTO `procedurality_text` (`kind`, `type_mask`, `weight`, `text`) VALUES
-- titles
('title', 0, 3, 'Bounty: $targets'),
('title', 0, 2, 'Trouble near $area'),
('title', 0, 1, 'A Price on $targets'),
('title', 2, 2, 'Culling the $targets'),
('title', 2, 1, 'Too Many $targets'),
('title', 128, 2, 'Clearing $area'),
('title', 128, 1, 'Wanted: $targets'),
('title', 64, 2, 'Rest for the Restless'),
('title', 64, 1, 'The Dead of $area'),
('title', 16, 1, 'Unbound Elements'),
('title', 512, 1, 'Scrap Duty'),
-- quest log objectives
('objectives', 0, 3, 'Kill $count $targets near $area, then return to $giver.'),
('objectives', 0, 1, '$giver wants $count $targets near $area dead. Return when it''s done.'),
-- giver dialog
('details', 0, 2, 'The $targets near $area have gone unchecked for too long, $N. Nobody has been out that way in days, and they''re spreading.$B$BThin them out, $count should do it, and I''ll see you paid.'),
('details', 0, 1, 'You look like you can handle yourself, $C. There''s coin in it for whoever deals with the $targets near $area.$B$BBring me word when $count of them are down.'),
('details', 2, 2, 'The $targets near $area have been breeding without anything to cull them. They''ve started taking livestock, and soon it''ll be travellers.$B$BHunt down $count of them, $N.'),
('details', 2, 1, 'Have you heard them at night? The $targets out near $area. There are more of them every week.$B$BKill $count and the rest will think twice about coming closer.'),
('details', 128, 2, 'A band of $targets has dug in near $area. They''ve been robbing anyone who passes, and the guards can''t spare the men.$B$BBreak them up, $N. $count of them should send the rest running.'),
('details', 128, 1, 'I''ll be blunt, $C: the $targets near $area are a problem I''m paying to make go away. $count of them, and the bounty is yours.'),
('details', 64, 2, 'The dead don''t rest easy near $area. $targets walk there at all hours, and the living have stopped going.$B$BPut $count of them down for good, $N.'),
('details', 16, 1, 'Something has stirred the $targets near $area. Whatever the cause, they''re a danger to anyone nearby.$B$BDisperse $count of them and come back to me.'),
('details', 512, 1, 'Those $targets near $area have gone haywire. Nobody remembers who built them, and nobody can switch them off.$B$BBreak $count of them, $N.'),
-- turn-in, not done
('request', 0, 2, 'Have you dealt with the $targets near $area?'),
('request', 0, 1, 'The bounty stands until $count of them are down, $N.'),
-- turn-in, done
('reward', 0, 2, 'Good work, $N. That should keep $area quiet for a while. Here, you''ve earned this.'),
('reward', 0, 1, 'Done already? I''ll admit I had my doubts, $C. Here''s the bounty, as promised.'),
('reward', 64, 1, 'May they stay down this time. Thank you, $N.'),
-- giver announcement when posted
('say', 0, 2, 'Bounty posted! $targets near $area. Speak to me if you want the work.'),
('say', 0, 1, 'Coin for anyone who''ll deal with the $targets near $area!'),
('say', 128, 1, 'I''m paying for every one of those $targets near $area. Who''s brave enough?');
