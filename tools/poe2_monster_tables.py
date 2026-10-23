#!/usr/bin/env python3
"""Writes the POE2 Mashup monster tables (harder monsters plan v2, step 3).

- mods/poe2/txtdata/poe2/monster_levels.tsv: by monster level 1-100, a white monster's life, hit (in tenths) and
  accuracy. POE2's table (smoothed from 65 to 80 so it never jumps) times the fitted dials of step 1, life also times
  KILL_TARGET / 2 (the dials were fitted for 2 uses of the main skill a white). The poe2* columns are the table before
  the dials, for reference and tests.
- mods/poe2/txtdata/poe2/monster_types.tsv: every monster type's Diablo-scale references, the averages of the types
  that can spawn on its floors (its own minDunLvl..maxDunLvl within 1-24; Diablo floor 16, Na-Krul 24), in tenths, and
  its ratios to them in percent, clamped to 30-250: life (data midpoint), damage, and xp = the average of the two. The
  game rolls a monster's life as the table times clamp(roll / lifeRef) and its hit as the table times the type's damage
  percent.
- mods/poe2/txtdata/poe2/monster_bosses.tsv: life and damage of uniques in percent of a white at the floor's area level.
  The named bosses are fixed below; other uniques get clamp(own life / their floor's life ref, 3, 6) and
  clamp(own damage / their floor's damage ref, 1, 2.5). Diablo and Na-Krul count as bosses however they are placed.
- mods/poe2/txtdata/poe2/experience_bosses.tsv: the same life multipliers as experience percents (bosses and uniques
  pay for the life they carry).

Run from anywhere: python3 tools/poe2_monster_tables.py
"""
import csv
import math
import os

KILL_TARGET = 2        # uses of the main skill a white at the hero's area level takes (owner's pick)
FIT_KILL_TARGET = 2    # the KILL_TARGET the LIFE dials were fitted for (life scales linearly with it)

# POE2's monster table by level: life, damage per hit, accuracy (poe2-levelling.md s7), 65 -> 80 smoothed
# geometrically (80 = 31,065 / 1.25^2) so life never jumps.
POE2_ROWS = {
    1: (15, 9.16, 32), 10: (67, 20.44, 72), 20: (249, 37.29, 140), 30: (593, 59.94, 242), 40: (1253, 90.02, 391),
    50: (2556, 129.56, 606), 60: (4834, 181.12, 914), 65: (6555, 212.36, 1114), 70: (9489, 247.86, 1351),
    75: (13735, 288.14, 1632), 79: (18464, 324.19, 1893), 80: (19882, 333.78, 1964), 83: (33945, 364.00, 2192),
    85: (36012, 385.42, 2357), 90: (41748, 443.79, 2821), 100: (56106, 584.05, 4011),
}
# Fitted dials (research/levels-100/audit-numbers/v2/fit_v2.py, KILL_TARGET 2): area -> (life, damage, accuracy).
# Geometric between the fit areas, flat outside 2-80.
DIALS = {
    2: (1.65, 0.50, 0.66), 5: (1.46, 0.57, 0.50), 8: (1.06, 0.59, 0.38), 13: (1.72, 1.03, 0.73),
    25: (1.22, 1.07, 0.81), 38: (1.19, 0.91, 0.77), 50: (0.77, 0.74, 0.84), 63: (0.48, 0.60, 0.86),
    80: (0.26, 0.48, 0.94),
}
MAX_LEVEL = 100

# Bosses: life and damage in percent of a white at the floor's area level (plan v2, bosses and uniques).
BOSSES = [
    ('The Butcher', 700, 200), ('Skeleton King', 800, 160), ('Gharbad the Weak', 400, 150), ('Snotspill', 400, 200),
    ('Zhar the Mad', 400, 150), ('Hork Demon', 600, 150), ('The Defiler', 1000, 180), ('Warlord of Blood', 600, 200),
    ('Lachdanan', 500, 150), ('Red Vex', 400, 200), ('Black Jade', 400, 200), ('Arch-Bishop Lazarus', 1000, 180),
    ('Na-Krul', 1500, 200), ('MT_NAKRUL', 1500, 200), ('MT_DIABLO', 1500, 200),
]
UNIQUE_LIFE_BAND = (3.0, 6.0)
UNIQUE_DAMAGE_BAND = (1.0, 2.5)
# A unique not in the file (a mod's) gets the middle of the band.
OTHER_UNIQUE = (450, 150)
TYPE_BAND = (0.3, 2.5)
# Types that never spawn on floors 1-24 by their own range take their boss floor's references.
TYPE_FLOOR = {'MT_DIABLO': 16, 'MT_NAKRUL': 24}
FLOORS = range(1, 25)

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
MONSTDAT = os.path.join(ROOT, 'mods', 'hf', 'txtdata', 'monsters', 'monstdat.tsv')
UNIQUES = os.path.join(ROOT, 'assets', 'txtdata', 'monsters', 'unique_monstdat.tsv')
OUT = os.path.join(ROOT, 'mods', 'poe2', 'txtdata', 'poe2')


def geometric(table, level, column):
    if level in table:
        return table[level][column]
    keys = sorted(table)
    if level <= keys[0]:
        return table[keys[0]][column]
    if level >= keys[-1]:
        return table[keys[-1]][column]
    lo = max(k for k in keys if k < level)
    hi = min(k for k in keys if k > level)
    t = (level - lo) / (hi - lo)
    return math.exp(math.log(table[lo][column]) * (1 - t) + math.log(table[hi][column]) * t)


def poe2(level, column):
    return geometric(POE2_ROWS, level, column)


def dial(level, column):
    return geometric(DIALS, level, column)


def level_rows():
    rows = []
    for level in range(1, MAX_LEVEL + 1):
        life = poe2(level, 0) * dial(level, 0) * KILL_TARGET / FIT_KILL_TARGET
        rows.append((level, max(1, round(life)), max(1, round(poe2(level, 1) * dial(level, 1) * 10)),
                     max(1, round(poe2(level, 2) * dial(level, 2))),
                     round(poe2(level, 0)), round(poe2(level, 1) * 10), round(poe2(level, 2))))
    return rows


def read_tsv(path):
    with open(path, newline='') as f:
        return list(csv.DictReader(f, delimiter='\t'))


def clamp(value, low, high):
    return max(low, min(high, value))


def main():
    monsters = read_tsv(MONSTDAT)
    uniques = read_tsv(UNIQUES)
    for m in monsters:
        m['life'] = (int(m['hitPointsMinimum']) + int(m['hitPointsMaximum'])) / 2
        m['damage'] = (int(m['minDamage']) + int(m['maxDamage'])) / 2
        m['toHit'] = int(m['toHit'])
        m['lo'], m['hi'] = int(m['minDunLvl']), int(m['maxDunLvl'])

    # Per floor: the averages of the types that can spawn there (IsMonsterAvailable: not Never, in range).
    floor_refs = {}
    for floor in FLOORS:
        here = [m for m in monsters if m['availability'] != 'Never' and m['lo'] <= floor <= m['hi']]
        floor_refs[floor] = tuple(sum(m[key] for m in here) / len(here) for key in ('life', 'damage', 'toHit'))

    def type_refs(m):
        floors = [f for f in FLOORS if m['lo'] <= f <= m['hi']]
        if not floors:
            floors = [TYPE_FLOOR.get(m['_monster_id'], clamp(m['lo'], 1, 24))]
        return tuple(sum(floor_refs[f][i] for f in floors) / len(floors) for i in range(3))

    with open(os.path.join(OUT, 'monster_levels.tsv'), 'w', newline='') as out:
        out.write('level\tlife\tdamage10\taccuracy\tpoe2Life\tpoe2Damage10\tpoe2Accuracy\n')
        for row in level_rows():
            out.write('\t'.join(str(v) for v in row) + '\n')

    with open(os.path.join(OUT, 'monster_types.tsv'), 'w', newline='') as out:
        out.write('type\tlifeRef10\tdamageRef10\ttoHitRef10\tlifePercent\tdamagePercent\txpPercent\n')
        for m in monsters:
            life_ref, damage_ref, hit_ref = type_refs(m)
            life = clamp(m['life'] / life_ref, *TYPE_BAND)
            damage = clamp(m['damage'] / damage_ref, *TYPE_BAND)
            xp = clamp((life + damage) / 2, *TYPE_BAND)
            out.write(f"{m['_monster_id']}\t{round(life_ref * 10)}\t{round(damage_ref * 10)}\t{round(hit_ref * 10)}"
                      f"\t{round(life * 100)}\t{round(damage * 100)}\t{round(xp * 100)}\n")

    boss_rows = list(BOSSES)
    named = {name for name, _, _ in BOSSES}
    for u in uniques:
        if u['name'] in named:
            continue
        floor = clamp(int(u['level']), 1, 24)
        life_ref, damage_ref, _ = floor_refs[floor]
        life = clamp(int(u['maxHp']) / life_ref, *UNIQUE_LIFE_BAND)
        damage = clamp((int(u['minDamage']) + int(u['maxDamage'])) / 2 / damage_ref, *UNIQUE_DAMAGE_BAND)
        boss_rows.append((u['name'], round(life * 100), round(damage * 100)))
    boss_rows.append(('*',) + OTHER_UNIQUE)

    with open(os.path.join(OUT, 'monster_bosses.tsv'), 'w', newline='') as out:
        out.write('boss\tlifePercent\tdamagePercent\n')
        for name, life, damage in boss_rows:
            out.write(f'{name}\t{life}\t{damage}\n')

    with open(os.path.join(OUT, 'experience_bosses.tsv'), 'w', newline='') as out:
        out.write('boss\txpPercent\n')
        for name, life, _ in boss_rows:
            out.write(f'{name}\t{life}\n')


if __name__ == '__main__':
    main()
