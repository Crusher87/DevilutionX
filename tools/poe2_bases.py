#!/usr/bin/env python3
"""Turns the itemization bundle's base_items.json into POE2 Mashup item bases.

Usage: tools/poe2_bases.py path/to/base_items.json

Adds a row to mods/poe2/txtdata/items/itemdat.tsv for each bundle base of the classes the game
has (helmets, shields, foci, rings, amulets and the weapon lines Diablo can wield), and writes
mods/poe2/txtdata/poe2/base_meta.tsv with what the game reads besides itemdat: the defence line,
how quality works, and the implicits (including the attack or cast speed of heavy, swift, focused
and quick bases).

Rows are matched by the id in the first column, so running it again updates them in place and
never moves a row: saved items refer to bases by their row. The Diablo bases of those classes stop
dropping (old items keep working), and each new base takes the unique type of the nearest Diablo
base of its kind, so Diablo's uniques keep dropping until the bundle's own uniques replace them.

Levels, damage and defence are POE2's own numbers (harder monsters plan, step 10b): drop levels are
area levels, weapons hit for the bundle's damage, and a base's defence is its armour plus evasion
plus a third of its energy shield (the game shows defence points as three energy shield each). The
body armours, which have no id, are matched by name and take POE2's numbers from
research/poe2-body-armours.md. base_meta.tsv's split column says how a hybrid's defence divides.
"""

import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ITEMDAT = os.path.join(ROOT, 'mods', 'poe2', 'txtdata', 'items', 'itemdat.tsv')
META = os.path.join(ROOT, 'mods', 'poe2', 'txtdata', 'poe2', 'base_meta.tsv')
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
	'Opal Ring': 'MERCURIAL_RING', 'Gold Ring': 'RING_OF_ENGAGEMENT',
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
REQUIREMENT_SCALE = 0.55
# One defence point is one armour, one evasion or three energy shield (EnergyShieldPerDefense).
ENERGY_SHIELD_PER_DEFENSE = 3
# A weapon hits for the bundle's damage.
DAMAGE_SCALE = 1
DAMAGE_BASE = 0

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

HEADER = ['id', 'dropRate', 'class', 'equipType', 'cursorGraphic', 'itemType', 'uniqueBaseItem', 'name', 'shortName',
          'minMonsterLevel', 'durability', 'minDamage', 'maxDamage', 'minArmor', 'maxArmor', 'minStrength', 'minMagic',
          'minDexterity', 'specialEffects', 'miscId', 'spell', 'usable', 'value']


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


def damage(stats):
	lo = max(1, round(stats['physical_damage_min'] * DAMAGE_SCALE + DAMAGE_BASE))
	return lo, max(lo + 1, round(stats['physical_damage_max'] * DAMAGE_SCALE + DAMAGE_BASE))


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
	for row in rows[1:]:
		# Unique and starting-gear rows have no short name; bases do.
		if row[0].startswith(ID_PREFIX) or row[5] not in diablo_types or row[8] == '' or row[19] == 'UNIQUE':
			continue
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

	meta_rows = []
	taken = {row[7] for row in rows[1:] if not row[0].startswith(ID_PREFIX)}
	generated = set()
	for b in bases:
		cls = b['item_class']
		item_class, equip, item_type, short, quality, unique_kind = CLASSES[cls]
		name = RENAMES.get(b['id'], b['display_name'])
		assert name not in taken and name not in generated, f'{name} clashes with another base; add it to RENAMES'
		generated.add(name)
		stats = b['base_stats']
		tier = b['progression_tier']
		is_jewellery = cls in ('ring', 'amulet')
		level = POE2_JEWELLERY_LEVELS.get(b['id'], b['drop_level']) if is_jewellery else d1_level(b['drop_level'])
		req = b['requirements']

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
			min_dam, max_dam = damage(stats)
			other = balanced.get((cls, tier))
			if other is not None and other is not b:
				implicits.append(speed_implicit(stats['attacks_per_second'], other['base_stats']['attacks_per_second'], 'attacks_per_second', 'Attack Speed'))
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
		value = (1000 + level * 50) if cls == 'amulet' else (1000 + b['drop_level'] * 50) if is_jewellery else (100 + b['drop_level'] * 100)
		row = [
			ID_PREFIX + b['id'], '0' if b['id'] in RETIRED_BASES else '1', item_class, equip, cursor, item_type, unique, name, short, str(level),
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
		meta_rows.append([
			name, cls, POE2_AMULET_IDENTITIES.get(name, b.get('design_identity', '')), defense_line, quality if cls != 'focus' else 'defence',
			';'.join(f'{e}|{lo}|{hi}|{text}' for e, lo, hi, text in implicits), split,
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
		meta_rows.append([name, 'amulet', POE2_AMULET_IDENTITIES[name], '', 'none', f'{effect}|{lo}|{hi}|{text}', ''])

	# The body armours keep their rows, art, durability, requirements and price; they take POE2's level and defence.
	# Their implicits stay in the game (GetBodyArmourImplicit).
	found = set()
	for row in rows[1:]:
		if row[0] != '' or row[2] != 'Armor' or row[5] not in ('LightArmor', 'MediumArmor', 'HeavyArmor') or row[7] not in BODY_ARMOURS:
			continue
		level, armour, evasion, energy_shield = BODY_ARMOURS[row[7]]
		_, total = defense_points(armour, evasion, energy_shield)
		row[9] = str(level)
		row[13] = row[14] = str(total)
		identity, line = BODY_ARMOUR_LINES[(armour > 0, evasion > 0, energy_shield > 0)]
		meta_rows.append([row[7], 'body_armour', identity, line, 'defence', '', defense_split(line, armour, evasion, energy_shield)])
		found.add(row[7])
	assert found == set(BODY_ARMOURS), f'body armours not in itemdat.tsv: {sorted(set(BODY_ARMOURS) - found)}'

	with open(ITEMDAT, 'w', encoding='utf-8', newline='') as f:
		for row in rows:
			f.write('\t'.join(row) + '\r\n')
	with open(META, 'w', encoding='utf-8', newline='') as f:
		for row in [['name', 'class', 'identity', 'defense', 'quality', 'implicits', 'split']] + meta_rows:
			f.write('\t'.join(row) + '\r\n')
	print(f'{len(meta_rows)} bases')


if __name__ == '__main__':
	main()
