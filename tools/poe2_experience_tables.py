#!/usr/bin/env python3
"""Writes the POE2 Mashup experience tables (levels 1-100).

- mods/poe2/txtdata/Experience.tsv: the experience to leave each level. POE's table to level 85, then each level
  costs 2% more than the one before (90-100 about half of POE's). Row L is the total to reach L + 1; row 100 repeats
  row 99, the cap.
- mods/poe2/txtdata/poe2/monster_experience.tsv: the experience of a white (normal) monster by monster level. A kill
  at area level A is worth need(A) / K(A) / RARITY, K being how many kills a level takes at your own area level
  (log-linear between KNOTS). Past 63 a kill grows 1% a level to 80; rows past 80 repeat 80.
- mods/poe2/txtdata/poe2/experience_level_gap.tsv: permille of a kill's experience by character level and level gap
  (POE2's rule: full within a safe zone of 3 + level / 16, then ((L + 5) / (L + 5 + ED^2.5))^1.5, floor 1%). The game
  pays full experience for monsters of level 80 or more whatever the gap.

KNOTS were refit so level 10 comes after about 550 kills on the Act 1 floors (110 kills a floor), knots 38 and 63
unchanged. Run from the repository root: python3 tools/poe2_experience_tables.py
"""
import math
import os

# Total experience to reach each level, POE's table (index = level), as far as the tail needs it.
POE_TOTAL = [
    0, 0, 525, 1760, 3781, 7184, 12186, 19324, 29377, 43181, 61693, 85990, 117506, 157384, 207736, 269997, 346462,
    439268, 551295, 685171, 843709, 1030734, 1249629, 1504995, 1800847, 2142652, 2535122, 2984677, 3496798, 4080655,
    4742836, 5490247, 6334393, 7283446, 8348398, 9541110, 10874351, 12361842, 14018289, 15859432, 17905634, 20171471,
    22679999, 25456123, 28517857, 31897771, 35621447, 39721017, 44225461, 49176560, 54607467, 60565335, 67094245,
    74247659, 82075627, 90631041, 99984974, 110197515, 121340161, 133497202, 146749362, 161191120, 176922628,
    194049893, 212684946, 232956711, 255001620, 278952403, 304972236, 333233648, 363906163, 397194041, 433312945,
    472476370, 514937180, 560961898, 610815862, 664824416, 723298169, 786612664, 855129128, 929261318, 1009443795,
    1096169525, 1189918242, 1291270350, 1400795257, 1519130326,
]
TAIL = 1.02            # each level from 85 on costs this much more than the one before
RARITY = 1.12          # average experience of a floor's monsters over a white one (12% magic x1.5, 3% rare x3)
KNOTS = {1: 59.47252247486256, 38: 81.22022843042804, 63: 145.65288739463497}
MAPG = 1.01            # experience a kill grows per monster level past 63
TOP = 80               # base experience is flat from this monster level
MAX_LEVEL = 100
MAX_GAP = 40

NEED = [0] * (MAX_LEVEL + 1)
for level in range(1, MAX_LEVEL):
    NEED[level] = POE_TOTAL[level + 1] - POE_TOTAL[level] if level <= 85 else round(NEED[85] * TAIL ** (level - 85))
TOTAL = [0, 0]
for level in range(1, MAX_LEVEL):
    TOTAL.append(TOTAL[-1] + NEED[level])
assert TOTAL[MAX_LEVEL] < 2 ** 32


def kills_per_level(area):
    knots = sorted(KNOTS)
    if area <= knots[0]:
        return KNOTS[knots[0]]
    if area >= knots[-1]:
        return KNOTS[knots[-1]]
    for a, b in zip(knots, knots[1:]):
        if a <= area <= b:
            t = (area - a) / (b - a)
            return math.exp(math.log(KNOTS[a]) * (1 - t) + math.log(KNOTS[b]) * t)
    raise ValueError(area)


def base_experience(level):
    a = min(level, TOP)
    if a <= 63:
        return round(NEED[a] / kills_per_level(a) / RARITY)
    return round(NEED[63] / kills_per_level(63) / RARITY * MAPG ** (a - 63))


def gap_permille(level, gap):
    ed = max(0, gap - (3 + level // 16))
    if ed == 0:
        return 1000
    return max(10, round(1000 * ((level + 5) / (level + 5 + ed ** 2.5)) ** 1.5))


def write(path, header, rows):
    with open(path, 'w', newline='') as out:
        out.write('\t'.join(header) + '\n')
        for row in rows:
            out.write('\t'.join(str(value) for value in row) + '\n')


def main():
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'mods', 'poe2', 'txtdata')
    write(os.path.join(root, 'Experience.tsv'), ['Level', 'Experience'],
          [(level, TOTAL[min(level + 1, MAX_LEVEL)]) for level in range(1, MAX_LEVEL + 1)])
    write(os.path.join(root, 'poe2', 'monster_experience.tsv'), ['level', 'baseXp'],
          [(level, base_experience(level)) for level in range(1, MAX_LEVEL + 1)])
    write(os.path.join(root, 'poe2', 'experience_level_gap.tsv'), ['level'] + [f'gap{gap}' for gap in range(MAX_GAP + 1)],
          [[level] + [gap_permille(level, gap) for gap in range(MAX_GAP + 1)] for level in range(1, MAX_LEVEL + 1)])


if __name__ == '__main__':
    main()
