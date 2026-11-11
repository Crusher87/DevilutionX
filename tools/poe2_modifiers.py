#!/usr/bin/env python3
"""Turns the itemization bundle's modifier_families.json into the POE2 Mashup modifier tables.

Usage: tools/poe2_modifiers.py path/to/modifier_families.json

Writes mods/poe2/txtdata/poe2/modifier_families.tsv (one row per family) and
modifier_tiers.tsv (one row per tier, T1 the strongest). The bundle's numbers are
GAME-DEFAULT tuning, kept as they are; the scale columns say how the game shrinks
a value to Diablo's scale (100 = unchanged). The legacy scale columns keep the scales
items had before the bases moved to POE2's numbers (harder monsters plan, step 10b):
the game rolls an item saved before then with them, so it keeps its numbers.

The caster weapons rework's families come after all of them, from tools/poe2_caster_affixes.tsv (POE2's own rows),
moved onto the game's item levels and scales (caster_affixes below).
"""

import json
import os
from fractions import Fraction
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(ROOT, 'mods', 'poe2', 'txtdata', 'poe2')

# family id -> (effect, scale of the first stat, scale of the second stat, name word, enabled, natural)
# enabled loads the family; natural lets it roll on drops, vendor items and currency that adds a random modifier. A family
# that isn't natural comes only from what names it (an essence, a unique, an implicit, a rune). Both are set here on
# purpose, never from the effect: items rebuild their modifiers from the seed, so a family that starts to roll naturally
# re-rolls saved items of its slots. Families missing here have no effect in the game yet and load disabled.
FAMILIES = {
	'maximum_life': ('Life', 100, 100, 'Virile', True, True),
	'maximum_mana': ('Mana', 100, 100, 'Azure', True, True),
	# Bases have POE2's defence, so flat armour and evasion keep the bundle's numbers; energy shield is in defence
	# points, three energy shield each.
	'flat_armour': ('FlatArmour', 100, 100, 'Lacquered', True, True),
	'flat_evasion': ('FlatEvasion', 100, 100, "Shade's", True, True),
	'flat_energy_shield': ('FlatEnergyShield', 33, 100, 'Glimmering', True, True),
	'increased_armour': ('DefensePercent', 100, 100, 'Reinforced', True, True),
	'increased_evasion': ('DefensePercent', 100, 100, 'Agile', True, True),
	'increased_energy_shield': ('DefensePercent', 100, 100, 'Protective', True, True),
	'hybrid_armour_evasion': ('DefensePercent', 100, 100, "Scrapper's", True, True),
	'hybrid_armour_es': ('DefensePercent', 100, 100, 'Infixed', True, True),
	'hybrid_evasion_es': ('DefensePercent', 100, 100, 'Shadowy', True, True),
	'life_armour_hybrid': ('LifeAndDefensePercent', 100, 100, "Rhino's", True, True),
	'life_evasion_hybrid': ('LifeAndDefensePercent', 100, 100, "Fawn's", True, True),
	'life_es_hybrid': ('LifeAndDefensePercent', 100, 100, "Monk's", True, True),
	'mana_es_hybrid': ('ManaAndDefensePercent', 100, 100, "Seer's", True, True),
	'spirit_flat': ('Spirit', 500, 100, 'Spirited', True, True),
	# Weapons hit for the bundle's damage, so added damage keeps its numbers too.
	'local_added_physical': ('AddedPhysical', 100, 100, 'Glinting', True, True),
	'local_increased_physical': ('PhysicalPercent', 100, 100, 'Heavy', True, True),
	'physical_accuracy_hybrid': ('PhysicalPercentAndAccuracy', 100, 10, "Squire's", True, True),
	'local_added_fire': ('AddedFire', 100, 100, 'Heated', True, True),
	'local_added_lightning': ('AddedLightning', 100, 100, 'Humming', True, True),
	'minion_damage': ('MinionDamage', 100, 100, "Baron's", True, True),
	'global_added_physical_attacks': ('AddedPhysicalToAttacks', 100, 100, 'Burnished', True, True),
	'strength': ('Strength', 40, 100, 'of the Brute', True, True),
	'dexterity': ('Dexterity', 40, 100, 'of the Mongoose', True, True),
	'intelligence': ('Intelligence', 40, 100, 'of the Prodigy', True, True),
	'all_attributes': ('AllAttributes', 40, 100, 'of the Clouds', True, True),
	'fire_resistance': ('FireResistance', 100, 100, 'of the Whelpling', True, True),
	# Diablo has no cold damage, so cold resistance guards against magic, its third element.
	'cold_resistance': ('ColdResistance', 100, 100, 'of the Penguin', True, True),
	'lightning_resistance': ('LightningResistance', 100, 100, 'of the Squall', True, True),
	'all_elemental_resistance': ('AllResistances', 100, 100, 'of the Crystal', True, True),
	# Chaos resistance (chaos damage plan step 4) rolls on drops since step 14; items saved before that re-roll.
	'chaos_resistance': ('ChaosResistance', 100, 100, 'of the Lost', True, True),
	'attack_speed': ('AttackSpeed', 100, 100, 'of Skill', True, True),
	'cast_speed': ('CastSpeed', 100, 100, 'of Talent', True, True),
	# Accuracy is Diablo's percent chance to hit, so it stays a tenth.
	'accuracy_rating': ('Accuracy', 10, 100, 'of Calm', True, True),
	'reduced_attribute_requirements': ('ReducedRequirements', 100, 100, 'of the Worthy', True, True),
	'life_regeneration': ('LifeRegeneration', 100, 100, 'of the Newt', True, True),
	'mana_regeneration': ('ManaRegeneration', 100, 100, 'of Excitement', True, True),
	'melee_skill_levels': ('MeleeSkillLevels', 100, 100, 'of Combat', True, True),
	'projectile_skill_levels': ('ProjectileSkillLevels', 100, 100, 'of the Archer', True, True),
	'spell_skill_levels': ('SpellSkillLevels', 100, 100, 'of the Magus', True, True),
	# Staves roll increased spell damage like POE2's (harder monsters plan step 14): it adds to the tree's spell damage.
	'spell_damage': ('SpellDamage', 100, 100, "Apprentice's", True, True),
	'fire_skill_levels': ('FireSkillLevels', 100, 100, 'of Flames', True, True),
	'lightning_skill_levels': ('LightningSkillLevels', 100, 100, 'of Thunder', True, True),
	'jewel_maximum_life_pct': ('LifePercent', 100, 100, 'Healthy', True, True),
	'jewel_maximum_mana_pct': ('ManaPercent', 100, 100, 'Wise', True, True),
	'jewel_damage_pct': ('DamagePercent', 100, 100, 'Brutal', True, True),
	'jewel_attack_speed': ('AttackSpeed', 100, 100, 'of Alacrity', True, True),
	'jewel_cast_speed': ('CastSpeed', 100, 100, 'of Enchanting', True, True),
	'item_rarity': ('ItemRarity', 100, 100, 'of Plunder', True, True),
}

# Tiers the game sets apart from the bundle's: family id -> [(tier, item level, weight, min1, max1, min2, max2)].
TIER_OVERRIDES = {
	# The Magic Find suffix: three tiers that start low (loot plan).
	'item_rarity': [(1, 40, 420, 15, 18, 0, 0), (2, 24, 560, 11, 14, 0, 0), (3, 3, 640, 6, 10, 0, 0)],
}

# Families the bundle doesn't have, after the jewel families: the family row and its tiers.
EXTRA_FAMILIES = [
	(['item_rarity_prefix', 'Item Rarity', "Magpie's", 'prefix', 'item_rarity_prefix', 'rarity', 'helmet,ring,amulet', '', '',
	  '{0}% increased Rarity of Items found', 'ItemRarity', '100', '100', 'true', '100', '100', 'true'],
	 [(1, 47, 360, 16, 19, 0, 0), (2, 29, 520, 12, 15, 0, 0), (3, 10, 620, 8, 11, 0, 0)]),
]

# The caster weapons rework: the slots staff and sceptre now mean its new bases, which roll families of their own.
# Every family from before keeps the staves and sceptres saved before it (the legacy slots), and none rolls on a wand.
LEGACY_SLOTS = {'staff': 'legacy_staff', 'sceptre': 'legacy_sceptre', 'wand': None}


def legacy_slots(slots):
	out = []
	for slot in slots:
		mapped = LEGACY_SLOTS.get(slot, slot)
		if mapped is not None:
			out.append(mapped)
	return out

# The scales before step 10b, for items saved before it: family id -> (scale of the first stat, of the second).
# Families missing here had the scales they have now.
LEGACY_SCALES = {
	'flat_armour': (15, 100),
	'flat_evasion': (15, 100),
	'flat_energy_shield': (10, 100),
	'local_added_physical': (25, 25),
	'local_added_fire': (25, 25),
	'local_added_lightning': (25, 25),
	'global_added_physical_attacks': (25, 25),
}

# Colour-specific jewel modifiers from POE2's jewel pools (poe2db, research/poe2-jewels.md): one tier each,
# for every item level, on the jewel colours that roll them.
JEWEL_FAMILIES = [
	# id, name, word, side, slots, template, effect, min, max
	('ruby_increased_armour', 'Armoured', 'Armoured', 'prefix', 'ruby', '{0}% increased Armour', 'ArmourPercent', 10, 20),
	('ruby_physical_damage', 'Sharpened', 'Sharpened', 'prefix', 'ruby', '{0}% increased Global Physical Damage', 'DamagePercent', 5, 15),
	('emerald_increased_evasion', 'Evasive', 'Evasive', 'prefix', 'emerald', '{0}% increased Evasion Rating', 'EvasionPercent', 10, 20),
	('emerald_accuracy', 'Accurate', 'Accurate', 'prefix', 'emerald', '+{0} to Accuracy Rating', 'Accuracy', 5, 10),
	('sapphire_energy_shield', 'Shimmering', 'Shimmering', 'prefix', 'sapphire', '{0}% increased maximum Energy Shield', 'EnergyShieldPercent', 10, 20),
	('sapphire_minion_damage', 'Authoritative', 'Authoritative', 'prefix', 'sapphire', 'Minions deal {0}% increased Damage', 'MinionDamage', 5, 15),
	('sapphire_mana_regeneration', 'of Energy', 'of Energy', 'suffix', 'sapphire', '{0}% increased Mana Regeneration Rate', 'ManaRegeneration', 5, 15),
]
JEWEL_WEIGHT = 800

# The bundle's skill level tiers repeat (+1 four times, +2 twice, then +3 and +4), so they become +1, +2 and +3.
SKILL_LEVEL_VALUES = [3, 3, 2, 2, 1, 1, 1, 1]

# Families that roll on fewer slots than the bundle gives them. Spell damage only on staves for now (step 14): enabling
# a family re-rolls the modifiers of saved items of its slots (they rebuild from the seed), so wands, sceptres, foci and
# amulets wait for their own decision.
SLOTS = {'spell_damage': ['staff']}

# Fire and lightning added damage share Diablo's one elemental weapon slot, so they can't both roll.
EXTRA_CONFLICTS = [('local_added_fire', 'local_added_lightning')]

# The caster weapons rework's families, appended after every family above (caster-weapons-plan-v2.md 5.2): POE2's rows,
# their item levels moved to our curve, the +level families from item level 20, and the offensive staff and wand
# families and the sceptre's +level family at half size (caster_half_scale).
CASTER_AFFIXES = os.path.join(ROOT, 'tools', 'poe2_caster_affixes.tsv')
CASTER_MAX_ITEM_LEVEL = 82
CASTER_SKILL_LEVEL_FLOOR = 20
CASTER_HALF_SCALE = 50
# The rework's families whose natural column isn't their enabled one ('true' or 'false'): none yet.
CASTER_NATURAL = {}


def main():
	if len(sys.argv) != 2:
		sys.exit(__doc__)
	with open(sys.argv[1], encoding='utf-8') as f:
		data = json.load(f)
	families = data['modifier_families']

	family_rows = []
	tier_rows = []
	for family in families:
		fid = family['id']
		effect, scale1, scale2, word, enabled, natural = FAMILIES.get(fid, ('', 100, 100, '', False, False))
		if enabled and not effect:
			sys.exit(f'{fid}: enabled without an effect')
		legacy1, legacy2 = LEGACY_SCALES.get(fid, (scale1, scale2))
		conflicts = list(family['conflicts'])
		for a, b in EXTRA_CONFLICTS:
			if fid == a:
				conflicts.append(b)
			elif fid == b:
				conflicts.append(a)
		slots = legacy_slots(SLOTS.get(fid, list(dict.fromkeys(family['valid_slots']))))
		family_rows.append([
			fid,
			family['name'],
			word,
			family['generation_type'],
			family['group'],
			','.join(family['tags']),
			','.join(slots),
			','.join(family['requires_any_base_tags']),
			','.join(conflicts),
			game_template(family['display_template']),
			effect,
			str(scale1),
			str(scale2),
			'true' if enabled else 'false',
			str(legacy1),
			str(legacy2),
			'true' if natural else 'false',
		])
		if fid in TIER_OVERRIDES:
			for row in TIER_OVERRIDES[fid]:
				tier_rows.append([fid] + [str(v) for v in row])
			continue
		tiers = sorted(family['tiers'], key=lambda t: int(t['tier'][1:]))
		for tier in tiers:
			number = int(tier['tier'][1:])
			stats = tier['stats']
			values = []
			for stat in stats[:2]:
				lo, hi = stat['min'], stat['max']
				if effect.endswith('SkillLevels'):
					lo = hi = SKILL_LEVEL_VALUES[number - 1]
				values += [lo, hi]
			while len(values) < 4:
				values.append(0)
			# Fractional values (leech) are kept in tenths; those families are disabled for now.
			values = [str(v if isinstance(v, int) or float(v).is_integer() else round(v * 10)) for v in values]
			values = [str(int(float(v))) for v in values]
			tier_rows.append([fid, str(number), str(tier['required_item_level']), str(tier['spawn_weight'])] + values)

	for fid, name, word, side, slots, template, effect, lo, hi in JEWEL_FAMILIES:
		family_rows.append([fid, name, word, side, fid, 'jewel', slots, '', '', template, effect, '100', '100', 'true', '100', '100', 'true'])
		tier_rows.append([fid, '1', '1', str(JEWEL_WEIGHT), str(lo), str(hi), '0', '0'])

	for row, tiers in EXTRA_FAMILIES:
		family_rows.append(row)
		for tier in tiers:
			tier_rows.append([row[0]] + [str(v) for v in tier])

	caster_families, caster_tiers = caster_affixes()
	family_rows += caster_families
	tier_rows += caster_tiers

	write(os.path.join(OUT_DIR, 'modifier_families.tsv'),
	      ['id', 'name', 'word', 'side', 'group', 'tags', 'slots', 'baseTags', 'conflicts', 'template', 'effect', 'scale1', 'scale2', 'enabled', 'legacyScale1', 'legacyScale2', 'natural'],
	      family_rows)
	write(os.path.join(OUT_DIR, 'modifier_tiers.tsv'),
	      ['family', 'tier', 'itemLevel', 'weight', 'min1', 'max1', 'min2', 'max2'],
	      tier_rows)


def caster_damage_group(fid):
	"""A staff or wand carries one damage prefix, as Diablo's prefix does: its spell, hybrid and element damage share a group."""
	match = re.fullmatch(r'(staff|wand)_(spell_damage|spell_damage_mana|(fire|cold|lightning|physical|chaos)_spell_damage)', fid)
	return f'{match.group(1)}_spell_damage' if match else None


def caster_half_scale(fid):
	"""The offensive staff and wand families and the sceptre's +level family roll at half POE2's size (the pace knob,
	plan 5.2). The staff and wand +spell levels keep POE2's numbers, so a two-handed staff rolls up to +6 and a wand +4
	(Curtis 2026-10-30)."""
	if re.fullmatch(r'(staff|wand)_\w+_skill_levels', fid):
		return False
	if fid.endswith('_skill_levels'):
		return True
	return re.fullmatch(r'(staff|wand)_(maximum_mana|spell_damage|spell_damage_mana|(fire|cold|lightning|physical|chaos)_spell_damage|cast_speed)', fid) is not None


def caster_item_level(poe2_level, rank, skill_levels):
	"""POE2's area level on our item level curve (x1.3, loot.cpp), held below 83 - rank so T1 stays reachable at 82."""
	level = min(round(Fraction(13 * poe2_level, 10)), CASTER_MAX_ITEM_LEVEL + 1 - rank)
	if skill_levels:
		# +Levels start where today's spell_skill_levels does.
		level = max(level, CASTER_SKILL_LEVEL_FLOOR)
	return level


def caster_fixes(row):
	"""The plan's changes to the POE2 rows (caster-weapons-plan-v2.md 4.6)."""
	fid = row[0]
	if fid.endswith('_light_mana_regeneration'):
		# The 18-22 is the mana regeneration, the 5-15 the light radius.
		row[9] = '{0}% increased Mana Regeneration Rate; {1}% increased Light Radius'
	if fid.endswith('_ignite_magnitude'):
		row[1] = 'Flammability Magnitude'
		row[9] = '{0}% increased Flammability Magnitude'
	if re.fullmatch(r'sceptre_ally_added_(physical|fire|cold|lightning)', fid):
		row[10] = 'MinionAddedDamage'
		row[13] = 'true'
	if fid == 'sceptre_ally_life_regeneration':
		row[10] = 'MinionLifeRegen'
		row[13] = 'true'
		# Its values are tenths of a point a second: as many points every 10 seconds.
		row[9] = 'Minions regenerate {0} Life every 10 seconds'
	return row


def caster_affixes():
	"""The caster weapons rework's families and tiers: tools/poe2_caster_affixes.tsv with the plan's transform (5.2)."""
	families = []
	tiers = []
	with open(CASTER_AFFIXES, encoding='utf-8') as f:
		for line in f:
			line = line.rstrip('\r\n')
			if not line or line.startswith('#'):
				continue
			kind, *fields = line.split('\t')
			if kind == 'family':
				row = caster_fixes(fields)
				fid = row[0]
				row[4] = caster_damage_group(fid) or row[4]
				if caster_half_scale(fid):
					row[11] = row[14] = str(CASTER_HALF_SCALE)
				# Every family of the rework that is enabled rolls naturally; the others don't roll at all.
				row.append(CASTER_NATURAL.get(fid, row[13]))
				families.append(row)
			elif kind == 'tier':
				fid, tier, level, *rest = fields
				tiers.append([fid, tier, str(caster_item_level(int(level), int(tier), fid.endswith('_skill_levels')))] + rest)
			else:
				sys.exit(f'{CASTER_AFFIXES}: unknown row kind {kind}')
	return families, tiers


def game_template(template):
	"""Numbers the placeholders {0} and {1}, in the order of the family's stats, and drops the '+' before an increase."""
	template = re.sub(r'\+(\{[a-z_]+\}% increased)', r'\1', template)
	names = []
	for name in re.findall(r'\{([a-z_]+)\}', template):
		if name not in names:
			names.append(name)
	for index, name in enumerate(names):
		template = template.replace('{' + name + '}', '{' + str(index) + '}')
	return template


def write(path, header, rows):
	with open(path, 'w', encoding='utf-8', newline='') as f:
		for row in [header] + rows:
			f.write('\t'.join(row) + '\r\n')


if __name__ == '__main__':
	main()
