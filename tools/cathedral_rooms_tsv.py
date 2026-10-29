"""Writes mods/poe2/txtdata/poe2/cathedral_rooms.tsv from the room collection's rooms.json.

The collection page (Cathedral Room Collection) is drawn from the same rooms.json, so the game builds exactly the
rooms Curtis picked. Usage: python3 tools/cathedral_rooms_tsv.py rooms.json [picks.json]
picks.json (optional) maps a room id to "keep", "change" or "cut"; cut rooms are left out.
"""
import json
import sys

ROLES = {'big fight': 'big', 'main way': 'main', 'side area': 'side', 'quiet room': 'quiet', 'junction': 'junction'}


def main():
    data = json.load(open(sys.argv[1]))
    picks = json.load(open(sys.argv[2])) if len(sys.argv) > 2 else {}
    out = ['id\tfamily\troles\tmemorable\tpillarsBehind\treward\toptions\tgrid']
    for r in data['rooms']:
        if picks.get(r['id']) == 'cut':
            continue
        width = max(len(row) for row in r['grid'])
        grid = '/'.join(row.ljust(width).replace(' ', '_') for row in r['grid'])
        options = ';'.join(digit + on + off for digit, (on, off, _label) in sorted(r['opts'].items()))
        roles = ','.join(ROLES[role] for role in r['roles'])
        out.append('\t'.join([r['id'], r['family'], roles, '1' if r.get('memorable') else '0',
                              '1' if r.get('pillarsBehind') else '0', (r.get('base') or '').replace(' ', ''), options, grid]))
    path = 'mods/poe2/txtdata/poe2/cathedral_rooms.tsv'
    with open(path, 'w', newline='\r\n') as f:
        f.write('\n'.join(out) + '\n')
    print(path, len(out) - 1, 'rooms')


if __name__ == '__main__':
    main()
