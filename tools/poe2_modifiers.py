#!/usr/bin/env python3
"""Turns the itemization bundle's modifier_families.json into the POE2 Mashup modifier tables.

Usage: tools/poe2_modifiers.py path/to/modifier_families.json

Writes mods/poe2/txtdata/poe2/modifier_families.tsv (one row per family) and
modifier_tiers.tsv (one row per tier, T1 the strongest). The bundle's numbers are
GAME-DEFAULT tuning, kept as they are; the scale columns say how the game shrinks
a value to Diablo's scale (100 = unchanged). The legacy scale columns keep the scales
items had before the bases moved to POE2's numbers (harder monsters plan, step 10b):
the game rolls an item saved before then with them, so it keeps its numbers.
"""

import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(ROOT, 'mods', 'poe2', 'txtdata', 'poe2')

# family id -> (effect, scale of the first stat, scale of the second stat, name word)
# Families missing here have no effect in the game yet and load disabled.
FAMILIES = {
	'maximum_life': ('Life', 100, 100, 'Virile'),
	'maximum_mana': ('Mana', 100, 100, 'Azure'),
	# Bases have POE2's defence, so flat armour and evasion keep the bundle's numbers; energy shield is in defence
	# points, three energy shield each.
	'flat_armour': ('FlatArmour', 100, 100, 'Lacquered'),
	'flat_evasion': ('FlatEvasion', 100, 100, "Shade's"),
	'flat_energy_shield': ('FlatEnergyShield', 33, 100, 'Glimmering'),
	'increased_armour': ('DefensePercent', 100, 100, 'Reinforced'),
	'increased_evasion': ('DefensePercent', 100, 100, 'Agile'),
	'increased_energy_shield': ('DefensePercent', 100, 100, 'Protective'),
	'hybrid_armour_evasion': ('DefensePercent', 100, 100, "Scrapper's"),
	'hybrid_armour_es': ('DefensePercent', 100, 100, 'Infixed'),
	'hybrid_evasion_es': ('DefensePercent', 100, 100, 'Shadowy'),
	'life_armour_hybrid': ('LifeAndDefensePercent', 100, 100, "Rhino's"),
	'life_evasion_hybrid': ('LifeAndDefensePercent', 100, 100, "Fawn's"),
	'life_es_hybrid': ('LifeAndDefensePercent', 100, 100, "Monk's"),
	'mana_es_hybrid': ('ManaAndDefensePercent', 100, 100, "Seer's"),
	'spirit_flat': ('Spirit', 500, 100, 'Spirited'),
	# Weapons hit for the bundle's damage, so added damage keeps its numbers too.
	'local_added_physical': ('AddedPhysical', 100, 100, 'Glinting'),
	'local_increased_physical': ('PhysicalPercent', 100, 100, 'Heavy'),
	'physical_accuracy_hybrid': ('PhysicalPercentAndAccuracy', 100, 10, "Squire's"),
	'local_added_fire': ('AddedFire', 100, 100, 'Heated'),
	'local_added_lightning': ('AddedLightning', 100, 100, 'Humming'),
	'minion_damage': ('MinionDamage', 100, 100, "Baron's"),
	'global_added_physical_attacks': ('AddedPhysicalToAttacks', 100, 100, 'Burnished'),
	'strength': ('Strength', 40, 100, 'of the Brute'),
	'dexterity': ('Dexterity', 40, 100, 'of the Mongoose'),
	'intelligence': ('Intelligence', 40, 100, 'of the Prodigy'),
	'all_attributes': ('AllAttributes', 40, 100, 'of the Clouds'),
	'fire_resistance': ('FireResistance', 100, 100, 'of the Whelpling'),
	# Diablo has no cold damage, so cold resistance guards against magic, its third element.
	'cold_resistance': ('MagicResistance', 100, 100, 'of the Penguin'),
	'lightning_resistance': ('LightningResistance', 100, 100, 'of the Squall'),
	'all_elemental_resistance': ('AllResistances', 100, 100, 'of the Crystal'),
	'attack_speed': ('AttackSpeed', 100, 100, 'of Skill'),
	'cast_speed': ('CastSpeed', 100, 100, 'of Talent'),
	# Accuracy is Diablo's percent chance to hit, so it stays a tenth.
	'accuracy_rating': ('Accuracy', 10, 100, 'of Calm'),
	'reduced_attribute_requirements': ('ReducedRequirements', 100, 100, 'of the Worthy'),
	'life_regeneration': ('LifeRegeneration', 100, 100, 'of the Newt'),
	'mana_regeneration': ('ManaRegeneration', 100, 100, 'of Excitement'),
	'melee_skill_levels': ('MeleeSkillLevels', 100, 100, 'of Combat'),
	'projectile_skill_levels': ('ProjectileSkillLevels', 100, 100, 'of the Archer'),
	'spell_skill_levels': ('SpellSkillLevels', 100, 100, 'of the Magus'),
	# Staves roll increased spell damage like POE2's (harder monsters plan step 14): it adds to the tree's spell damage.
	'spell_damage': ('SpellDamage', 100, 100, "Apprentice's"),
	'fire_skill_levels': ('FireSkillLevels', 100, 100, 'of Flames'),
	'lightning_skill_levels': ('LightningSkillLevels', 100, 100, 'of Thunder'),
	'jewel_maximum_life_pct': ('LifePercent', 100, 100, 'Healthy'),
	'jewel_maximum_mana_pct': ('ManaPercent', 100, 100, 'Wise'),
	'jewel_damage_pct': ('DamagePercent', 100, 100, 'Brutal'),
	'jewel_attack_speed': ('AttackSpeed', 100, 100, 'of Alacrity'),
	'jewel_cast_speed': ('CastSpeed', 100, 100, 'of Enchanting'),
	'item_rarity': ('ItemRarity', 100, 100, 'of Plunder'),
}

# Tiers the game sets apart from the bundle's: family id -> [(tier, item level, weight, min1, max1, min2, max2)].
TIER_OVERRIDES = {
	# The Magic Find suffix: three tiers that start low (loot plan).
	'item_rarity': [(1, 40, 420, 15, 18, 0, 0), (2, 24, 560, 11, 14, 0, 0), (3, 3, 640, 6, 10, 0, 0)],
}

# Families the bundle doesn't have, after the jewel families: the family row and its tiers.
EXTRA_FAMILIES = [
	(['item_rarity_prefix', 'Item Rarity', "Magpie's", 'prefix', 'item_rarity_prefix', 'rarity', 'helmet,ring,amulet', '', '',
	  '{0}% increased Rarity of Items found', 'ItemRarity', '100', '100', 'true', '100', '100'],
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
		effect, scale1, scale2, word = FAMILIES.get(fid, ('', 100, 100, ''))
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
			game_text(family['name'], effect),
			word,
			family['generation_type'],
			family['group'],
			','.join(family['tags']),
			','.join(slots),
			','.join(family['requires_any_base_tags']),
			','.join(conflicts),
			game_text(game_template(family['display_template']), effect),
			effect,
			str(scale1),
			str(scale2),
			'true' if effect else 'false',
			str(legacy1),
			str(legacy2),
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
		family_rows.append([fid, name, word, side, fid, 'jewel', slots, '', '', template, effect, '100', '100', 'true', '100', '100'])
		tier_rows.append([fid, '1', '1', str(JEWEL_WEIGHT), str(lo), str(hi), '0', '0'])

	for row, tiers in EXTRA_FAMILIES:
		family_rows.append(row)
		for tier in tiers:
			tier_rows.append([row[0]] + [str(v) for v in tier])

	write(os.path.join(OUT_DIR, 'modifier_families.tsv'),
	      ['id', 'name', 'word', 'side', 'group', 'tags', 'slots', 'baseTags', 'conflicts', 'template', 'effect', 'scale1', 'scale2', 'enabled', 'legacyScale1', 'legacyScale2'],
	      family_rows)
	write(os.path.join(OUT_DIR, 'modifier_tiers.tsv'),
	      ['family', 'tier', 'itemLevel', 'weight', 'min1', 'max1', 'min2', 'max2'],
	      tier_rows)


def game_text(text, effect):
	"""Names the stat the game gives: POE2's cold resistance is the game's magic resistance, so it reads that way."""
	if effect == 'MagicResistance':
		text = text.replace('Cold Resistance', 'Magic Resistance')
	return text


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
