#!/usr/bin/env python3
"""Turns the itemization bundle's base_items.json into POE2 Mashup item bases.

Usage: tools/poe2_bases.py path/to/base_items.json

Adds a row to mods/poe2/txtdata/items/itemdat.tsv for each bundle base of the classes the game
has (helmets, shields, foci, rings, amulets and the weapon lines Diablo can wield), and writes
mods/poe2/txtdata/poe2/base_meta.tsv with what the game reads besides itemdat: the defence line,
how quality works, and the implicits (including the cast speed of focused and quick bases).

Rows are matched by the id in the first column, so running it again updates them in place and
never moves a row: saved items refer to bases by their row. The Diablo bases of those classes stop
dropping (old items keep working), and each new base takes the unique type of the nearest Diablo
base of its kind, so Diablo's uniques keep dropping until the bundle's own uniques replace them.

Levels, damage and defence are POE2's own numbers (harder monsters plan, step 10b): drop levels are
area levels, weapons hit for their base's damage, and a base's defence is its armour plus evasion
plus a third of its energy shield (the game shows defence points as three energy shield each). The
body armours, which have no id, are matched by name and take POE2's numbers from
research/poe2-body-armours.md. base_meta.tsv's split column says how a hybrid's defence divides, and
its level column the character level a base needs, like POE2's: the bundle's required level, POE2's
item level for rings and amulets, and the table's level for body armours.
and its block column is a shield's own chance to block, in percent (empty for everything else).
Its aps column is a weapon's attacks per second in hundredths (POE2 attack speed).

Weapons are POE2's own bases (Curtis 2026-10-27), not the bundle's: each balanced bundle weapon becomes the POE2 base of
its class nearest to it in level (REAL_WEAPON_BASES), with that base's name, level, requirements, damage and attack speed
from tools/poe2_real_weapons.tsv (Path of Building 2's data). The bundle's heavy and swift variants and all crossbows stop
dropping; the heavy and swift rows take their balanced sibling's numbers, so saved ones stay sane.
"""

import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ITEMDAT = os.path.join(ROOT, 'mods', 'poe2', 'txtdata', 'items', 'itemdat.tsv')
META = os.path.join(ROOT, 'mods', 'poe2', 'txtdata', 'poe2', 'base_meta.tsv')
REAL_WEAPONS = os.path.join(ROOT, 'tools', 'poe2_real_weapons.tsv')
ID_PREFIX = 'poe2:'

# Bundle names that clash with Diablo bases or each other.
RENAMES = {
	'one_hand_sword_balanced_falchion': 'Fine Falchion',
	'two_hand_axe_balanced_broad_axe': 'Fine Broad Axe',
	'two_hand_sword_balanced_claymore': 'Fine Claymore',
	'bow_balanced_short_bow': 'Fine Short Bow',
	'crossbow_balanced_siege_crossbow': 'Fine Siege Crossbow',
	'evasion_es_helmet_phantom_hood': 'Phantom Cowl',
	# POE2's own ring names (Path of Building 2's ring data, Curtis 2026-10-20).
	'lapis_ring': 'Lazuli Ring',
	'opal_ring': 'Pearl Ring',
	'gold_ring': 'Golden Hoop',
	# POE2's amulets, each bundle base going to the POE2 amulet with the nearest implicit (Curtis 2026-10-20).
	'azure_amulet': 'Lapis Amulet',
	'prismatic_amulet': 'Pearlescent Amulet',
	'vital_amulet': 'Bloodstone Amulet',
	'spirit_amulet': 'Solar Amulet',
	'keen_amulet': 'Stellar Amulet',
	'sage_amulet': 'Azure Amulet',
}

# POE2's own weapon bases (Curtis 2026-10-27): each balanced bundle weapon -> the POE2 base of its class nearest to it in
# level, in order and without repeats (the fewest levels apart over the class, bases without an implicit preferred, since
# the game has none of POE2's weapon implicits). Every class's first base is POE2's first.
REAL_WEAPON_BASES = {
	'one_hand_sword_balanced_shortsword': 'Shortsword', 'one_hand_sword_balanced_leafblade': 'Vampiric Blade',
	'one_hand_sword_balanced_soldier_sword': 'Charred Shortsword', 'one_hand_sword_balanced_falchion': 'Falchion',
	'one_hand_sword_balanced_knightblade': 'Runic Shortsword', 'one_hand_sword_balanced_dueling_sword': 'Messer',
	'one_hand_sword_balanced_runed_sabre': 'Commander Sword', 'one_hand_sword_balanced_imperial_edge': 'Dark Blade',
	'two_hand_sword_balanced_longsword': 'Corroded Longsword', 'two_hand_sword_balanced_greatblade': 'Blessed Claymore',
	'two_hand_sword_balanced_mercenary_blade': 'Rippled Greatsword', 'two_hand_sword_balanced_claymore': 'Obsidian Greatsword',
	'two_hand_sword_balanced_war_greatsword': 'Ancient Greatblade', 'two_hand_sword_balanced_duelist_greatblade': 'Flanged Greatblade',
	'two_hand_sword_balanced_runed_greatsword': 'Regalia Longsword', 'two_hand_sword_balanced_sovereign_blade': 'Ultra Greatsword',
	'one_hand_axe_balanced_hatchet': 'Dull Hatchet', 'one_hand_axe_balanced_bearded_axe': 'Bearded Axe',
	'one_hand_axe_balanced_raider_axe': 'Crescent Axe', 'one_hand_axe_balanced_ironcleaver': 'Carving Hatchet',
	'one_hand_axe_balanced_war_axe': 'Boarding Hatchet', 'one_hand_axe_balanced_siege_axe': 'Battle Axe',
	'one_hand_axe_balanced_bloodiron_axe': 'Profane Cleaver', 'one_hand_axe_balanced_kingsplitter': 'Dread Hatchet',
	'two_hand_axe_balanced_woodsman_s_axe': 'Splitting Greataxe', 'two_hand_axe_balanced_execution_axe': 'Arched Greataxe',
	'two_hand_axe_balanced_broad_axe': 'Elegant Glaive', 'two_hand_axe_balanced_headsman_axe': 'Rending Halberd',
	'two_hand_axe_balanced_great_cleaver': 'Reaver Glaive', 'two_hand_axe_balanced_siegebreaker': 'Ceremonial Halberd',
	'two_hand_axe_balanced_bloodoak_axe': 'Monument Greataxe', 'two_hand_axe_balanced_worldsplitter': 'Vile Greataxe',
	'one_hand_mace_balanced_cudgel': 'Wooden Club', 'one_hand_mace_balanced_flanged_mace': 'Slim Mace',
	'one_hand_mace_balanced_war_club': 'Plated Mace', 'one_hand_mace_balanced_spiked_mace': 'Brigand Mace',
	'one_hand_mace_balanced_battle_mace': 'Flared Mace', 'one_hand_mace_balanced_siege_hammer': 'Bandit Mace',
	'one_hand_mace_balanced_dread_maul': 'Crown Mace', 'one_hand_mace_balanced_judicator': 'Marauding Mace',
	'two_hand_mace_balanced_sledge': 'Felled Greatclub', 'two_hand_mace_balanced_great_mace': 'Studded Greatclub',
	'two_hand_mace_balanced_war_mallet': 'Temple Maul', 'two_hand_mace_balanced_stonebreaker': 'Leaden Greathammer',
	'two_hand_mace_balanced_battle_hammer': 'Pointed Maul', 'two_hand_mace_balanced_siege_maul': 'Heavy Greathammer',
	'two_hand_mace_balanced_titan_hammer': 'Sacred Maul', 'two_hand_mace_balanced_cataclysm': 'Massive Greathammer',
	'dagger_balanced_shiv': 'Glass Shank', 'dagger_balanced_dirk': 'Simple Dagger', 'dagger_balanced_rondel': 'Engraved Knife',
	'dagger_balanced_stiletto': 'Obsidian Dagger', 'dagger_balanced_kris': 'Kris Knife', 'dagger_balanced_assassin_dirk': 'Parrying Dagger',
	'dagger_balanced_nightglass_knife': 'Arcane Dirk', 'dagger_balanced_voidneedle': 'Cinquedea',
	'quarterstaff_balanced_ash_staff': 'Wrapped Quarterstaff', 'quarterstaff_balanced_banded_staff': 'Gothic Quarterstaff',
	'quarterstaff_balanced_ironwood_staff': 'Crescent Quarterstaff', 'quarterstaff_balanced_battle_staff': 'Slicing Quarterstaff',
	'quarterstaff_balanced_monk_staff': 'Barbarous Quarterstaff', 'quarterstaff_balanced_temple_staff': 'Bladed Quarterstaff',
	'quarterstaff_balanced_celestial_staff': 'Lunar Quarterstaff', 'quarterstaff_balanced_ascendant_staff': 'Dreaming Quarterstaff',
	'bow_balanced_short_bow': 'Crude Bow', 'bow_balanced_yew_bow': 'Recurve Bow', 'bow_balanced_hunting_bow': 'Composite Bow',
	'bow_balanced_recurve_bow': 'Zealot Bow', 'bow_balanced_war_bow': 'Tribal Bow', 'bow_balanced_longbow': 'Militant Bow',
	'bow_balanced_runewood_bow': 'Cavalry Bow', 'bow_balanced_starbow': 'Warmonger Bow',
}
# POE2 weapon names Diablo's bases already have.
REAL_WEAPON_RENAMES = {
	'Falchion': 'Fine Falchion', 'Battle Axe': 'Fine Battle Axe', 'Composite Bow': 'Fine Composite Bow',
	'Wrapped Quarterstaff': 'Fine Wrapped Quarterstaff', 'Gothic Quarterstaff': 'Fine Gothic Quarterstaff',
	'Scimitar': 'Fine Scimitar', 'Spiked Club': 'Fine Spiked Club',
}
# The bundle's heavy and swift weapons and its crossbows (Diablo has no crossbow animation) no longer drop.
RETIRED_WEAPON_IDENTITIES = {'heavy', 'swift'}
RETIRED_WEAPON_CLASSES = {'crossbow'}
# Diablo weapon unique types no base above carries, each on a POE2 base of its kind that none uses, near its uniques'
# levels: (id, class, POE2 base, unique type). They go after the other rows, with the Diablo base's art, so every
# Diablo and Hellfire weapon unique has a base that drops.
DIABLO_UNIQUE_TYPE_BASES = [
	('one_hand_sword_broadsword', 'one_hand_sword', 'Broadsword', 'FALCHION'),
	('one_hand_sword_scimitar', 'one_hand_sword', 'Scimitar', 'CLAYMORE'),
	('one_hand_sword_sickle_sword', 'one_hand_sword', 'Sickle Sword', 'LONGSWR'),
	('two_hand_axe_executioner_greataxe', 'two_hand_axe', 'Executioner Greataxe', 'LARGEAXE'),
	('two_hand_axe_savage_greataxe', 'two_hand_axe', 'Savage Greataxe', 'BATTLEAXE'),
	('one_hand_mace_spiked_club', 'one_hand_mace', 'Spiked Club', 'MACE'),
	('one_hand_mace_warpick', 'one_hand_mace', 'Warpick', 'MORNSTAR'),
	('one_hand_mace_construct_hammer', 'one_hand_mace', 'Construct Hammer', 'WARHAMMER'),
	('bow_shortbow', 'bow', 'Shortbow', 'LONGBOW'),
	('quarterstaff_steelpoint_quarterstaff', 'quarterstaff', 'Steelpoint Quarterstaff', 'COMPSTAFF'),
]
# Diablo unique types with a base of their own: The Butcher's Cleaver and Griswold's Edge.
OWN_BASE_UNIQUE_TYPES = {'CLEAVER', 'GRISWOLD'}

# The caster weapons rework (research/sorc-run2/caster-weapons-plan-v2.md section 3): the bundle's Focused and Quick
# staves and sceptres stop dropping and take these classes, so saved ones keep working as before.
LEGACY_CASTER_CLASSES = {'staff': 'legacy_staff', 'sceptre': 'legacy_sceptre'}

# The rework's bases, POE2's own (Path of Building 2, caster-weapons-sources-v1.md sections 3 and 4), after every other
# row: (class, id, art, unique type, name, level, durability, str, int, dex, min damage, max damage, grants, element,
# aps, crit, implicit). Sceptres, staves and wands hit for nothing; each grants a skill, and staves and wands lock their
# element spell modifiers to an element. Quarterstaves take the early-weapon damage rule; crit is POE2's in tenths of a
# percent (the game halves it), set only here. The new ones are identity poe2, so bare fists keep following the
# bundle's nine.
CASTER_BASES = [
	('sceptre', 'sceptre_rattling_sceptre', 'MACE', 'NONE', 'Fine Rattling Sceptre', 1, 35, 0, 0, 0, 0, 0, 'SkeletalWarriors', '', '', '', ''),
	('sceptre', 'sceptre_stoic_sceptre', 'MACE', 'NONE', 'Fine Stoic Sceptre', 6, 40, 0, 12, 0, 0, 0, 'ManaShield', '', '', '', ''),
	('sceptre', 'sceptre_lupine_sceptre', 'MACE', 'NONE', 'Lupine Sceptre', 12, 40, 0, 24, 0, 0, 0, 'SkeletalWarriors', '', '', '', ''),
	('sceptre', 'sceptre_omen_sceptre', 'MORNING_STAR', 'NONE', 'Fine Omen Sceptre', 16, 45, 12, 25, 0, 0, 0, 'OverwhelmingPresence', '', '', '', ''),
	('sceptre', 'sceptre_ochre_sceptre', 'MORNING_STAR', 'NONE', 'Ochre Sceptre', 21, 45, 0, 40, 0, 0, 0, 'SkeletalWarriors', '', '', '', ''),
	('sceptre', 'sceptre_shrine_sceptre', 'MORNING_STAR', 'NONE', 'Shrine Sceptre', 26, 50, 17, 38, 0, 0, 0, 'PurityOfElements', '', '', '', ''),
	('sceptre', 'sceptre_devouring_sceptre', 'MORNING_STAR', 'NONE', 'Devouring Sceptre', 33, 50, 0, 60, 0, 0, 0, 'SkeletalWarriors', '', '', '', ''),
	('sceptre', 'sceptre_clasped_sceptre', 'WAR_HAMMER', 'NONE', 'Clasped Sceptre', 36, 50, 0, 65, 0, 0, 0, 'HeraldOfFrost', '', '', '', ''),
	('sceptre', 'sceptre_devotional_sceptre', 'WAR_HAMMER', 'NONE', 'Devotional Sceptre', 45, 55, 26, 63, 0, 0, 0, 'SkeletalWarriors', '', '', '', ''),
	('sceptre', 'sceptre_wrath_sceptre', 'WAR_HAMMER', 'NONE', 'Wrath Sceptre', 49, 55, 0, 87, 0, 0, 0, 'HeraldOfThunder', '', '', '', ''),
	('sceptre', 'sceptre_aromatic_sceptre', 'SPIKED_CLUB', 'NONE', 'Aromatic Sceptre', 52, 55, 0, 92, 0, 0, 0, 'SkeletalWarriors', '', '', '', ''),
	('sceptre', 'sceptre_pious_sceptre', 'SPIKED_CLUB', 'NONE', 'Pious Sceptre', 58, 60, 0, 102, 0, 0, 0, 'SkeletalWarriors', '', '', '', ''),
	('sceptre', 'sceptre_hallowed_sceptre', 'SPIKED_CLUB', 'NONE', 'Hallowed Sceptre', 65, 65, 0, 114, 0, 0, 0, 'SkeletalWarriors', '', '', '', ''),
	('staff', 'staff_ashen_staff', 'LONG_STAFF', 'SHORTSTAFF', 'Ashen Staff', 1, 35, 0, 0, 0, 0, 0, 'StaffBolt', 'fire', '', '', ''),
	('staff', 'staff_gelid_staff', 'LONG_STAFF', 'NONE', 'Gelid Staff', 1, 35, 0, 0, 0, 0, 0, 'FreezingShards', 'cold', '', '', ''),
	('staff', 'staff_voltaic_staff', 'LONG_STAFF', 'NONE', 'Voltaic Staff', 1, 35, 0, 0, 0, 0, 0, 'LightningBolt', 'lightning', '', '', ''),
	('staff', 'staff_spriggan_staff', 'LONG_STAFF', 'QUARSTAFF', 'Spriggan Staff', 11, 40, 0, 23, 0, 0, 0, 'StaffBolt', '', '', '', ''),
	('staff', 'staff_pyrophyte_staff', 'COMPOSITE_STAFF', 'LONGSTAFF', 'Pyrophyte Staff', 16, 45, 0, 31, 0, 0, 0, 'SolarOrb', 'fire', '', '', ''),
	('staff', 'staff_chiming_staff', 'COMPOSITE_STAFF', 'NONE', 'Chiming Staff', 25, 45, 0, 46, 0, 0, 0, 'ChimingBolt', '', '', '', ''),
	('staff', 'staff_rending_staff', 'COMPOSITE_STAFF', 'NONE', 'Rending Staff', 33, 50, 0, 60, 0, 0, 0, 'Soulrend', 'physical', '', '', ''),
	('staff', 'staff_reaping_staff', 'COMPOSITE_STAFF', 'NONE', 'Reaping Staff', 38, 50, 0, 68, 0, 0, 0, 'Reap', 'physical', '', '', ''),
	('staff', 'staff_icicle_staff', 'WAR_STAFF', 'NONE', 'Icicle Staff', 45, 55, 0, 80, 0, 0, 0, 'FrostLance', 'cold', '', '', ''),
	('staff', 'staff_roaring_staff', 'WAR_STAFF', 'NONE', 'Roaring Staff', 49, 55, 0, 87, 0, 0, 0, 'EchoingBlast', '', '', '', ''),
	('staff', 'staff_paralysing_staff', 'WAR_STAFF', 'NONE', 'Paralysing Staff', 52, 55, 0, 92, 0, 0, 0, 'EnervatingNova', 'lightning', '', '', ''),
	('staff', 'staff_sanctified_staff', 'WAR_STAFF', 'NONE', 'Sanctified Staff', 56, 60, 0, 99, 0, 0, 0, 'ConsecratedGround', '', '', '', ''),
	('staff', 'staff_dark_staff', 'STAFF_OF_LAZARUS', 'NONE', 'Dark Staff', 65, 65, 0, 114, 0, 0, 0, 'DarkPact', '', '', '', ''),
	('staff', 'staff_ravenous_staff', 'STAFF_OF_LAZARUS', 'NONE', 'Ravenous Staff', 65, 65, 0, 114, 0, 0, 0, 'FeastOfFlesh', '', '', '', ''),
	('wand', 'wand_withered_wand', 'CLUB', 'NONE', 'Withered Wand', 1, 35, 0, 0, 0, 0, 0, 'WitheringBolt', 'physical', '', '', ''),
	('wand', 'wand_bone_wand', 'CLUB', 'NONE', 'Bone Wand', 1, 35, 0, 0, 0, 0, 0, 'BoneBolt', 'physical', '', '', ''),
	('wand', 'wand_attuned_wand', 'CLUB', 'NONE', 'Attuned Wand', 1, 35, 0, 0, 0, 0, 0, 'ManaDrain', '', '', '', ''),
	('wand', 'wand_siphoning_wand', 'CLUB', 'NONE', 'Siphoning Wand', 11, 40, 0, 23, 0, 0, 0, 'PowerSiphon', '', '', '', ''),
	('wand', 'wand_volatile_wand', 'CLUB', 'NONE', 'Volatile Wand', 16, 45, 0, 31, 0, 0, 0, 'VolatileBolt', 'fire', '', '', ''),
	('wand', 'wand_galvanic_wand', 'CLUB', 'NONE', 'Galvanic Wand', 25, 45, 0, 46, 0, 0, 0, 'GalvanicBolt', 'lightning', '', '', ''),
	('wand', 'wand_acrid_wand', 'CLUB', 'NONE', 'Acrid Wand', 33, 50, 0, 60, 0, 0, 0, 'Decompose', '', '', '', ''),
	('wand', 'wand_offering_wand', 'CLUB', 'NONE', 'Offering Wand', 38, 50, 0, 68, 0, 0, 0, 'Exsanguinate', 'physical', '', '', ''),
	('wand', 'wand_frigid_wand', 'CLUB', 'NONE', 'Frigid Wand', 45, 55, 0, 80, 0, 0, 0, 'FrostBolt', 'cold', '', '', ''),
	('wand', 'wand_torture_wand', 'CLUB', 'NONE', 'Torture Wand', 49, 55, 0, 87, 0, 0, 0, 'TormentBolt', '', '', '', ''),
	('wand', 'wand_critical_wand', 'CLUB', 'NONE', 'Critical Wand', 52, 55, 0, 92, 0, 0, 0, 'CriticalBolt', '', '', '', ''),
	('wand', 'wand_primordial_wand', 'CLUB', 'NONE', 'Primordial Wand', 56, 60, 0, 99, 0, 0, 0, 'WitheringRay', 'physical', '', '', ''),
	('wand', 'wand_dueling_wand', 'CLUB', 'NONE', 'Dueling Wand', 65, 65, 0, 114, 0, 0, 0, 'TwinBolts', '', '', '', ''),
	# Chaos damage plan step 13: chaos wands and staves, new bases with the chaos element, so they roll the chaos spell
	# families; the older bases granting these skills keep their element and pools.
	('wand', 'wand_blighted_wand', 'CLUB', 'NONE', 'Blighted Wand', 20, 45, 0, 38, 0, 0, 0, 'WitheringBolt', 'chaos', '', '', ''),
	('wand', 'wand_rotting_wand', 'CLUB', 'NONE', 'Rotting Wand', 42, 50, 0, 75, 0, 0, 0, 'Decompose', 'chaos', '', '', ''),
	('wand', 'wand_gloom_wand', 'CLUB', 'NONE', 'Gloom Wand', 60, 60, 0, 106, 0, 0, 0, 'WitheringRay', 'chaos', '', '', ''),
	('staff', 'staff_hollow_staff', 'COMPOSITE_STAFF', 'NONE', 'Hollow Staff', 41, 50, 0, 73, 0, 0, 0, 'Soulrend', 'chaos', '', '', ''),
	('staff', 'staff_void_staff', 'STAFF_OF_LAZARUS', 'NONE', 'Void Staff', 60, 60, 0, 106, 0, 0, 0, 'DarkPact', 'chaos', '', '', ''),
	('quarterstaff', 'quarterstaff_long_quarterstaff', 'SHORT_STAFF', 'NONE', 'Fine Long Quarterstaff', 1, 35, 0, 0, 9, 9, 18, '', '', 140, 100, ''),
	('quarterstaff', 'quarterstaff_barrier_quarterstaff', 'COMPOSITE_STAFF', 'NONE', 'Barrier Quarterstaff', 37, 50, 0, 22, 52, 35, 58, '', '', 140, 100, 'BlockChance|12|18|+{0}% to Block chance'),
	('quarterstaff', 'quarterstaff_hefty_quarterstaff', 'COMPOSITE_STAFF', 'NONE', 'Hefty Quarterstaff', 45, 55, 0, 26, 63, 39, 81, '', '', 130, 100, 'DazeChance|20|50|{0}% chance to Daze on Hit'),
	('quarterstaff', 'quarterstaff_reaching_quarterstaff', 'COMPOSITE_STAFF', 'NONE', 'Reaching Quarterstaff', 45, 55, 0, 26, 63, 35, 72, '', '', 140, 100, ''),
	('quarterstaff', 'quarterstaff_anima_quarterstaff', 'COMPOSITE_STAFF', 'NONE', 'Anima Quarterstaff', 52, 55, 0, 29, 72, 47, 79, '', '', 140, 100, ''),
	('quarterstaff', 'quarterstaff_waxing_quarterstaff', 'COMPOSITE_STAFF', 'NONE', 'Waxing Quarterstaff', 54, 60, 0, 30, 75, 39, 82, '', '', 150, 100, ''),
	('quarterstaff', 'quarterstaff_graceful_quarterstaff', 'COMPOSITE_STAFF', 'NONE', 'Graceful Quarterstaff', 56, 60, 0, 31, 77, 47, 87, '', '', 140, 100, ''),
	('quarterstaff', 'quarterstaff_guardian_quarterstaff', 'WAR_STAFF', 'NONE', 'Guardian Quarterstaff', 62, 65, 0, 34, 85, 52, 86, '', '', 140, 100, 'BlockChance|12|18|+{0}% to Block chance'),
	('quarterstaff', 'quarterstaff_wyrm_quarterstaff', 'WAR_STAFF', 'NONE', 'Wyrm Quarterstaff', 65, 65, 0, 36, 89, 57, 94, '', '', 140, 100, ''),
	('quarterstaff', 'quarterstaff_sinister_quarterstaff', 'WAR_STAFF', 'NONE', 'Sinister Quarterstaff', 67, 65, 0, 41, 104, 55, 91, '', '', 140, 120, ''),
	('quarterstaff', 'quarterstaff_skullcrusher_quarterstaff', 'WAR_STAFF', 'NONE', 'Skullcrusher Quarterstaff', 75, 65, 0, 50, 127, 59, 122, '', '', 130, 100, 'DazeChance|20|50|{0}% chance to Daze on Hit'),
	('quarterstaff', 'quarterstaff_striking_quarterstaff', 'WAR_STAFF', 'NONE', 'Striking Quarterstaff', 77, 65, 0, 50, 127, 53, 111, '', '', 140, 100, ''),
	('quarterstaff', 'quarterstaff_razor_quarterstaff', 'WAR_STAFF', 'NONE', 'Razor Quarterstaff', 77, 65, 0, 50, 127, 65, 108, '', '', 140, 100, ''),
	('quarterstaff', 'quarterstaff_aegis_quarterstaff', 'WAR_STAFF', 'NONE', 'Fine Aegis Quarterstaff', 79, 65, 0, 50, 127, 58, 97, '', '', 140, 100, 'BlockChance|12|18|+{0}% to Block chance'),
]
# Granted skills not built yet: their bases grant nothing until the skill exists (an unknown key is an error).
PENDING_GRANTS = set()
CASTER_CLASSES = {
	'staff': ('Weapon', 'Two-handed', 'Staff', 'Staff'),
	'wand': ('Weapon', 'One-handed', 'Mace', 'Wand'),
	'sceptre': ('Weapon', 'One-handed', 'Mace', 'Sceptre'),
	'quarterstaff': ('Weapon', 'Two-handed', 'Staff', 'Quarterstaff'),
}

# Rings and amulets follow POE2's lineup and drop levels rather than the bundle's: Iron and Lazuli first, then Ruby,
# Sapphire and Golden Hoop, Topaz, Pearl. POE2 has no Coral Ring, so it only carries its unique. Jewellery levels are
# POE2 item levels, not scaled to Diablo's (the game checks them against those).
POE2_JEWELLERY_LEVELS = {
	'iron_ring': 1, 'lapis_ring': 1, 'ruby_ring': 8, 'sapphire_ring': 12, 'gold_ring': 12, 'topaz_ring': 16, 'opal_ring': 32,
	'coral_ring': 20,
	'sage_amulet': 1, 'amber_amulet': 8, 'jade_amulet': 8, 'azure_amulet': 8, 'vital_amulet': 18, 'keen_amulet': 24,
	'spirit_amulet': 30, 'prismatic_amulet': 30,
}
RETIRED_BASES = {'coral_ring'}

# POE2 amulets the bundle lacks, added after its bases: (id, name, level).
POE2_EXTRA_AMULETS = [
	('crimson_amulet', 'Crimson Amulet', 1),
	('lunar_amulet', 'Lunar Amulet', 14),
	('gold_amulet', 'Gold Amulet', 35),
]

# POE2 rings the bundle lacks, added after its bases: (id, name, level, implicit). The Amethyst Ring is the chaos
# resistance ring (chaos damage plan step 5).
POE2_EXTRA_RINGS = [
	('amethyst_ring', 'Amethyst Ring', 30, 'ring_chaos'),
]

# bundle class -> (itemdat class, equip type, item type, short name, quality, unique-type source)
CLASSES = {
	'helmet': ('Armor', 'Helm', 'Helm', 'Helm', 'defence', 'Helm'),
	'shield': ('Armor', 'One-handed', 'Shield', 'Shield', 'defence', 'Shield'),
	'focus': ('Armor', 'One-handed', 'Shield', 'Focus', 'defence', None),
	'one_hand_sword': ('Weapon', 'One-handed', 'Sword', 'Sword', 'weapon', 'Sword1'),
	'two_hand_sword': ('Weapon', 'Two-handed', 'Sword', 'Sword', 'weapon', 'Sword2'),
	'one_hand_axe': ('Weapon', 'One-handed', 'Axe', 'Axe', 'weapon', 'Axe'),
	'two_hand_axe': ('Weapon', 'Two-handed', 'Axe', 'Axe', 'weapon', 'Axe'),
	'one_hand_mace': ('Weapon', 'One-handed', 'Mace', 'Mace', 'weapon', 'Mace1'),
	'two_hand_mace': ('Weapon', 'Two-handed', 'Mace', 'Maul', 'weapon', 'Mace2'),
	'dagger': ('Weapon', 'One-handed', 'Sword', 'Dagger', 'weapon', 'Dagger'),
	'quarterstaff': ('Weapon', 'Two-handed', 'Staff', 'Quarterstaff', 'weapon', 'Staff'),
	'bow': ('Weapon', 'Two-handed', 'Bow', 'Bow', 'weapon', 'Bow'),
	'crossbow': ('Weapon', 'Two-handed', 'Bow', 'Crossbow', 'weapon', 'Bow'),
	'staff': ('Weapon', 'Two-handed', 'Staff', 'Staff', 'caster', 'Staff'),
	'sceptre': ('Weapon', 'One-handed', 'Mace', 'Sceptre', 'caster', 'Mace1'),
	'ring': ('Misc', 'Ring', 'Ring', 'Ring', 'none', 'Ring'),
	'amulet': ('Misc', 'Amulet', 'Amulet', 'Amulet', 'none', 'Amulet'),
}

# Reused Diablo art, weakest tier first; a line takes them in turn across its 8 tiers.
CURSORS = {
	'armour_helmet': ['HELM', 'FULL_HELM', 'GREAT_HELM', 'OVERLORDS_HELM'],
	'evasion_helmet': ['CAP', 'SKULL_CAP', 'FOOLS_CREST', 'HARLEQUIN_CREST'],
	'energy_shield_helmet': ['ROYAL_CIRCLET', 'CROWN', 'THINKING_CAP', 'THE_UNDEAD_CROWN'],
	'armour_evasion_helmet': ['SKULL_CAP', 'HELM', 'FULL_HELM', 'HELM_OF_SPIRITS'],
	'armour_es_helmet': ['CROWN', 'GREAT_HELM', 'ROYAL_CIRCLET', 'THE_UNDEAD_CROWN'],
	'evasion_es_helmet': ['CAP', 'ROYAL_CIRCLET', 'THINKING_CAP', 'HARLEQUIN_CREST'],
	'tri_helmet': ['FULL_HELM', 'CROWN', 'HELM_OF_SPIRITS', 'VEIL_OF_STEEL'],
	'shield_armour': ['LARGE_SHIELD', 'TOWER_SHIELD', 'GOTHIC_SHIELD', 'BLACKOAK_SHIELD'],
	'shield_evasion': ['BUCKLER', 'SMALL_SHIELD', 'KITE_SHIELD', 'SPLIT_SKULL_SHIELD'],
	'shield_armour_evasion': ['SMALL_SHIELD', 'KITE_SHIELD', 'LARGE_SHIELD', 'DRAGONS_BREACH'],
	'shield_armour_es': ['KITE_SHIELD', 'BLACKOAK_SHIELD', 'GOTHIC_SHIELD', 'DRAGONS_BREACH'],
	'focus': ['BOOK_GREY', 'BOOK_RED', 'BOOK_BLUE', 'FUNGAL_TOME'],
	'one_hand_sword': ['SHORT_SWORD', 'SABRE', 'SCIMITAR', 'FALCHION', 'LONG_SWORD', 'BROAD_SWORD', 'BASTARD_SWORD', 'DIAMONDEDGE'],
	'two_hand_sword': ['CLAYMORE', 'BLADE', 'TWO_HANDED_SWORD', 'GREAT_SWORD', 'THE_EXECUTIONERS_BLADE'],
	'one_hand_axe': ['SMALL_AXE', 'AXE', 'CLEAVER', 'LARGE_AXE'],
	'two_hand_axe': ['LARGE_AXE', 'BROAD_AXE', 'BATTLE_AXE', 'GREAT_AXE', 'STONECLEAVER'],
	'one_hand_mace': ['CLUB', 'SPIKED_CLUB', 'MACE', 'MORNING_STAR', 'FLAIL', 'GRYPHONS_CLAW'],
	'two_hand_mace': ['MAUL', 'WAR_HAMMER', 'THE_GRANDFATHER', 'MAUL'],
	'dagger': ['DAGGER', 'GONNAGALS_DIRK', 'BLACK_RAZOR', 'WIZARDSPIKE'],
	'quarterstaff': ['SHORT_STAFF', 'LONG_STAFF', 'COMPOSITE_STAFF', 'WAR_STAFF'],
	'bow': ['SHORT_BOW', 'HUNTERS_BOW', 'COMPOSITE_BOW', 'SHORT_BATTLE_BOW', 'LONG_BATTLE_BOW', 'SHORT_WAR_BOW', 'LONG_WAR_BOW', 'WINDFORCE'],
	'crossbow': ['SHORT_BATTLE_BOW', 'LONG_BATTLE_BOW', 'SHORT_WAR_BOW', 'LONG_WAR_BOW'],
	'staff': ['LONG_STAFF', 'COMPOSITE_STAFF', 'WAR_STAFF', 'STAFF_OF_LAZARUS'],
	'sceptre': ['MACE', 'MORNING_STAR', 'WAR_HAMMER', 'SPIKED_CLUB'],
}
RING_CURSORS = {
	'Ruby Ring': 'RING_OF_MAGMA', 'Sapphire Ring': 'XORINES_RING', 'Topaz Ring': 'RING_OF_THUNDER',
	'Coral Ring': 'RING_OF_REGHA', 'Lapis Ring': 'RING_OF_THE_MYSTICS', 'Iron Ring': 'GIANTS_KNUCKLE',
	'Opal Ring': 'MERCURIAL_RING', 'Gold Ring': 'RING_OF_ENGAGEMENT', 'Amethyst Ring': 'CONSTRICTING_RING',
	'Amber Amulet': 'AMULET', 'Jade Amulet': 'OPTIC_AMULET', 'Azure Amulet': 'AMULET_OF_WARDING',
	'Prismatic Amulet': 'AURIC_AMULET', 'Vital Amulet': 'ACOLYTES_AMULET', 'Spirit Amulet': 'OPTIC_AMULET',
	'Keen Amulet': 'AMULET_OF_WARDING', 'Sage Amulet': 'ACOLYTES_AMULET',
	'Crimson Amulet': 'AMULET', 'Lunar Amulet': 'OPTIC_AMULET', 'Gold Amulet': 'AURIC_AMULET',
}

# Implicits the game can do: bundle id -> (effect, min, max, text). Values are scaled to Diablo the way
# the matching modifiers are (attributes 40%, Spirit 500%). The others wait for their mechanics.
IMPLICITS = {
	'ring_fire': ('FireResistance', 12, 16, '+{0}% to Fire Resistance'),
	'ring_cold': ('MagicResistance', 12, 16, '+{0}% to Magic Resistance'),
	'ring_lightning': ('LightningResistance', 12, 16, '+{0}% to Lightning Resistance'),
	'ring_life': ('Life', 20, 30, '+{0} to maximum Life'),
	'ring_mana': ('Mana', 18, 28, '+{0} to maximum Mana'),
	'ring_attack': ('AddedPhysicalToAttacks', 2, 4, 'Adds {0} to {1} Physical Damage to Attacks'),
	'ring_chaos': ('ChaosResistance', 7, 13, '+{0}% to Chaos Resistance'),
	'amber_amulet': ('Strength', 6, 9, '+{0} to Strength'),
	'jade_amulet': ('Dexterity', 6, 9, '+{0} to Dexterity'),
	'azure_amulet': ('Intelligence', 6, 9, '+{0} to Intelligence'),
	'prismatic_amulet': ('AllResistances', 8, 12, '+{0}% to all Elemental Resistances'),
	'vital_amulet': ('Life', 25, 35, '+{0} to maximum Life'),
	'spirit_amulet': ('Spirit', 60, 90, '+{0} to Spirit'),
	'sage_amulet': ('ManaRegeneration', 8, 12, '{0}% increased Mana Regeneration Rate'),
	'staff_casting': ('CastSpeed', 8, 8, '{0}% increased Cast Speed'),
}
# Jewellery whose bundle implicit has no mechanic here gets the nearest one that has (every ring and amulet has one).
IMPLICIT_STAND_INS = {
	'Pearl Ring': ('CastSpeed', 7, 10, '{0}% increased Cast Speed'),
	'Golden Hoop': ('AllAttributes', 3, 5, '+{0} to all Attributes'),
}

# Amulets take POE2's own implicits (Path of Building 2's amulet data), scaled like the matching modifiers: attributes
# 40%, energy shield 10% in defence points (shown as 30%). Spirit is already on POE2's scale (Heralds reserve 30).
POE2_AMULET_IMPLICITS = {
	'Crimson Amulet': ('LifeRegeneration', 2, 4, 'Regenerate {0} Life per second'),
	'Azure Amulet': ('ManaRegeneration', 20, 30, '{0}% increased Mana Regeneration Rate'),
	'Amber Amulet': ('Strength', 4, 6, '+{0} to Strength'),
	'Jade Amulet': ('Dexterity', 4, 6, '+{0} to Dexterity'),
	'Lapis Amulet': ('Intelligence', 4, 6, '+{0} to Intelligence'),
	'Lunar Amulet': ('FlatEnergyShield', 2, 3, '+{0} to maximum Energy Shield'),
	'Bloodstone Amulet': ('Life', 30, 40, '+{0} to maximum Life'),
	'Stellar Amulet': ('AllAttributes', 2, 3, '+{0} to all Attributes'),
	'Solar Amulet': ('Spirit', 10, 15, '+{0} to Spirit'),
	'Pearlescent Amulet': ('AllResistances', 7, 10, '+{0}% to all Elemental Resistances'),
	'Gold Amulet': ('ItemRarity', 12, 20, '{0}% increased Rarity of Items found'),
}
# The bundle's identity for the amulets that took another amulet's place, and the added ones.
POE2_AMULET_IDENTITIES = {'Stellar Amulet': 'attribute', 'Crimson Amulet': 'life', 'Lunar Amulet': 'defence', 'Gold Amulet': 'rarity'}


def one_implicit(implicits):
	"""POE2 Mashup rule (Curtis, 2026-10-16): a base has at most one implicit.

	Speed lines of one kind are summed first (a focused staff's slower casting and the staff's own faster casting
	are one line). Then faster attacks or casting win (swift and quick bases), else Intelligence (a caster weapon's
	power), else the base's own line, else a slower speed (heavy bases).
	"""
	merged = []
	for effect, lo, hi, text in implicits:
		if effect in ('CastSpeed', 'AttackSpeed'):
			same = next((m for m in merged if m[0] == effect), None)
			if same is not None:
				same[1] += lo
				same[2] += hi
				continue
		merged.append([effect, lo, hi, text])
	for m in merged:
		if m[0] in ('CastSpeed', 'AttackSpeed'):
			word = 'Cast Speed' if m[0] == 'CastSpeed' else 'Attack Speed'
			m[3] = f'{abs(m[1])}% {"increased" if m[1] > 0 else "reduced"} {word}'
	merged = [m for m in merged if not (m[0] in ('CastSpeed', 'AttackSpeed') and m[1] == 0)]

	def rank(m):
		if m[0] in ('CastSpeed', 'AttackSpeed'):
			return 0 if m[1] > 0 else 3
		return 1 if m[0] == 'Intelligence' else 2
	return [tuple(min(merged, key=rank))] if merged else []


# Spell power of staves and sceptres becomes Intelligence, which powers Diablo's spells.
SPELL_POWER_TO_INTELLIGENCE = 0.4

DEFENSE_LINES = {
	'armour': 'Armour', 'evasion': 'Evasion', 'energy_shield': 'EnergyShield', 'armour_evasion': 'ArmourEvasion',
	'armour_es': 'ArmourEnergyShield', 'evasion_es': 'EvasionEnergyShield', 'tri': 'All', 'caster_defence': 'EnergyShield',
}

# Drop levels are POE2 area levels.
LEVEL_SCALE = 1
# The scale the levels had before, which still picks the Diablo unique type a base carries: saved uniques find
# themselves by their base's unique type, so it must never change.
UNIQUE_TYPE_LEVEL_SCALE = 0.36
# Attribute requirements are POE2's own numbers (Curtis 2026-10-27, after the Warrior playtest): Legion Helm 78 Str,
# Titan Crown 150. They were 55% while levels were scaled down.
REQUIREMENT_SCALE = 1
# Like POE2, the first base of each line needs no attributes (Curtis 2026-10-30: "5 Mag" and "3 Mag, 3 Dex" on starter
# gear); the needs start with the next base. The game's MaxStarterBaseLevel.
STARTER_BASE_LEVEL = 1
# One defence point is one armour, one evasion or three energy shield (EnergyShieldPerDefense).
ENERGY_SHIELD_PER_DEFENSE = 3
# A weapon hits for its base's damage: POE2's own (REAL_WEAPON_BASES), the bundle's for crossbows.
DAMAGE_SCALE = 1
DAMAGE_BASE = 0
# Act 1 shortfall fix (harder monsters plan step 14, Curtis 2026-10-27): the first quarterstaves and bows, the only ones
# a hero uses up to about level 11 (drop level 1: POE2's Wrapped Quarterstaff and Crude Bow since 2026-10-27), hit
# harder than their base, in percent of its damage. The step-13 fight check found the Monk and the Rogue could not
# clear Act 1 packs with them. Raised from 160 and 140 when attacks took the bases' real POE2 speeds (research/
# attack-speed/rebalance): a Crude Bow shot went from 0.40 s to 0.83 s and a staff swing from 0.45 s to 0.71 s, so the
# Rogue and the Monk kill as much slower than before as the Warrior and the Bard do, whose weapons need nothing extra.
EARLY_WEAPON_DAMAGE_PERCENT = {'quarterstaff': 200, 'bow': 215}
EARLY_WEAPON_MAX_DROP_LEVEL = 1

# POE2's body armours (research/poe2-body-armours.md, the normal tier): name -> (level, armour, evasion, energy shield).
# The game's names for Chain Mail and Scale Mail differ, since Diablo has bases of those names.
BODY_ARMOURS = {
	'Rusted Cuirass': (1, 45, 0, 0), 'Iron Cuirass': (11, 115, 0, 0), 'Maraketh Cuirass': (20, 178, 0, 0),
	'Full Plate': (33, 270, 0, 0), 'Juggernaut Plate': (45, 355, 0, 0), 'Champion Cuirass': (58, 446, 0, 0),
	'Conqueror Plate': (68, 496, 0, 0),
	'Leather Vest': (1, 0, 30, 0), 'Pathfinder Coat': (11, 0, 96, 0), 'Rhoahide Coat': (22, 0, 168, 0),
	"Scout's Vest": (33, 0, 240, 0), 'Corsair Vest': (45, 0, 320, 0), 'Hardleather Coat': (56, 0, 392, 0),
	'Armoured Vest': (73, 0, 451, 0),
	'Tattered Robe': (1, 0, 0, 28), "Hexer's Robe": (11, 0, 0, 45), 'Silk Robe': (22, 0, 0, 64),
	'Votive Raiment': (33, 0, 0, 83), 'Elementalist Robe': (45, 0, 0, 103), 'Plated Raiment': (58, 0, 0, 126),
	'Enlightened Robe': (68, 0, 0, 138),
	'Chain Mail Vest': (1, 25, 16, 0), 'Rogue Armour': (11, 63, 53, 0), 'Cloaked Mail': (26, 121, 107, 0),
	'Scale Armour': (37, 164, 147, 0), 'Knight Armour': (45, 195, 176, 0), 'Gladiator Armour': (58, 245, 223, 0),
	'Heroic Armour': (65, 273, 248, 0),
	'Pilgrim Vestments': (1, 25, 0, 16), 'Pelt Mantle': (10, 59, 0, 24), 'Mail Vestments': (16, 83, 0, 30),
	'Shaman Mantle': (28, 129, 0, 41), 'Cleric Vestments': (45, 195, 0, 57), 'Revered Vestments': (65, 273, 0, 76),
	'Corvus Mantle': (68, 273, 0, 76),
	'Hermit Garb': (1, 0, 16, 16), 'Waxed Jacket': (11, 0, 53, 25), 'Marabout Garb': (16, 0, 71, 30),
	'Wayfarer Jacket': (28, 0, 114, 41), "Scalper's Jacket": (39, 0, 154, 51), 'Monastic Garb': (56, 0, 216, 67),
	'Devout Garb': (68, 0, 248, 76),
	'Grand Regalia': (65, 182, 165, 50),
}

# Their attribute requirements, from the same tables (Curtis 2026-10-27: gear needs POE2's full numbers): the
# armour part needs Strength, evasion Dexterity and energy shield Intelligence. The starters need none.
BODY_ARMOUR_ATTRIBUTES = {
	'Iron Cuirass': 21, 'Maraketh Cuirass': 34, 'Full Plate': 54, 'Juggernaut Plate': 72, 'Champion Cuirass': 92,
	'Conqueror Plate': 121,
	'Pathfinder Coat': 21, 'Rhoahide Coat': 37, "Scout's Vest": 54, 'Corsair Vest': 72, 'Hardleather Coat': 89,
	'Armoured Vest': 121,
	"Hexer's Robe": 21, 'Silk Robe': 37, 'Votive Raiment': 54, 'Elementalist Robe': 72, 'Plated Raiment': 92,
	'Enlightened Robe': 121,
	'Rogue Armour': 13, 'Cloaked Mail': 25, 'Scale Armour': 34, 'Knight Armour': 41, 'Gladiator Armour': 51,
	'Heroic Armour': 67,
	'Pelt Mantle': 12, 'Mail Vestments': 17, 'Shaman Mantle': 26, 'Cleric Vestments': 41, 'Revered Vestments': 67,
	'Corvus Mantle': 67,
	'Waxed Jacket': 13, 'Marabout Garb': 17, 'Wayfarer Jacket': 26, "Scalper's Jacket": 35, 'Monastic Garb': 50,
	'Devout Garb': 67,
	'Grand Regalia': 41,
}

HEADER = ['id', 'dropRate', 'class', 'equipType', 'cursorGraphic', 'itemType', 'uniqueBaseItem', 'name', 'shortName',
          'minMonsterLevel', 'durability', 'minDamage', 'maxDamage', 'minArmor', 'maxArmor', 'minStrength', 'minMagic',
          'minDexterity', 'specialEffects', 'miscId', 'spell', 'usable', 'value']


def load_real_weapons():
	"""POE2's weapon bases by name (tools/poe2_real_weapons.tsv)."""
	with open(REAL_WEAPONS, encoding='utf-8') as f:
		lines = [line.rstrip('\n').split('\t') for line in f]
	return {row[1]: dict(zip(lines[0], row)) for row in lines[1:]}


def base_name(b):
	"""The name a bundle base has in the game: POE2's name for a balanced weapon, the bundle's (or a rename) otherwise."""
	if b['id'] in REAL_WEAPON_BASES:
		real = REAL_WEAPON_BASES[b['id']]
		return REAL_WEAPON_RENAMES.get(real, real)
	return RENAMES.get(b['id'], b['display_name'])


def is_retired_weapon(b):
	return b['item_class'] in RETIRED_WEAPON_CLASSES or (b.get('design_identity') in RETIRED_WEAPON_IDENTITIES)


def d1_level(drop_level):
	return max(1, round(drop_level * LEVEL_SCALE))


def unique_type_level(drop_level):
	return max(1, round(drop_level * UNIQUE_TYPE_LEVEL_SCALE))


def unique_source(row):
	"""Which kind of Diablo base a row is, for borrowing its unique type."""
	item_type, equip, short = row[5], row[3], row[8]
	if item_type == 'Sword':
		if short == 'Dagger':
			return 'Dagger'
		return 'Sword2' if equip == 'Two-handed' else 'Sword1'
	if item_type == 'Mace':
		return 'Mace2' if equip == 'Two-handed' else 'Mace1'
	return item_type


def damage(stats, percent=100):
	lo = max(1, round((stats['physical_damage_min'] * DAMAGE_SCALE + DAMAGE_BASE) * percent / 100))
	return lo, max(lo + 1, round((stats['physical_damage_max'] * DAMAGE_SCALE + DAMAGE_BASE) * percent / 100))


def damage_percent(cls, drop_level):
	"""The share of its base's damage a weapon hits for: more for the early staves and bows (step 14)."""
	return EARLY_WEAPON_DAMAGE_PERCENT.get(cls, 100) if drop_level <= EARLY_WEAPON_MAX_DROP_LEVEL else 100


def caster_damage(spell_power, staff):
	if staff:
		lo = round(2 + spell_power * 0.06)
		return lo, max(lo + 2, round(4 + spell_power * 0.14))
	lo = round(1 + spell_power * 0.1)
	return lo, max(lo + 2, round(4 + spell_power * 0.2))


def defense_points(armour, evasion, energy_shield):
	"""Each kind of defence in defence points (energy shield in thirds), and their total, which every base rolls exactly."""
	points = (armour, evasion, energy_shield / ENERGY_SHIELD_PER_DEFENSE)
	return points, max(1, round(sum(points)))


def defense(stats):
	_, total = defense_points(stats.get('armour', 0), stats.get('evasion', 0), stats.get('energy_shield', 0))
	return total, total


def defense_split(line, armour, evasion, energy_shield):
	"""How a hybrid divides its defence: the percent of each kind of its line but the last, which takes the rest."""
	points, _ = defense_points(armour, evasion, energy_shield)
	total = sum(points)
	kinds = {'ArmourEvasion': (0,), 'ArmourEnergyShield': (0,), 'EvasionEnergyShield': (1,), 'All': (0, 1)}.get(line)
	if kinds is None or total <= 0:
		return ''
	return '/'.join(str(round(100 * points[kind] / total)) for kind in kinds)


BODY_ARMOUR_LINES = {
	(True, False, False): ('armour', 'Armour'), (False, True, False): ('evasion', 'Evasion'),
	(False, False, True): ('energy_shield', 'EnergyShield'), (True, True, False): ('armour_evasion', 'ArmourEvasion'),
	(True, False, True): ('armour_es', 'ArmourEnergyShield'), (False, True, True): ('evasion_es', 'EvasionEnergyShield'),
	(True, True, True): ('tri', 'All'),
}


def speed_implicit(base, balanced, key, word):
	percent = round((base / balanced - 1) * 100)
	if percent == 0:
		return None
	effect = 'AttackSpeed' if key == 'attacks_per_second' else 'CastSpeed'
	text = f'{{0}}% {"increased" if percent > 0 else "reduced"} {word}'
	# The text shows the size; the value keeps its sign.
	return (effect, percent, percent, text.replace('{0}', str(abs(percent))))


def main():
	if len(sys.argv) != 2:
		sys.exit(__doc__)
	with open(sys.argv[1], encoding='utf-8') as f:
		bases = [b for b in json.load(f)['base_items'] if b['item_class'] in CLASSES]

	with open(ITEMDAT, encoding='utf-8', newline='') as f:
		lines = f.read().split('\r\n')
	if lines[-1] == '':
		lines.pop()
	rows = [line.split('\t') for line in lines]
	assert rows[0] == HEADER, 'itemdat.tsv columns changed'
	by_id = {row[0]: i for i, row in enumerate(rows) if row[0].startswith(ID_PREFIX)}

	# Diablo's bases of these kinds stop dropping; their unique types move to the new bases.
	unique_types = {}
	diablo_types = {'Helm', 'Shield', 'Sword', 'Axe', 'Mace', 'Bow', 'Staff', 'Ring', 'Amulet'}
	# Each Diablo weapon unique type and the art of its base.
	diablo_weapon_art = {}
	for row in rows[1:]:
		# Unique and starting-gear rows have no short name; bases do.
		if row[0].startswith(ID_PREFIX) or row[5] not in diablo_types or row[8] == '' or row[19] == 'UNIQUE':
			continue
		if row[2] == 'Weapon' and row[6] != 'NONE':
			diablo_weapon_art.setdefault(row[6], row[4])
		if row[6] != 'NONE' and row[8] not in ('Focus', 'Quarterstaff', 'Crossbow', 'Sceptre'):
			unique_types.setdefault(unique_source(row), []).append((int(row[9]), row[6]))
		row[1] = '0'
	for kind in unique_types.values():
		kind.sort()

	# Speeds are relative to the balanced (or the faster caster) base of the same line and tier.
	balanced = {}
	for b in bases:
		if b.get('design_identity') in ('balanced', 'rapid'):
			balanced[(b['item_class'], b['progression_tier'])] = b

	real_weapons = load_real_weapons()
	assert len(set(REAL_WEAPON_BASES.values())) == len(REAL_WEAPON_BASES), 'a POE2 weapon base is used twice'

	meta_rows = []
	taken = {row[7] for row in rows[1:] if not row[0].startswith(ID_PREFIX)}
	generated = set()
	for b in bases:
		cls = b['item_class']
		item_class, equip, item_type, short, quality, unique_kind = CLASSES[cls]
		name = base_name(b)
		assert name not in taken and name not in generated, f'{name} clashes with another base; add it to RENAMES'
		generated.add(name)
		stats = b['base_stats']
		tier = b['progression_tier']
		is_jewellery = cls in ('ring', 'amulet')
		level = POE2_JEWELLERY_LEVELS.get(b['id'], b['drop_level']) if is_jewellery else d1_level(b['drop_level'])
		req = b['requirements']
		# A weapon's POE2 base: its own, or its balanced sibling's for a heavy or swift one.
		sibling = balanced.get((cls, tier))
		real = real_weapons[REAL_WEAPON_BASES[sibling['id']]] if sibling is not None and sibling['id'] in REAL_WEAPON_BASES else None
		if real is not None:
			level = max(1, int(real['level']))
			req = {'strength': int(real['str']), 'dexterity': int(real['dex']), 'intelligence': int(real['int'])}

		group = b.get('upgrade_group') or cls
		if cls in ('ring', 'amulet'):
			cursor = RING_CURSORS[b['display_name']]
		else:
			art = CURSORS[group if group in CURSORS else cls]
			cursor = art[(tier - 1) * len(art) // 8]

		unique = 'NONE'
		if unique_kind and unique_kind in unique_types:
			unique_level = level if is_jewellery else unique_type_level(b['drop_level'])
			unique = min(unique_types[unique_kind], key=lambda t: (abs(t[0] - unique_level), t[0]))[1]

		min_dam = max_dam = min_ac = max_ac = 0
		implicits = []
		if 'physical_damage_min' in stats:
			weapon = stats if real is None else {'physical_damage_min': int(real['min']), 'physical_damage_max': int(real['max'])}
			min_dam, max_dam = damage(weapon, damage_percent(cls, b['drop_level']))
		if 'innate_spell_power' in stats:
			min_dam, max_dam = caster_damage(stats['innate_spell_power'], cls == 'staff')
			other = balanced.get((cls, tier))
			if other is not None and other is not b:
				implicits.append(speed_implicit(stats['cast_speed_scalar'], other['base_stats']['cast_speed_scalar'], 'cast_speed_scalar', 'Cast Speed'))
			intelligence = round(stats['innate_spell_power'] * SPELL_POWER_TO_INTELLIGENCE)
			implicits.append(('Intelligence', intelligence, intelligence, '+{0} to Intelligence'))
		if any(k in stats for k in ('armour', 'evasion', 'energy_shield')):
			min_ac, max_ac = defense(stats)
		for implicit in b['implicit_modifiers']:
			if implicit in IMPLICITS:
				implicits.append(IMPLICITS[implicit])
		implicits = [i for i in implicits if i is not None]
		implicits = one_implicit(implicits)

		if is_jewellery and not implicits and name in IMPLICIT_STAND_INS:
			implicits = [IMPLICIT_STAND_INS[name]]
		if name in POE2_AMULET_IMPLICITS:
			implicits = [POE2_AMULET_IMPLICITS[name]]
		value = (1000 + level * 50) if cls == 'amulet' else (1000 + b['drop_level'] * 50) if is_jewellery else (100 + (b['drop_level'] if real is None else level) * 100)
		retired = b['id'] in RETIRED_BASES or is_retired_weapon(b) or cls in LEGACY_CASTER_CLASSES
		row = [
			ID_PREFIX + b['id'], '0' if retired else '1', item_class, equip, cursor, item_type, unique, name, short, str(level),
			'0' if is_jewellery else str(30 + tier * 5), str(min_dam), str(max_dam), str(min_ac), str(max_ac),
			str(round(req['strength'] * REQUIREMENT_SCALE)), str(round(req['intelligence'] * REQUIREMENT_SCALE)),
			str(round(req['dexterity'] * REQUIREMENT_SCALE)), '', cls.upper() if is_jewellery else 'NONE', 'Null', 'false', str(value),
		]
		if row[0] in by_id:
			rows[by_id[row[0]]] = row
		else:
			by_id[row[0]] = len(rows)
			rows.append(row)

		defense_line = DEFENSE_LINES.get(b.get('design_identity', ''), '') if min_ac else ''
		split = defense_split(defense_line, stats.get('armour', 0), stats.get('evasion', 0), stats.get('energy_shield', 0)) if min_ac else ''
		# The character level the base needs: the bundle's, or for jewellery, which follows POE2's lineup, its level.
		required_level = level if is_jewellery or real is not None else b['required_level']
		aps = int(real['aps']) if real is not None else round(stats['attacks_per_second'] * 100) if 'attacks_per_second' in stats else ''
		meta_rows.append([
			name, LEGACY_CASTER_CLASSES.get(cls, cls), POE2_AMULET_IDENTITIES.get(name, b.get('design_identity', '')), defense_line, quality if cls != 'focus' else 'defence',
			';'.join(f'{e}|{lo}|{hi}|{text}' for e, lo, hi, text in implicits), split, str(required_level), str(stats.get('block_chance_pct', '')),
			str(aps),
		])

	for base_id, name, level in POE2_EXTRA_AMULETS:
		assert name not in taken and name not in generated, f'{name} clashes with another base'
		generated.add(name)
		unique = min(unique_types['Amulet'], key=lambda t: (abs(t[0] - level), t[0]))[1] if 'Amulet' in unique_types else 'NONE'
		row = [
			ID_PREFIX + base_id, '1', 'Misc', 'Amulet', RING_CURSORS[name], 'Amulet', unique, name, 'Amulet', str(level),
			'0', '0', '0', '0', '0', '0', '0', '0', '', 'AMULET', 'Null', 'false', str(1000 + level * 50),
		]
		if row[0] in by_id:
			rows[by_id[row[0]]] = row
		else:
			by_id[row[0]] = len(rows)
			rows.append(row)
		effect, lo, hi, text = POE2_AMULET_IMPLICITS[name]
		meta_rows.append([name, 'amulet', POE2_AMULET_IDENTITIES[name], '', 'none', f'{effect}|{lo}|{hi}|{text}', '', str(level), '', ''])

	for base_id, name, level, implicit in POE2_EXTRA_RINGS:
		assert name not in taken and name not in generated, f'{name} clashes with another base'
		generated.add(name)
		unique = min(unique_types['Ring'], key=lambda t: (abs(t[0] - level), t[0]))[1] if 'Ring' in unique_types else 'NONE'
		row = [
			ID_PREFIX + base_id, '1', 'Misc', 'Ring', RING_CURSORS[name], 'Ring', unique, name, 'Ring', str(level),
			'0', '0', '0', '0', '0', '0', '0', '0', '', 'RING', 'Null', 'false', str(1000 + level * 50),
		]
		if row[0] in by_id:
			rows[by_id[row[0]]] = row
		else:
			by_id[row[0]] = len(rows)
			rows.append(row)
		effect, lo, hi, text = IMPLICITS[implicit]
		meta_rows.append([name, 'ring', 'resistance', '', 'none', f'{effect}|{lo}|{hi}|{text}', '', str(level), '', ''])

	tier_levels = sorted({b['drop_level'] for b in bases if b['item_class'] == 'one_hand_sword'})
	for base_id, cls, real_name, unique in DIABLO_UNIQUE_TYPE_BASES:
		assert real_name not in REAL_WEAPON_BASES.values(), f'{real_name} is already a base'
		real = real_weapons[real_name]
		name = REAL_WEAPON_RENAMES.get(real_name, real_name)
		assert name not in taken and name not in generated, f'{name} clashes with another base; add it to REAL_WEAPON_RENAMES'
		generated.add(name)
		item_class, equip, item_type, short, quality, _ = CLASSES[cls]
		level = max(1, int(real['level']))
		tier = sum(1 for t in tier_levels if t <= level)
		min_dam, max_dam = damage({'physical_damage_min': int(real['min']), 'physical_damage_max': int(real['max'])}, damage_percent(cls, level))
		row = [
			ID_PREFIX + base_id, '1', item_class, equip, diablo_weapon_art[unique], item_type, unique, name, short, str(level),
			str(30 + tier * 5), str(min_dam), str(max_dam), '0', '0', str(round(int(real['str']) * REQUIREMENT_SCALE)),
			str(round(int(real['int']) * REQUIREMENT_SCALE)), str(round(int(real['dex']) * REQUIREMENT_SCALE)), '', 'NONE', 'Null',
			'false', str(100 + level * 100),
		]
		if row[0] in by_id:
			rows[by_id[row[0]]] = row
		else:
			by_id[row[0]] = len(rows)
			rows.append(row)
		meta_rows.append([name, cls, 'balanced', '', quality, '', '', str(level), '', real['aps']])

	caster_meta = []
	for (cls, base_id, cursor, unique, name, level, durability, strength, intelligence, dexterity, lo, hi, grants, element,
	     aps, crit, implicit) in CASTER_BASES:
		assert name not in taken and name not in generated, f'{name} clashes with another base'
		generated.add(name)
		item_class, equip, item_type, short = CASTER_CLASSES[cls]
		if hi > 0:
			lo, hi = damage({'physical_damage_min': lo, 'physical_damage_max': hi}, damage_percent(cls, level))
		row = [
			ID_PREFIX + base_id, '1', item_class, equip, cursor, item_type, unique, name, short, str(level), str(durability),
			str(lo), str(hi), '0', '0', str(strength), str(intelligence), str(dexterity), '', 'NONE', 'Null', 'false',
			str(100 + level * 100),
		]
		if row[0] in by_id:
			rows[by_id[row[0]]] = row
		else:
			by_id[row[0]] = len(rows)
			rows.append(row)
		caster_meta.append([
			name, cls, 'poe2', '', 'weapon' if cls == 'quarterstaff' else '', implicit, '', str(level), '', str(aps),
			'' if grants in PENDING_GRANTS else grants, element, str(crit),
		])

	# Every Diablo weapon unique type is on a base that drops.
	carried = {row[6] for row in rows[1:] if row[0].startswith(ID_PREFIX) and row[1] != '0' and row[2] == 'Weapon'}
	missing = set(diablo_weapon_art) - carried - OWN_BASE_UNIQUE_TYPES
	assert not missing, f'no dropping base carries the Diablo unique types {sorted(missing)}; add them to DIABLO_UNIQUE_TYPE_BASES'

	# The body armours keep their rows, art, durability and price; they take POE2's level, defence and requirements.
	# Their implicits stay in the game (GetBodyArmourImplicit).
	found = set()
	for row in rows[1:]:
		if row[0] != '' or row[2] != 'Armor' or row[5] not in ('LightArmor', 'MediumArmor', 'HeavyArmor') or row[7] not in BODY_ARMOURS:
			continue
		level, armour, evasion, energy_shield = BODY_ARMOURS[row[7]]
		_, total = defense_points(armour, evasion, energy_shield)
		row[9] = str(level)
		row[13] = row[14] = str(total)
		need = BODY_ARMOUR_ATTRIBUTES.get(row[7], 0)
		row[15], row[16], row[17] = (str(need if part > 0 else 0) for part in (armour, energy_shield, evasion))
		identity, line = BODY_ARMOUR_LINES[(armour > 0, evasion > 0, energy_shield > 0)]
		meta_rows.append([row[7], 'body_armour', identity, line, 'defence', '', defense_split(line, armour, evasion, energy_shield), str(level), '', ''])
		found.add(row[7])
	assert found == set(BODY_ARMOURS), f'body armours not in itemdat.tsv: {sorted(set(BODY_ARMOURS) - found)}'

	starters = {meta[0] for meta in meta_rows + caster_meta if int(meta[7] or 0) <= STARTER_BASE_LEVEL}
	for row in rows[1:]:
		if row[7] in starters:
			row[15] = row[16] = row[17] = '0'

	with open(ITEMDAT, 'w', encoding='utf-8', newline='') as f:
		for row in rows:
			f.write('\t'.join(row) + '\r\n')
	with open(META, 'w', encoding='utf-8', newline='') as f:
		# Rows from before the caster weapons rework leave out its columns.
		header = ['name', 'class', 'identity', 'defense', 'quality', 'implicits', 'split', 'level', 'block', 'aps', 'grants', 'element', 'crit']
		for row in [header] + meta_rows + caster_meta:
			f.write('\t'.join(row) + '\r\n')
	print(f'{len(meta_rows) + len(caster_meta)} bases')


if __name__ == '__main__':
	main()
