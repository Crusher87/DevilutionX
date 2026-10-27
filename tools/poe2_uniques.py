#!/usr/bin/env python3
"""Turns the itemization bundle's unique_items.json into POE2 Mashup uniques.

Usage: tools/poe2_uniques.py path/to/03_Unique_Item_Data path/to/base_items.json

Writes mods/poe2/txtdata/poe2/uniques.tsv: each unique the game has a slot for, the base it drops on,
its tier and pool (global, or the boss whose table holds it), its level and its modifiers. A modifier
is an item effect the modifier engine knows (life, defences, attack speed, ...) or a rule the game
applies while the unique is worn (poe2/uniques.cpp), each with its value range and tooltip text.

Uniques of classes the game has no slot for (gloves, boots, belts, quivers), the chaos and poison
boss (the game has neither), the endgame mechanic pools and the flasks and charms are left out.
Claws drop as daggers, spears as quarterstaves, wands as sceptres and flails as one-handed maces.
The order of the rows decides which unique a saved item is, so new uniques only go at the end.

The bundle's heavy and swift weapons and its crossbows no longer drop (Curtis 2026-10-27: weapons are POE2's own
bases), so their uniques move to the balanced base of the same class and tier, a crossbow's to the bow of its tier.
The savedBase column keeps the base a moved unique was on, so items saved on it still find their unique.
"""

import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from poe2_bases import CLASSES, base_name as base_name_of, d1_level, is_retired_weapon, unique_type_level  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'mods', 'poe2', 'txtdata', 'poe2', 'uniques.tsv')

REMAP = {'claw': 'dagger', 'spear': 'quarterstaff', 'wand': 'sceptre', 'flail': 'one_hand_mace'}
ITEMDAT = os.path.join(ROOT, 'mods', 'poe2', 'txtdata', 'items', 'itemdat.tsv')
# The game's POE2 body armours (from poe2db, not the bundle), one line per defence mix, by bundle base id prefix.
BODY_LINES = {
	'armour_body': ['Rusted Cuirass', 'Iron Cuirass', 'Maraketh Cuirass', 'Full Plate', 'Juggernaut Plate', 'Champion Cuirass', 'Conqueror Plate'],
	'evasion_body': ['Leather Vest', 'Pathfinder Coat', 'Rhoahide Coat', "Scout's Vest", 'Corsair Vest', 'Hardleather Coat', 'Armoured Vest'],
	'es_body': ['Tattered Robe', "Hexer's Robe", 'Silk Robe', 'Votive Raiment', 'Elementalist Robe', 'Plated Raiment', 'Enlightened Robe'],
	'armour_evasion_body': ['Chain Mail Vest', 'Rogue Armour', 'Cloaked Mail', 'Scale Armour', 'Knight Armour', 'Gladiator Armour', 'Heroic Armour'],
	'armour_es_body': ['Pilgrim Vestments', 'Pelt Mantle', 'Mail Vestments', 'Shaman Mantle', 'Cleric Vestments', 'Revered Vestments', 'Corvus Mantle'],
	'evasion_es_body': ['Hermit Garb', 'Waxed Jacket', 'Marabout Garb', 'Wayfarer Jacket', "Scalper's Jacket", 'Monastic Garb', 'Devout Garb'],
	'tri_body': ['Hermit Garb', 'Waxed Jacket', 'Marabout Garb', 'Wayfarer Jacket', "Scalper's Jacket", 'Monastic Garb', 'Devout Garb'],
}
SKIPPED_SOURCES = {'keeper_of_the_deep'}
# Body armour uniques keep the base they were first made on, from before the body armours took POE2's levels: a saved
# unique finds itself among its base's uniques.
BODY_ARMOUR_BASES = {'Ghostweave': 'Cleric Vestments', "Mother's Veil": 'Cleric Vestments'}
TIER_VALUE = {'common': 2000, 'uncommon': 6000, 'rare': 15000, 'mythic': 40000, 'boss': 25000}

# Bundle modifier id -> (effect, value when the bundle gives none). Effects the modifier engine
# knows change the item itself; the rest are rules (poe2/uniques.hpp). 'Text' only shows its line.
EFFECTS = {
	'unique_damage': ('PhysicalPercent', None),
	'unique_attack_speed': ('AttackSpeed', None),
	'unique_life': ('Life', None),
	'unique_mana': ('Mana', None),
	'unique_defence': ('DefensePercent', None),
	'unique_attributes': ('AllAttributes', None),
	'unique_spell_damage': ('SpellDamage', None),
	'base_speed_penalty': ('AttackSpeed', -10),
	'boss_phys_speed': ('AttackSpeed', -15),
	'boss_variant_defence': ('AllResistances', None),
	'boss_variant_speed': ('ActionSpeed', None),
	'boss_lightning_self': ('LightningResistance', -10),
	# Martial weapons.
	'convert_phys_to_fire': ('PhysicalToFire', 50),
	'ignite_buildup': ('IgniteBuildup', None),
	'cold_disabled': ('NoColdDamage', 1),
	'speed_from_missing_life': ('MissingLifeAttackSpeed', 20),
	'low_life_accuracy': ('LowLifeUnevadable', 1),
	'recovery_penalty': ('LessLifeRecovery', 25),
	'crit_to_bleed': ('CritBleed', 80),
	'crit_bonus': ('CritDamageBonus', None),
	'noncrit_penalty': ('NonCritLess', 15),
	'fortify_on_heavy_hit': ('GuardOnHeavyHit', 5),
	'guard_damage': ('GuardStackDamage', 2),
	'guard_speed_penalty': ('GuardStackSlow', 1),
	'element_rotation': ('ThirdAttackElemental', 60),
	'element_exposure': ('ThirdAttackExposure', 1),
	'physical_noncycle_penalty': ('OtherAttacksLessPhysical', 20),
	'echo_attack': ('FifthAttackRepeat', 70),
	'repeat_speed': ('Text', 30),
	# Caster weapons and foci.
	'mana_as_spell_damage': ('ManaSpellDamage', 20),
	'mana_cost_penalty': ('ManaCost', 25),
	'repeat_after_stationary': ('StillSpellRepeat', 4),
	'moving_cast_penalty': ('MovingCastSlow', 15),
	'element_cycle_spell': ('SpellElementCycle', 35),
	'off_element_penalty': ('OffElementLess', 20),
	'es_spend_power': ('SpellsSpendEnergyShield', 30),
	'es_recharge_penalty': ('LessEnergyShieldRecharge', 50),
	'ailment_memory': ('AilmentMemory', 6),
	'memory_penetration': ('MemoryPenetration', 12),
	'one_memory': ('Text', 1),
	'spell_echo_kill': ('SpellEchoes', 5),
	'boss_echo_gain': ('BossEchoes', 2),
	'echo_damage_penalty': ('RepeatedSpellLess', 30),
	# Armour.
	'armour_elemental': ('ArmourVsElements', 20),
	'evasion_zero': ('NoEvasion', 1),
	'evasion_spell': ('EvasionVsSpells', 25),
	'armour_penalty': ('LessArmour', 50),
	'es_life_recovery': ('LifeRecoveryToShield', 30),
	'es_recharge_disabled': ('NoEnergyShieldRecharge', 1),
	'resist_overcap_guard': ('OvercapResistGuard', 8),
	'max_res_penalty': ('MaxElementalResistance', -2),
	'stationary_bastion': ('StationaryDefences', 25),
	'moving_vulnerability': ('MovingVulnerability', 10),
	'guard_break_heal': ('BigHitRecovery', 15),
	'flask_recovery_penalty': ('LessLifeFlaskRecovery', 25),
	# Rings.
	'fire_overcap_damage': ('FireOvercapDamage', 3),
	'cold_max_penalty': ('MaxMagicResistance', -5),
	'mana_to_es': ('ManaToEnergyShield', 25),
	'life_penalty': ('LessMaxLife', 15),
	'crit_recent_speed': ('CritActionSpeed', 15),
	'noncrit_damage': ('NonCritLess', 10),
	'resistance_swap': ('ResistanceShare', 50),
	'kill_memory': ('StealRareMod', 20),
	'mirror_ring': ('MirrorRing', 1),
	'own_implicit_disabled': ('Text', 1),
	# Amulets.
	'highest_attribute_skill': ('HighestAttributeDamage', 20),
	'spirit_to_damage': ('SpiritDamage', 2),
	'reserve_penalty': ('MoreSpiritReservation', 10),
	'low_life_levels': ('LowLifeSkillLevels', 2),
	'max_life_penalty': ('LessMaxLife', 20),
	'ailment_conversion': ('StunThreshold', 50),
	'keystone_echo': ('FirstKeystone', 50),
	'second_keystone_penalty': ('OtherKeystones', 10),
	'skill_level_cost': ('AllSkillLevels', 3),
	'resource_cost': ('ManaCost', 35),
	# Boss uniques.
	'boss_fire_rule': ('IgniteSpread', 2),
	'boss_fire_power': ('IgniteBuildup', None),
	'boss_fire_cost': ('IgniteSpreadSelfDamage', 1),
	'boss_minion_rule': ('MinionFrenzy', 10),
	'boss_minion_respawn': ('MinionResummon', 3),
	'boss_minion_life_cost': ('LessMinionLife', 20),
	'boss_phys_rule': ('ArmourStunBuildup', 250),
	'boss_lightning_rule': ('Electrocute', 50),
	'boss_lightning_power': ('ElectrocutedLightning', 15),
	'boss_cold_rule': ('FreezeStoresChill', 30),
	'boss_cold_fire_penalty': ('CannotIgnite', 1),
	'boss_crit_rule': ('LuckyCrits', 1),
	'boss_crit_noncrit': ('NonCritLess', 25),
	'boss_bleed_rule': ('BleedHinder', 12),
	'boss_bleed_self': ('Text', 20),
	'boss_dark_rule': ('DistanceRule', 25),
	'boss_dark_mid': ('Text', 1),
	'boss_caster_rule': ('SixthSpellRepeat', 45),
	'boss_caster_cost': ('RepeatManaDrain', 10),
	'boss_armour_rule': ('ArmourPhysicalReduction', 12),
	'boss_armour_move': ('HeavyArmourSlow', 20),
	'boss_resource_rule': ('Text', 1),
	'boss_resource_power': ('ResourceSpentDamage', 30),
	'boss_resource_stop': ('ResourceLock', 1),
}

RANGE = re.compile(r'(\d+)–(\d+)')


# Modifiers that change keystones.
KEYSTONE_MODS = {mod for mod, (effect, _) in EFFECTS.items() if effect in ('FirstKeystone', 'OtherKeystones')}


def effect_text(display):
	"""The tooltip text with {0} for a rolled range, in the game's words for resistances."""
	text = RANGE.sub('{0}', display, count=1)
	# The effects column separates effects with semicolons, so a text keeps them as commas.
	return text.replace('Cold Resistance', 'Magic Resistance').replace('–', '-').replace(';', ',')


def main():
	if len(sys.argv) != 3:
		sys.exit(__doc__)
	with open(os.path.join(sys.argv[1], 'unique_items.json'), encoding='utf-8') as f:
		uniques = json.load(f)['unique_items']
	with open(os.path.join(sys.argv[1], 'boss_tables.json'), encoding='utf-8') as f:
		bosses = {b['boss_id'] for b in json.load(f)['boss_tables']}
	with open(sys.argv[2], encoding='utf-8') as f:
		bases = [b for b in json.load(f)['base_items'] if b['item_class'] in CLASSES]
	by_id = {b['id']: b for b in bases}
	balanced = {(b['item_class'], b['progression_tier']): b for b in bases if b.get('design_identity') == 'balanced'}
	with open(ITEMDAT, encoding='utf-8') as f:
		item_levels = {row[7]: int(row[9]) for row in (line.rstrip('\r\n').split('\t') for line in f) if row[0] == '' and row[3] == 'Armor'}

	rows = []
	names = set()
	for u in uniques:
		cls = REMAP.get(u['item_class'], u['item_class'])
		source = u['acquisition'].get('source_id', 'global') if u['unique_tier'] == 'restricted' else 'global'
		if (cls not in CLASSES and cls != 'body_armour') or source in SKIPPED_SOURCES or (source != 'global' and source not in bosses):
			continue
		# The passive tree has no keystones for these to change.
		if any(m['id'] in KEYSTONE_MODS for m in u['fixed_modifiers']):
			continue
		level = u['minimum_drop_level']
		if cls == 'body_armour':
			line = BODY_LINES[u['base_id'].split('_body_armour_')[0] + '_body']
			fits = [name for name in line if item_levels[name] <= d1_level(level)] or line[:1]
			base_name = BODY_ARMOUR_BASES.get(u['name']) or max(fits, key=lambda name: item_levels[name])
		else:
			base_name = None
		base = by_id.get(u['base_id']) if base_name is None else None
		if base_name is not None:
			pass
		elif base is None or base['item_class'] != cls:
			# A remapped class: the strongest base of the new class that drops by the unique's level.
			fits = [b for b in bases if b['item_class'] == cls and b['drop_level'] <= level]
			base = max(fits, key=lambda b: (b['drop_level'], b['id']))
		saved_base = ''
		if base_name is None:
			assert base['drop_level'] <= level, u['id']
			base_name = base_name_of(base)
			if is_retired_weapon(base):
				saved_base = base_name
				base = balanced[('bow' if base['item_class'] == 'crossbow' else base['item_class'], base['progression_tier'])]
				assert base['drop_level'] <= level, u['id']
				base_name = base_name_of(base)
		effects = []
		for m in u['fixed_modifiers']:
			assert m['id'] in EFFECTS, f'{u["id"]}: no effect for {m["id"]}'
			effect, value = EFFECTS[m['id']]
			if 'min' in m:
				lo, hi = m['min'], m['max']
			elif value is not None:
				lo = hi = value
			else:
				lo = hi = m['value']
			effects.append(f'{effect}|{lo}|{hi}|{effect_text(m["display"])}')
		tier = 'boss' if source != 'global' else u['unique_tier']
		assert u['name'] not in names, u['name']
		names.add(u['name'])
		rows.append([
			# The Diablo level stays on the scale the uniques were made with (UIMinLvl, which rebuilds saved ones).
			u['name'], base_name, tier, source, str(unique_type_level(level)), str(level),
			str(TIER_VALUE[tier] + level * 100), ';'.join(effects), saved_base,
		])

	with open(OUT, 'w', encoding='utf-8', newline='') as f:
		for row in [['name', 'base', 'tier', 'pool', 'minLevel', 'dropLevel', 'value', 'effects', 'savedBase']] + rows:
			f.write('\t'.join(row) + '\r\n')
	print(f'{len(rows)} uniques')


if __name__ == '__main__':
	main()
