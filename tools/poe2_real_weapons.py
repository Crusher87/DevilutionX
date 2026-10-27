#!/usr/bin/env python3
"""Turns Path of Building 2's weapon base data into tools/poe2_real_weapons.tsv, POE2's own weapon bases.

Usage: tools/poe2_real_weapons.py path/to/PathOfBuilding-PoE2/src/Data/Bases

Reads sword.lua, axe.lua, mace.lua, dagger.lua, staff.lua (its quarterstaves) and bow.lua, and writes one row per
base that drops in POE2: the class tools/poe2_bases.py calls it, its name, the level, Strength, Dexterity and
Intelligence it needs, its physical damage, its attacks per second in hundredths, its critical hit chance in tenths of a
percent and its implicit text. Bases that never drop (not for sale, hidden, runeforged, the Golden Blade and other
specials) and bases with other than physical damage are left out. tools/poe2_bases.py reads the table, so a build
needs no network; run this again only to take a newer Path of Building.
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'tools', 'poe2_real_weapons.tsv')

# Path of Building file -> {its type (and sub type) -> the class in the itemization bundle}.
FILES = {
	'sword.lua': {'One Hand Sword': 'one_hand_sword', 'Two Hand Sword': 'two_hand_sword'},
	'axe.lua': {'One Hand Axe': 'one_hand_axe', 'Two Hand Axe': 'two_hand_axe'},
	'mace.lua': {'One Hand Mace': 'one_hand_mace', 'Two Hand Mace': 'two_hand_mace'},
	'dagger.lua': {'Dagger': 'dagger'},
	'staff.lua': {'Staff/Warstaff': 'quarterstaff'},
	'bow.lua': {'Bow': 'bow'},
}
# Tags of bases that never drop as normal items.
SKIPPED_TAGS = {'not_for_sale', 'demigods', 'runeforged'}
# Specials without such a tag: the Golden Blade's second entry, the 3-socket Kalguuran hammer.
SKIPPED_NAMES = {'Golden Blade', 'Kalguuran Forgehammer'}

HEADER = ['class', 'name', 'level', 'str', 'dex', 'int', 'min', 'max', 'aps', 'crit', 'implicit']


def table(text):
	"""The key = value pairs of a Lua table body."""
	out = {}
	for m in re.finditer(r'(\w+)\s*=\s*("(?:[^"\\]|\\.)*"|[-\d.]+|true|false)', text):
		value = m.group(2)
		out[m.group(1)] = value[1:-1] if value.startswith('"') else value
	return out


def bases(path, types):
	text = open(path, encoding='utf-8').read()
	found = {}
	for m in re.finditer(r'^itemBases\["([^"]+)"\] = \{\n(.*?)\n\}', text, re.S | re.M):
		name, body = m.group(1), m.group(2)
		kind = re.search(r'^\ttype = "([^"]+)"', body, re.M).group(1)
		sub = re.search(r'^\tsubType = "([^"]+)"', body, re.M)
		cls = types.get(kind + ('/' + sub.group(1) if sub else ''))
		tags = re.search(r'^\ttags = \{(.*?)\},$', body, re.M)
		tags = {k for k, v in table(tags.group(1)).items() if v == 'true'} if tags else set()
		weapon = re.search(r'^\tweapon = \{(.*?)\},$', body, re.M)
		weapon = table(weapon.group(1)) if weapon else {}
		req = re.search(r'^\treq = \{(.*?)\},$', body, re.M)
		req = table(req.group(1)) if req else {}
		implicit = re.search(r'^\timplicit = "((?:[^"\\]|\\.)*)",$', body, re.M)
		# Lua keeps the last entry of a name, so this does too.
		found.pop(name, None)
		if cls is None or tags & SKIPPED_TAGS or name in SKIPPED_NAMES or name.startswith(('Runeforged', 'Runemastered')):
			continue
		if re.search(r'^\thidden = true', body, re.M) or 'PhysicalMin' not in weapon:
			continue
		if any(key.endswith('Min') and key != 'PhysicalMin' for key in weapon):
			continue
		found[name] = [
			cls, name, req.get('level', '0'), req.get('str', '0'), req.get('dex', '0'), req.get('int', '0'),
			weapon['PhysicalMin'], weapon['PhysicalMax'], str(round(float(weapon['AttackRateBase']) * 100)),
			str(round(float(weapon['CritChanceBase']) * 10)), implicit.group(1).replace('\\n', ' / ') if implicit else '',
		]
	return list(found.values())


def main():
	if len(sys.argv) != 2:
		sys.exit(__doc__)
	rows = []
	for name, types in FILES.items():
		rows += bases(os.path.join(sys.argv[1], name), types)
	order = {cls: i for i, cls in enumerate(cls for types in FILES.values() for cls in types.values())}
	# Each class by level, as POE2 lists them.
	rows.sort(key=lambda row: (order[row[0]], int(row[2])))
	with open(OUT, 'w', encoding='utf-8', newline='') as f:
		for row in [HEADER] + rows:
			f.write('\t'.join(row) + '\n')
	print(f'{len(rows)} weapon bases')


if __name__ == '__main__':
	main()
