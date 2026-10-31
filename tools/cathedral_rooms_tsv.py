"""Writes a designed-rooms list for the new floor builder from a room collection's rooms.json.

The collection pages (Cathedral, Catacombs, Caves and Hell Room Collections) are drawn from the same rooms.json, so the
game builds exactly the rooms Curtis picked.
Usage: python3 tools/cathedral_rooms_tsv.py rooms.json [picks.json] [--catacombs | --caves | --hell]
picks.json (optional) maps a room id to "keep", "change" or "cut"; cut rooms are left out.
--catacombs writes catacombs_rooms.tsv instead of cathedral_rooms.tsv, --caves caves_rooms.tsv, --hell hell_rooms.tsv.
The Hell page draws chained bodies as 'N', impaled souls as 'I' and headless bodies as 'h'; the game reads them as the
Cathedral's 'T', 'Y' and 'D'.
"""
import json
import sys

HELL_PROPS = str.maketrans({'N': 'T', 'I': 'Y', 'h': 'D'})
ROLES = {'big fight': 'big', 'main way': 'main', 'side area': 'side', 'quiet room': 'quiet', 'junction': 'junction'}


def catacombs_grid(room):
    """The arrival room gets the stairs back to town beside the stairs up ('Z'; plain floor below level 5)."""
    grid = list(room['grid'])
    if room['id'] == 'arrival':
        for y in (2, 3):
            grid[y] = grid[y][:7] + 'ZZ' + grid[y][9:]
    return grid


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    catacombs = '--catacombs' in sys.argv
    caves = '--caves' in sys.argv
    hell = '--hell' in sys.argv
    data = json.load(open(args[0]))
    picks = json.load(open(args[1])) if len(args) > 1 else {}
    out = ['id\tfamily\troles\tmemorable\tpillarsBehind\treward\toptions\tgrid']
    for r in data['rooms']:
        if picks.get(r['id']) == 'cut':
            continue
        rows = catacombs_grid(r) if catacombs else r['grid']
        if hell:
            rows = [row.translate(HELL_PROPS) for row in rows]
        width = max(len(row) for row in rows)
        grid = '/'.join(row.ljust(width).replace(' ', '_') for row in rows)
        options = ';'.join(digit + on + off for digit, (on, off, _label) in sorted(r['opts'].items()))
        if hell:
            options = options.translate(HELL_PROPS)
        roles = ','.join(ROLES[role] for role in r['roles'])
        out.append('\t'.join([r['id'], r['family'], roles, '1' if r.get('memorable') else '0',
                              '1' if r.get('pillarsBehind') else '0', (r.get('base') or '').replace(' ', ''), options, grid]))
    name = 'catacombs' if catacombs else 'caves' if caves else 'hell' if hell else 'cathedral'
    path = 'mods/poe2/txtdata/poe2/' + name + '_rooms.tsv'
    with open(path, 'w', newline='\r\n') as f:
        f.write('\n'.join(out) + '\n')
    print(path, len(out) - 1, 'rooms')


if __name__ == '__main__':
    main()
