#!/usr/bin/env python3
"""Turns the itemization bundle's modifier_families.json into the POE2 Mashup modifier tables.

Usage: tools/poe2_modifiers.py path/to/modifier_families.json

Writes mods/poe2/txtdata/poe2/modifier_families.tsv (one row per family) and
modifier_tiers.tsv (one row per tier, T1 the strongest). The bundle's numbers are
GAME-DEFAULT tuning, kept as they are; the scale columns say how the game shrinks
a value to Diablo's scale (100 = unchanged).
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
	'flat_armour': ('FlatDefense', 15, 100, 'Lacquered'),
	'flat_evasion': ('FlatDefense', 15, 100, "Shade's"),
	'flat_energy_shield': ('FlatDefense', 10, 100, 'Glimmering'),
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
	'local_added_physical': ('AddedPhysical', 25, 25, 'Glinting'),
	'local_increased_physical': ('PhysicalPercent', 100, 100, 'Heavy'),
	'physical_accuracy_hybrid': ('PhysicalPercentAndAccuracy', 100, 10, "Squire's"),
	'local_added_fire': ('AddedFire', 25, 25, 'Heated'),
	'local_added_lightning': ('AddedLightning', 25, 25, 'Humming'),
	'minion_damage': ('MinionDamage', 100, 100, "Baron's"),
	'global_added_physical_attacks': ('AddedPhysicalToAttacks', 25, 25, 'Burnished'),
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
	'accuracy_rating': ('Accuracy', 10, 100, 'of Calm'),
	'reduced_attribute_requirements': ('ReducedRequirements', 100, 100, 'of the Worthy'),
	'life_regeneration': ('LifeRegeneration', 100, 100, 'of the Newt'),
	'mana_regeneration': ('ManaRegeneration', 100, 100, 'of Excitement'),
	'melee_skill_levels': ('MeleeSkillLevels', 100, 100, 'of Combat'),
	'projectile_skill_levels': ('ProjectileSkillLevels', 100, 100, 'of the Archer'),
	'spell_skill_levels': ('SpellSkillLevels', 100, 100, 'of the Magus'),
	'fire_skill_levels': ('FireSkillLevels', 100, 100, 'of Flames'),
	'lightning_skill_levels': ('LightningSkillLevels', 100, 100, 'of Thunder'),
}

# The bundle's skill level tiers repeat (+1 four times, +2 twice, then +3 and +4), so they become +1, +2 and +3.
SKILL_LEVEL_VALUES = [3, 3, 2, 2, 1, 1, 1, 1]

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
		conflicts = list(family['conflicts'])
		for a, b in EXTRA_CONFLICTS:
			if fid == a:
				conflicts.append(b)
			elif fid == b:
				conflicts.append(a)
		slots = list(dict.fromkeys(family['valid_slots']))
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
			'true' if effect else 'false',
		])
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

	write(os.path.join(OUT_DIR, 'modifier_families.tsv'),
	      ['id', 'name', 'word', 'side', 'group', 'tags', 'slots', 'baseTags', 'conflicts', 'template', 'effect', 'scale1', 'scale2', 'enabled'],
	      family_rows)
	write(os.path.join(OUT_DIR, 'modifier_tiers.tsv'),
	      ['family', 'tier', 'itemLevel', 'weight', 'min1', 'max1', 'min2', 'max2'],
	      tier_rows)


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
