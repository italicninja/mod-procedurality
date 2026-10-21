-- Procedurality (mod-procedurality) (wowproject docs/procedurality.md): Wanted texts, take 2. The generic (type 0) lines no
-- longer call the rare "it" (Wanted rares are often people); "it" lines are limited to beasts and monsters
-- (type_mask 574 = beast, dragonkin, demon, elemental, giant, mechanical). Replaces the wanted_* rows of
-- 2026_10_14_05_procedurality_text_wanted.sql; the rare_* name parts stay.
DELETE FROM `procedurality_text` WHERE `kind` LIKE 'wanted\_%';
INSERT INTO `procedurality_text` (`kind`, `type_mask`, `weight`, `text`) VALUES
('wanted_title', 0, 3, 'Wanted: $rare'),
('wanted_title', 0, 1, 'The Hunt for $rare'),
('wanted_title', 2, 1, 'The Beast of $area'),
('wanted_objectives', 0, 2, 'Kill $rare near $area, then return to $giver.'),
('wanted_objectives', 0, 1, '$giver has put a price on $rare. Track $rare down near $area, then return.'),
('wanted_details', 2, 2, 'There''s something out near $area that isn''t like the other $targets. Bigger. Meaner. The hunters call it $rare, and it''s killed two of them already.$B$BTrack it down, $N. The reward is good, and I don''t want to send anyone else after it.'),
('wanted_details', 2, 1, 'Folk near $area swear a monster runs with the $targets there. They call it $rare. I''d laugh, if I hadn''t seen the tracks myself.$B$BIt''ll take more than one arrow, $C. Bring a friend if you can.'),
('wanted_details', 128, 2, 'Every band of $targets has someone giving the orders. The ones near $area answer to $rare, and as long as $rare lives, they''ll keep coming back.$B$BCut off the head, $N. There''s a reward on it, and I''ll pay it gladly.'),
('wanted_details', 128, 1, '$rare. That name is on half the complaints that cross my desk. Last seen near $area, with the $targets.$B$BI want $rare gone, $C. Dead is fine.'),
('wanted_details', 64, 1, 'The dead near $area have a master: $rare, older and crueler than the rest. Put it down, $N, and perhaps the others will rest.'),
('wanted_details', 574, 1, 'Have you heard of $rare? It was seen near $area, among the $targets. Whatever it is, it is dangerous, and it has a price on its head.$B$BDon''t go alone if you can help it, $N.'),
('wanted_details', 0, 1, 'Have you heard the name $rare? Last seen near $area, among the $targets, and dangerous enough that there''s a price on that head.$B$BDon''t go alone if you can help it, $N.'),
('wanted_request', 0, 2, 'Is $rare dead?'),
('wanted_request', 0, 1, 'The reward on $rare still stands, $N.'),
('wanted_reward', 0, 2, 'So $rare is finally dead. Well done, $N. The reward is yours, and you''ve earned every coin.'),
('wanted_reward', 128, 1, 'Without $rare, those $targets are just a rabble. Good work, $C. Here''s what I promised.'),
('wanted_say', 0, 2, 'Wanted: $rare, last seen near $area. There''s a reward!'),
('wanted_say', 0, 1, 'There''s a price on the head of $rare! Ask me if you''re brave enough.');
