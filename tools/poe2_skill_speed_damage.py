#!/usr/bin/env python3
"""Writes the damage_pct and mana_pct columns of mods/poe2/txtdata/poe2/skill_speed.tsv: slower skills hit harder, as
in POE2, and cost a little more for it.

Rule (Curtis 2026-10-27: "a skill that takes longer hits harder, so it beats the free attack per second"): a skill
that deals damage and takes longer than the free use it is weighed against hits harder by exactly how much longer it
takes, rounded up to a whole percent. Its damage per second against one target is then what it was when every use took
the same time, which was already above the free attack's: the free attack hits for 85% of the weapon
(DefaultAttackDamagePercent) and a plain skill for 100% or more. A skill as quick as the free use or quicker keeps 100.

- Weapon attacks (attack_speed_pct): the free attack is the weapon's own, 100%: the time ratio is 100 / attack_speed_pct.
  Flat added time (Leap Slam's 650 ms) and fixed attack times (the shield skills) are weighed at REFERENCE_APS, the
  starting Shortsword's 1.55 attacks a second; the time a dash adds by its length (distance_ms) is not counted.
- Bow skills (BOW_SKILLS) are weighed against REFERENCE_APS too, not the bow's own 1.2 attacks a second (Curtis
  2026-10-27, "Bow skills +29%"): a use takes 1 / (BOW_APS x attack_speed_pct) plus its added time, and the ratio is that
  time x REFERENCE_APS. Hail of Arrows, as quick as the bow, hits for 130%; the free bow shot is unchanged.
- Casts (cast_time_s, the spells and spell-like skills): the free use is the staff bolt, a 0.9-second cast
  (Default StaffBolt), so the ratio is (cast time + added time) / 0.9.
- Diablo's damage spells (the "spell" rows of spell_damage.tsv, whose damage a cast deals) are weighed against one spell
  baseline, SPELL_BASELINE_S, 0.645 s: the starting Shortsword's swing, which every attack skill's budget a second is
  set by too (Curtis 2026-10-27, "Re-time all spells"; research/skill-balance/audit-damage-v2.md section 3). They used
  to be weighed against the old 0.40 s held cast, which made them about 1.6 times an attack skill a second; their
  percents in spell_damage.tsv were re-derived for the new ratio. Walls, runes, Guardian and the other spells with no
  row keep the staff bolt's ratio, as the Elemental and Occult gems' casts do (their numbers wait on the owner).
- Steps ("Flurry:backhand") and the free uses' own rows ("Default ...") get no damage_pct; a skill's percent is on its
  main row. Skills that deal no damage (curses, buffs, warcries, Shield Wall) get none.

mana_pct (Curtis 2026-10-27, "Half as much"): a skill that hits harder for its speed costs more mana by half as much,
100 + (damage_pct - 100) / 2, rounded up: damage_pct 150 costs 125%. A skill with no damage_pct keeps its cost.

Run from anywhere: python3 tools/poe2_skill_speed_damage.py (rewrites the column in place, keeping every other column).
"""
import csv
import math
import os

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
TSV = os.path.join(ROOT, 'mods', 'poe2', 'txtdata', 'poe2', 'skill_speed.tsv')

SPELL_DAMAGE_TSV = os.path.join(ROOT, 'mods', 'poe2', 'txtdata', 'poe2', 'spell_damage.tsv')

REFERENCE_APS = 1.55
# The Crude Bow's attacks a second (DefaultBowAps), the bow the bow skills' damage_pct is worked from.
BOW_APS = 1.20
FREE_CAST_S = 0.9
SPELL_BASELINE_S = 0.645
COLUMN = 'damage_pct'
MANA_COLUMN = 'mana_pct'

# Bow skills with a row (the retired crossbow skills have none).
BOW_SKILLS = {'Barrage', 'RainOfArrows', 'LightningArrow', 'EscapeShot', 'Multishot', 'GuidedArrow', 'FreezingArrow', 'ClusterBolt', 'ExplosiveBolt'}

# Rows for skills that deal no damage of their own: the percent would change nothing, so they get none.
NO_DAMAGE = {
	'BattleCry', 'WarCry', 'AmplifyDamage', 'DeathMark', 'HolyShield', 'FrozenArmor', 'ShieldWall', 'SoulHarvest', 'BonePrison',
	'Weaken', 'IronMaiden', 'Decrepify', 'LowerResist', 'Confuse', 'Attract',
	'Spell Healing', 'Spell ManaShield', 'Spell StoneCurse', 'Spell Phasing', 'Spell Golem', 'Spell Teleport', 'Spell Warp',
	'Spell Etherealize', 'Spell Berserk', 'Spell Reflect',
}


def number(text):
	return float(text) if text.strip() else 0.0


def damage_spells():
	"""The skill_speed.tsv keys of the spells spell_damage.tsv gives a damage per cast."""
	with open(SPELL_DAMAGE_TSV, newline='') as f:
		return {'Spell ' + r['name'] for r in csv.DictReader(f, delimiter='\t') if r['kind'] == 'spell'}


DAMAGE_SPELLS = damage_spells()


def time_ratio(row):
	"""How much longer than its free use one use of the row takes; None when it is no skill of its own."""
	name = row['name']
	if name.startswith('Default ') or ':' in name or name in NO_DAMAGE:
		return None
	added = number(row['added_ms']) / 1000
	if row['attack_time_s'].strip():
		return (number(row['attack_time_s']) + added) * REFERENCE_APS
	if row['cast_time_s'].strip():
		reference = SPELL_BASELINE_S if name in DAMAGE_SPELLS else FREE_CAST_S
		return (number(row['cast_time_s']) + added) / reference
	percent = number(row['attack_speed_pct']) or 100
	if name in BOW_SKILLS:
		return ((100 / percent) / BOW_APS + added) * REFERENCE_APS
	return (100 / percent) + (added * REFERENCE_APS)


def damage_percent(row):
	ratio = time_ratio(row)
	if ratio is None or ratio <= 1.0 + 1e-9:
		return ''
	# Up to a whole percent, so the skill never falls under the free use's pace.
	return str(math.ceil(round(100 * ratio, 6)))


# Bolts keep the flat 4 mana Curtis set in round 1 ("all bolts cost 4"), whatever their speed adds.
MANA_KEPT = { 'Spell Firebolt' }


def mana_percent(name, damage):
	"""Half of the damage the speed adds, rounded up; none for a skill with no damage_pct."""
	if not damage:
		return ''
	if name in MANA_KEPT:
		return '100'
	return str(100 + math.ceil((int(damage) - 100) / 2))


def main():
	with open(TSV, newline='') as f:
		reader = csv.DictReader(f, delimiter='\t')
		fields = list(reader.fieldnames)
		rows = list(reader)
	if COLUMN not in fields:
		fields.insert(fields.index('note'), COLUMN)
	if MANA_COLUMN not in fields:
		fields.insert(fields.index('note'), MANA_COLUMN)
	changed = 0
	for row in rows:
		value = damage_percent(row)
		mana = mana_percent(row['name'], value)
		changed += row.get(COLUMN, '') != value or row.get(MANA_COLUMN, '') != mana
		row[COLUMN] = value
		row[MANA_COLUMN] = mana
	with open(TSV, 'w', newline='') as f:
		writer = csv.DictWriter(f, fieldnames=fields, delimiter='\t', lineterminator='\n')
		writer.writeheader()
		writer.writerows(rows)
	boosted = [(r['name'], r[COLUMN]) for r in rows if r[COLUMN]]
	print(f'{len(boosted)} of {len(rows)} rows hit harder for their speed ({changed} changed):')
	print(', '.join(f'{n} {v}' for n, v in boosted))


if __name__ == '__main__':
	main()
