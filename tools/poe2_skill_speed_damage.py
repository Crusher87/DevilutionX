#!/usr/bin/env python3
"""Writes the damage_pct column of mods/poe2/txtdata/poe2/skill_speed.tsv: slower skills hit harder, as in POE2.

Rule (Curtis 2026-10-27: "a skill that takes longer hits harder, so it beats the free attack per second"): a skill
that deals damage and takes longer than the free use it is weighed against hits harder by exactly how much longer it
takes, rounded up to a whole percent. Its damage per second against one target is then what it was when every use took
the same time, which was already above the free attack's: the free attack hits for 85% of the weapon
(DefaultAttackDamagePercent) and a plain skill for 100% or more. A skill as quick as the free use or quicker keeps 100.

- Weapon attacks (attack_speed_pct): the free attack is the weapon's own, 100%: the time ratio is 100 / attack_speed_pct.
  Flat added time (Leap Slam's 650 ms) and fixed attack times (the shield skills) are weighed at REFERENCE_APS, the
  starting Shortsword's 1.55 attacks a second; the time a dash adds by its length (distance_ms) is not counted.
- Casts (cast_time_s, the spells and spell-like skills): the free use is the staff bolt, a 0.9-second cast
  (Default StaffBolt), so the ratio is (cast time + added time) / 0.9.
- Diablo's damage spells (the "spell" rows of spell_damage.tsv, whose damage a cast deals) are weighed instead against
  the cast they were tuned at: before spells had cast times, a held cast took the Sorcerer's casting animation up to its
  cast frame, 8 game ticks (OLD_CAST_S, 0.40 s). Their ratio is (cast time + added time) / 0.40, so a spell deals the
  damage per second it did, and still more than the staff bolt. Walls, Guardian and the other spells with no row keep
  the staff bolt's ratio.
- Steps ("Flurry:backhand") and the free uses' own rows ("Default ...") get no damage_pct; a skill's percent is on its
  main row. Skills that deal no damage (curses, buffs, warcries, Shield Wall) get none.

Run from anywhere: python3 tools/poe2_skill_speed_damage.py (rewrites the column in place, keeping every other column).
"""
import csv
import math
import os

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
TSV = os.path.join(ROOT, 'mods', 'poe2', 'txtdata', 'poe2', 'skill_speed.tsv')

SPELL_DAMAGE_TSV = os.path.join(ROOT, 'mods', 'poe2', 'txtdata', 'poe2', 'spell_damage.tsv')

REFERENCE_APS = 1.55
FREE_CAST_S = 0.9
OLD_CAST_S = 0.40
COLUMN = 'damage_pct'

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
		reference = OLD_CAST_S if name in DAMAGE_SPELLS else FREE_CAST_S
		return (number(row['cast_time_s']) + added) / reference
	percent = number(row['attack_speed_pct']) or 100
	return (100 / percent) + (added * REFERENCE_APS)


def damage_percent(row):
	ratio = time_ratio(row)
	if ratio is None or ratio <= 1.0 + 1e-9:
		return ''
	# Up to a whole percent, so the skill never falls under the free use's pace.
	return str(math.ceil(round(100 * ratio, 6)))


def main():
	with open(TSV, newline='') as f:
		reader = csv.DictReader(f, delimiter='\t')
		fields = list(reader.fieldnames)
		rows = list(reader)
	if COLUMN not in fields:
		fields.insert(fields.index('note'), COLUMN)
	changed = 0
	for row in rows:
		value = damage_percent(row)
		changed += row.get(COLUMN, '') != value
		row[COLUMN] = value
	with open(TSV, 'w', newline='') as f:
		writer = csv.DictWriter(f, fieldnames=fields, delimiter='\t', lineterminator='\n')
		writer.writeheader()
		writer.writerows(rows)
	boosted = [(r['name'], r[COLUMN]) for r in rows if r[COLUMN]]
	print(f'{len(boosted)} of {len(rows)} rows hit harder for their speed ({changed} changed):')
	print(', '.join(f'{n} {v}' for n, v in boosted))


if __name__ == '__main__':
	main()
