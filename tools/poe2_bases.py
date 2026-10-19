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
}

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
	'Opal Ring': ('AllResistances', 6, 9, '+{0}% to all Elemental Resistances'),
	'Gold Ring': ('AllAttributes', 3, 5, '+{0} to all Attributes'),
	'Keen Amulet': ('Dexterity', 6, 9, '+{0} to Dexterity'),
}


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

# Diablo's levels run about a third of POE2's, the way the body armour bases were placed.
LEVEL_SCALE = 0.36
REQUIREMENT_SCALE = 0.55
DEFENSE_SCALE = 0.13
# Energy shield counts about 2.9 times its number, the bundle's armour to energy shield ratio.
ENERGY_SHIELD_WEIGHT = 2.9
# A weapon's average damage: a quarter of the bundle's plus 2.5, spread wider like Diablo's weapons.
DAMAGE_SCALE = 0.25
DAMAGE_BASE = 2.5

HEADER = ['id', 'dropRate', 'class', 'equipType', 'cursorGraphic', 'itemType', 'uniqueBaseItem', 'name', 'shortName',
          'minMonsterLevel', 'durability', 'minDamage', 'maxDamage', 'minArmor', 'maxArmor', 'minStrength', 'minMagic',
          'minDexterity', 'specialEffects', 'miscId', 'spell', 'usable', 'value']


def d1_level(drop_level):
	return max(1, round(drop_level * LEVEL_SCALE))


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
	avg = (stats['physical_damage_min'] + stats['physical_damage_max']) / 2 * DAMAGE_SCALE + DAMAGE_BASE
	lo = max(1, round(avg * 0.6))
	return lo, max(lo + 1, round(avg * 1.4))


def caster_damage(spell_power, staff):
	if staff:
		lo = round(2 + spell_power * 0.06)
		return lo, max(lo + 2, round(4 + spell_power * 0.14))
	lo = round(1 + spell_power * 0.1)
	return lo, max(lo + 2, round(4 + spell_power * 0.2))


def defense(stats):
	total = stats.get('armour', 0) + stats.get('evasion', 0) + stats.get('energy_shield', 0) * ENERGY_SHIELD_WEIGHT
	ac = total * DEFENSE_SCALE
	lo = max(1, round(ac * 0.9))
	return lo, max(lo, round(ac * 1.1))


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
		level = d1_level(b['drop_level'])
		req = b['requirements']

		group = b.get('upgrade_group') or cls
		if cls in ('ring', 'amulet'):
			cursor = RING_CURSORS[b['display_name']]
		else:
			art = CURSORS[group if group in CURSORS else cls]
			cursor = art[(tier - 1) * len(art) // 8]

		unique = 'NONE'
		if unique_kind and unique_kind in unique_types:
			unique = min(unique_types[unique_kind], key=lambda t: (abs(t[0] - level), t[0]))[1]

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

		is_jewellery = cls in ('ring', 'amulet')
		if is_jewellery and not implicits and name in IMPLICIT_STAND_INS:
			implicits = [IMPLICIT_STAND_INS[name]]
		value = (1000 + b['drop_level'] * 50) if is_jewellery else (100 + b['drop_level'] * 100)
		row = [
			ID_PREFIX + b['id'], '1', item_class, equip, cursor, item_type, unique, name, short, str(level),
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
		meta_rows.append([
			name, cls, b.get('design_identity', ''), defense_line, quality if cls != 'focus' else 'defence',
			';'.join(f'{e}|{lo}|{hi}|{text}' for e, lo, hi, text in implicits),
		])

	with open(ITEMDAT, 'w', encoding='utf-8', newline='') as f:
		for row in rows:
			f.write('\t'.join(row) + '\r\n')
	with open(META, 'w', encoding='utf-8', newline='') as f:
		for row in [['name', 'class', 'identity', 'defense', 'quality', 'implicits']] + meta_rows:
			f.write('\t'.join(row) + '\r\n')
	print(f'{len(meta_rows)} bases')


if __name__ == '__main__':
	main()
