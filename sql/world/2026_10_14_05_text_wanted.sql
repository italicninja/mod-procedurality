-- Procedurality (mod-procedurality), milestone 2 (wowproject docs/procedurality.md): text for Wanted rares.
-- Same table and rules as 2026_10_14_02_procedurality_text.sql. Extra placeholder: $rare (the rare's name).
-- Rare names: rare_pattern, filled with $prefix/$suffix (joined into one word), $first/$last, $epithet.
-- type_mask bits: 2 beast, 4 dragonkin, 8 demon, 16 elemental, 32 giant, 64 undead, 128 humanoid, 512 mechanical.
ALTER TABLE `procedurality_text` MODIFY `kind` VARCHAR(32) NOT NULL;

DELETE FROM `procedurality_text` WHERE `kind` LIKE 'wanted\_%' OR `kind` LIKE 'rare\_%';
INSERT INTO `procedurality_text` (`kind`, `type_mask`, `weight`, `text`) VALUES
-- quest
('wanted_title', 0, 3, 'Wanted: $rare'),
('wanted_title', 0, 1, 'The Hunt for $rare'),
('wanted_title', 2, 1, 'The Beast of $area'),
('wanted_objectives', 0, 2, 'Kill $rare near $area, then return to $giver.'),
('wanted_objectives', 0, 1, '$giver has put a price on $rare. Find it near $area and bring it down.'),
('wanted_details', 2, 2, 'There''s something out near $area that isn''t like the other $targets. Bigger. Meaner. The hunters call it $rare, and it''s killed two of them already.$B$BTrack it down, $N. The reward is good, and I don''t want to send anyone else after it.'),
('wanted_details', 2, 1, 'Folk near $area swear a monster runs with the $targets there. They call it $rare. I''d laugh, if I hadn''t seen the tracks myself.$B$BIt''ll take more than one arrow, $C. Bring a friend if you can.'),
('wanted_details', 128, 2, 'Every band of $targets has someone giving the orders. The ones near $area answer to $rare, and as long as $rare lives, they''ll keep coming back.$B$BCut off the head, $N. There''s a reward on it, and I''ll pay it gladly.'),
('wanted_details', 128, 1, '$rare. That name is on half the complaints that cross my desk. Last seen near $area, with the $targets.$B$BI want it gone, $C. Dead is fine.'),
('wanted_details', 64, 1, 'The dead near $area have a master: $rare, older and crueler than the rest. Put it down, $N, and perhaps the others will rest.'),
('wanted_details', 0, 1, 'Have you heard of $rare? It was seen near $area, among the $targets. Whatever it is, it is dangerous, and it has a price on its head.$B$BDon''t go alone if you can help it, $N.'),
('wanted_request', 0, 2, 'Is $rare dead?'),
('wanted_request', 0, 1, 'The reward on $rare still stands, $N.'),
('wanted_reward', 0, 2, 'So $rare is finally dead. Well done, $N. The reward is yours, and you''ve earned every coin.'),
('wanted_reward', 128, 1, 'Without $rare, those $targets are just a rabble. Good work, $C. Here''s what I promised.'),
('wanted_say', 0, 2, 'Wanted: $rare, last seen near $area. There''s a reward!'),
('wanted_say', 0, 1, 'There''s a price on the head of $rare! Ask me if you''re brave enough.'),
-- name patterns
('rare_pattern', 2, 3, '$prefix$suffix'),
('rare_pattern', 2, 2, '$prefix$suffix the $epithet'),
('rare_pattern', 2, 1, 'Old $prefix$suffix'),
('rare_pattern', 128, 3, '$first $last'),
('rare_pattern', 128, 2, '$first the $epithet'),
('rare_pattern', 64, 2, '$first the $epithet'),
('rare_pattern', 64, 1, '$first $last'),
('rare_pattern', 4, 1, '$prefix$suffix the $epithet'),
('rare_pattern', 8, 1, '$prefix$suffix the $epithet'),
('rare_pattern', 16, 1, '$prefix$suffix'),
('rare_pattern', 32, 1, '$prefix$suffix the $epithet'),
('rare_pattern', 512, 1, 'Unit $prefix-$suffix'),
-- one-word names (beasts and monsters): prefix + suffix
('rare_prefix', 0, 1, 'Grim'), ('rare_prefix', 0, 1, 'Black'), ('rare_prefix', 0, 1, 'Grey'), ('rare_prefix', 0, 1, 'Scar'),
('rare_prefix', 0, 1, 'Blood'), ('rare_prefix', 0, 1, 'Rot'), ('rare_prefix', 0, 1, 'Ash'), ('rare_prefix', 0, 1, 'Gore'),
('rare_prefix', 0, 1, 'Night'), ('rare_prefix', 0, 1, 'Bone'), ('rare_prefix', 0, 1, 'Iron'), ('rare_prefix', 0, 1, 'Mud'),
('rare_prefix', 0, 1, 'Frost'), ('rare_prefix', 0, 1, 'Thorn'), ('rare_prefix', 0, 1, 'Dread'), ('rare_prefix', 0, 1, 'Moss'),
('rare_prefix', 512, 3, 'XT'), ('rare_prefix', 512, 3, 'KX'), ('rare_prefix', 512, 2, 'Mk'),
('rare_suffix', 2, 2, 'fang'), ('rare_suffix', 2, 2, 'claw'), ('rare_suffix', 2, 1, 'maw'), ('rare_suffix', 2, 1, 'hide'),
('rare_suffix', 2, 1, 'tusk'), ('rare_suffix', 2, 1, 'snout'), ('rare_suffix', 2, 1, 'pelt'), ('rare_suffix', 2, 1, 'howl'),
('rare_suffix', 4, 1, 'scale'), ('rare_suffix', 4, 1, 'wing'), ('rare_suffix', 8, 1, 'horn'), ('rare_suffix', 8, 1, 'brand'),
('rare_suffix', 16, 1, 'heart'), ('rare_suffix', 16, 1, 'core'), ('rare_suffix', 16, 1, 'shard'),
('rare_suffix', 32, 1, 'fist'), ('rare_suffix', 32, 1, 'back'),
('rare_suffix', 512, 1, '7'), ('rare_suffix', 512, 1, '9'), ('rare_suffix', 512, 1, '12'), ('rare_suffix', 512, 1, '40'),
-- people: first + last
('rare_first', 128, 1, 'Marla'), ('rare_first', 128, 1, 'Bosk'), ('rare_first', 128, 1, 'Garrick'), ('rare_first', 128, 1, 'Edda'),
('rare_first', 128, 1, 'Fenwick'), ('rare_first', 128, 1, 'Grul'), ('rare_first', 128, 1, 'Hesk'), ('rare_first', 128, 1, 'Jorra'),
('rare_first', 128, 1, 'Kragg'), ('rare_first', 128, 1, 'Lysa'), ('rare_first', 128, 1, 'Mog'), ('rare_first', 128, 1, 'Nerra'),
('rare_first', 128, 1, 'Ollie'), ('rare_first', 128, 1, 'Pike'), ('rare_first', 128, 1, 'Rusk'), ('rare_first', 128, 1, 'Snag'),
('rare_first', 128, 1, 'Tam'), ('rare_first', 128, 1, 'Varn'), ('rare_first', 128, 1, 'Wedge'), ('rare_first', 128, 1, 'Yorl'),
('rare_first', 64, 1, 'Aldric'), ('rare_first', 64, 1, 'Morwen'), ('rare_first', 64, 1, 'Bertram'), ('rare_first', 64, 1, 'Sorrow'),
('rare_first', 64, 1, 'Hollis'), ('rare_first', 64, 1, 'Ysolde'),
('rare_last', 128, 1, 'Ironvein'), ('rare_last', 128, 1, 'Blackhand'), ('rare_last', 128, 1, 'Mudfoot'), ('rare_last', 128, 1, 'Grimtooth'),
('rare_last', 128, 1, 'Coldwater'), ('rare_last', 128, 1, 'Redbrook'), ('rare_last', 128, 1, 'Ashby'), ('rare_last', 128, 1, 'Skullsplitter'),
('rare_last', 128, 1, 'Stonejaw'), ('rare_last', 128, 1, 'Knuckles'), ('rare_last', 128, 1, 'Two-Knives'), ('rare_last', 128, 1, 'Hollowell'),
('rare_last', 64, 1, 'the Unburied'), ('rare_last', 64, 1, 'Graves'), ('rare_last', 64, 1, 'Mournwood'),
-- epithets
('rare_epithet', 0, 1, 'Cruel'), ('rare_epithet', 0, 1, 'Hungry'), ('rare_epithet', 0, 1, 'Scarred'), ('rare_epithet', 0, 1, 'Old'),
('rare_epithet', 2, 1, 'Mangy'), ('rare_epithet', 2, 1, 'Ravenous'), ('rare_epithet', 2, 1, 'One-Eyed'), ('rare_epithet', 2, 1, 'Swift'),
('rare_epithet', 2, 1, 'Man-Eater'), ('rare_epithet', 128, 1, 'Butcher'), ('rare_epithet', 128, 1, 'Sly'), ('rare_epithet', 128, 1, 'Smiler'),
('rare_epithet', 128, 1, 'Thief'), ('rare_epithet', 128, 1, 'Loud'), ('rare_epithet', 128, 1, 'Wolf'), ('rare_epithet', 64, 1, 'Restless'),
('rare_epithet', 64, 1, 'Hollow'), ('rare_epithet', 64, 1, 'Faceless'), ('rare_epithet', 8, 1, 'Burning'), ('rare_epithet', 16, 1, 'Unbound'),
('rare_epithet', 32, 1, 'Mountain'), ('rare_epithet', 4, 1, 'Ancient');
