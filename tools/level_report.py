#!/usr/bin/env python3
"""Builds a floor report page from POE2 Mashup event files: what each dungeon floor held, room by room.

The game writes a "floor_census" event on every dungeon floor entered (rooms, doors, monsters by group and rarity,
chests, barrels, the way from stairs to stairs and a small map) and a "floor_pacing" event when the floor is left
(time in fights, quiet stretches, walking back through emptied rooms, kills per room). The floor checker
(censusfloors(n) in the console) writes a census for each floor it builds and a "floor_batch" event at the end.
See Source/poe2/level_census.cpp.

The page shows, in order: the floors you played (one line each, with a map), then the floor checker's totals per
dungeon level, so a change to how monsters are placed can be checked against the numbers before it.

Usage:
  python3 tools/level_report.py [log folder] [--since YYYY-MM-DD] [--last N] [--maps N] [--out FILE]

The folder defaults to poe2_logs beside this script's folder. The page goes to level_report.html in the log folder
unless --out says otherwise. --maps sets how many floor checker maps to draw per level (default 4).
"""
import argparse
import collections
import os
import statistics
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from run_report import CSS, esc, find_sessions, num, read_events, table  # noqa: E402

GROUP_SIZES = ['alone', '2-3', '4-6', '7-11', '12+']
RARITY_COLOURS = ['#c94a3a', '#4a7fd6', '#e0b323', '#e07a1f']  # normal, magic, rare, unique

EXTRA_CSS = """
.maps { display: grid; grid-template-columns: repeat(auto-fill, minmax(260px, 1fr)); gap: 12px; }
figure { margin: 0; background: var(--panel); border: 1px solid var(--line); border-radius: 8px; padding: 8px; }
figure svg { width: 100%; height: auto; display: block; }
figcaption { font-size: 0.85rem; color: var(--muted); margin-top: 4px; }
.key span { display: inline-block; width: 12px; height: 12px; border-radius: 2px; vertical-align: -1px; margin: 0 4px 0 10px; }
.key { font-size: 0.85rem; color: var(--muted); }
"""

ROOM_COLOURS = {
    'empty': '#9a958c',      # nothing at all
    'props': '#c8b48a',      # barrels or props only
    'single': '#e7a77c',     # one monster
    'monsters': '#d9614d',   # a group or more
    'reward': '#6aa772',     # no monsters, but a chest, shrine, sarcophagus or item
}
ROOM_LABELS = {
    'empty': 'empty', 'props': 'only barrels or props', 'single': 'one monster', 'monsters': 'monsters', 'reward': 'loot, no monsters',
}


def room_kind(room):
    reward = room.get('chests', 0) + room.get('shrines', 0) + room.get('sarc', 0) + room.get('items', 0) + room.get('gold', 0) > 0
    if room.get('mon', 0) > 1:
        return 'monsters'
    if room.get('mon', 0) == 1:
        return 'single'
    if reward:
        return 'reward'
    if room.get('barrels', 0) + room.get('other', 0) > 0:
        return 'props'
    return 'empty'


MAP_LETTERS = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ!$%&'()*,-./:;<=>?@[]^_`{|}~"


QUIET_STEPS = 8  # the same as QuietSteps in level_census.cpp


def quiet_runs(rows, monsters, x0, y0):
    """Runs of floor tiles further than QUIET_STEPS steps (8 ways, no cutting corners) from every monster."""
    h, w = len(rows), len(rows[0])

    def walkable(x, y):
        return 0 <= x < w and 0 <= y < h and rows[y][x] != '#'

    reach = {}
    queue = collections.deque()
    for spot in monsters:
        x, y = spot[0] - x0, spot[1] - y0
        if walkable(x, y) and (x, y) not in reach:
            reach[(x, y)] = 0
            queue.append((x, y))
    while queue:
        x, y = queue.popleft()
        d = reach[(x, y)]
        if d >= QUIET_STEPS:
            continue
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (1, -1), (-1, 1), (-1, -1)):
            nx, ny = x + dx, y + dy
            if not walkable(nx, ny) or (nx, ny) in reach:
                continue
            if dx and dy and not (walkable(x + dx, y) and walkable(x, y + dy)):
                continue
            reach[(nx, ny)] = d + 1
            queue.append((nx, ny))
    for y in range(h):
        x = 0
        while x < w:
            if walkable(x, y) and (x, y) not in reach:
                start = x
                while x < w and walkable(x, y) and (x, y) not in reach:
                    x += 1
                yield y, start, x
            else:
                x += 1


def floor_map(census, scale=4):
    """The floor as the game shows it (turned 45 degrees), rooms coloured by what they hold."""
    rows = census.get('map') or []
    if not rows:
        return ''
    rooms = census.get('roomList') or []
    kinds = [room_kind(r) for r in rooms]
    x0, y0 = census.get('mapX', 0), census.get('mapY', 0)
    w, h = len(rows[0]), len(rows)

    def screen(x, y):
        # Dungeon x runs down-right on screen and y down-left.
        dx, dy = x - x0, y - y0
        return (dx - dy + h) * scale, (dx + dy) * scale / 2

    width, height = (w + h) * scale, (w + h) * scale / 2
    parts = [f'<svg viewBox="0 0 {width:.0f} {height:.0f}" xmlns="http://www.w3.org/2000/svg" role="img">',
             f'<g transform="matrix({scale} {scale / 2} {-scale} {scale / 2} {h * scale} 0)">']
    # One rectangle per run of tiles in the same room, to keep the page small.
    for y, row in enumerate(rows):
        x = 0
        while x < w:
            ch = row[x]
            start = x
            while x < w and row[x] == ch:
                x += 1
            if ch == '#':
                continue
            index = MAP_LETTERS.find(ch)
            colour = ROOM_COLOURS[kinds[index]] if 0 <= index < len(kinds) else '#777'
            parts.append(f'<rect x="{start}" y="{y}" width="{x - start}" height="1" fill="{colour}"/>')
    # Floor more than QUIET_STEPS walking steps from every monster is shaded darker.
    for y, x0_, x1_ in quiet_runs(rows, census.get('monsterSpots') or [], x0, y0):
        parts.append(f'<rect x="{x0_}" y="{y}" width="{x1_ - x0_}" height="1" fill="#1d1c1a" opacity="0.45"/>')
    # Room edges: a thin dark line where two rooms meet.
    for y, row in enumerate(rows):
        for x, ch in enumerate(row):
            if ch == '#':
                continue
            if x + 1 < w and row[x + 1] not in ('#', ch):
                parts.append(f'<line x1="{x + 1}" y1="{y}" x2="{x + 1}" y2="{y + 1}" stroke="#2b2925" stroke-width="0.25"/>')
            if y + 1 < h and rows[y + 1][x] not in ('#', ch):
                parts.append(f'<line x1="{x}" y1="{y + 1}" x2="{x + 1}" y2="{y + 1}" stroke="#2b2925" stroke-width="0.25"/>')
    parts.append('</g>')
    route = census.get('route') or []
    if route:
        points = ' '.join(f'{sx:.1f},{sy:.1f}' for sx, sy in (screen(x + 0.5, y + 0.5) for x, y in route))
        parts.append(f'<polyline points="{points}" fill="none" stroke="#f4f1ea" stroke-width="1.5" stroke-dasharray="3 2" opacity="0.9"/>')
    for spot in census.get('objectSpots') or []:
        x, y, kind = spot
        if kind in ('c', 's', 'x'):
            sx, sy = screen(x + 0.5, y + 0.5)
            colour = {'c': '#f2d16b', 's': '#8fd3ff', 'x': '#b49be0'}[kind]
            parts.append(f'<rect x="{sx - 2.5:.1f}" y="{sy - 2.5:.1f}" width="5" height="5" fill="{colour}" stroke="#1d1c1a" stroke-width="0.6"/>')
    for spot in census.get('monsterSpots') or []:
        x, y, rarity = spot[0], spot[1], spot[2]
        sx, sy = screen(x + 0.5, y + 0.5)
        r = 1.6 if rarity == 0 else 2.6
        parts.append(f'<circle cx="{sx:.1f}" cy="{sy:.1f}" r="{r}" fill="{RARITY_COLOURS[rarity]}" stroke="#1d1c1a" stroke-width="0.4"/>')
    for name in ('start', 'end'):
        if census.get(name):
            sx, sy = screen(census[name][0] + 0.5, census[name][1] + 0.5)
            parts.append(f'<circle cx="{sx:.1f}" cy="{sy:.1f}" r="4" fill="none" stroke="#f4f1ea" stroke-width="1.5"/>')
    parts.append('</svg>')
    return ''.join(parts)


def map_key():
    rooms = ''.join(f'<span style="background:{ROOM_COLOURS[k]}"></span>{esc(ROOM_LABELS[k])}' for k in ROOM_COLOURS)
    dots = ''.join(f'<span style="background:{c};border-radius:50%"></span>{n}' for c, n in zip(RARITY_COLOURS, ['normal', 'magic', 'rare', 'unique']))
    return (f'<p class="key">Rooms:{rooms}</p><p class="key">Monsters:{dots}<span style="background:#f2d16b"></span>chest'
            f'<span style="background:#8fd3ff"></span>shrine. Darker floor: more than {QUIET_STEPS} steps from any monster. '
            f'Dashed line: the shortest way from the up stairs (ring) to the down stairs.</p>')


def caption(census, pacing=None):
    text = (f"{census.get('area', '')} visit {census.get('visit', '?')}: {census.get('rooms', 0)} rooms, {census.get('emptyRooms', 0)} empty, "
            f"{census.get('propsOnlyRooms', 0)} only props, {census.get('singleMonsterRooms', 0)} one monster; {census.get('monsters', 0)} monsters, "
            f"{census.get('quietFloorShare', 0)}% of the floor far from monsters, longest empty stretch {census.get('longestEmptyWalk', 0)} tiles")
    if pacing:
        text += f"; {num(pacing.get('seconds', 0) / 60)} min, longest quiet {num(pacing.get('longestQuiet', 0), 0)} s"
    return text


def read(paths):
    played = []    # (census, pacing or None)
    batch = []     # censuses written by the floor checker
    summaries = []
    for path in paths:
        open_census = None
        for e in read_events(path):
            kind = e.get('k')
            if kind == 'floor_census':
                if open_census is not None:
                    played.append((open_census, None))
                    open_census = None
                if e.get('batch', -1) >= 0:
                    batch.append(e)
                else:
                    open_census = e
            elif kind == 'floor_pacing':
                if open_census is not None:
                    played.append((open_census, e))
                    open_census = None
            elif kind == 'floor_batch':
                summaries.append(e)
        if open_census is not None:
            played.append((open_census, None))
    return played, batch, summaries


def p90(values):
    values = sorted(values)
    return values[min(len(values) - 1, len(values) * 9 // 10)] if values else 0


def batch_tables(batch, maps_per_level):
    by_level = collections.defaultdict(list)
    for c in batch:
        by_level[c.get('dungeonLevel', 0)].append(c)
    fields = [('rooms', 'Rooms'), ('emptyRooms', 'Empty'), ('propsOnlyRooms', 'Only props'), ('singleMonsterRooms', 'One monster'),
              ('emptyDeadEnds', 'Empty dead ends'), ('monsters', 'Monsters'), ('monsterTypes', 'Kinds'), ('groups', 'Groups'),
              ('magic', 'Magic'), ('rare', 'Rare'), ('chests', 'Chests'), ('barrels', 'Barrels'), ('routeLength', 'Stairs to stairs'),
              ('quietFloorShare', 'Floor far from monsters %'), ('routeEmptyShare', 'Way far from monsters %'), ('longestEmptyWalk', 'Longest empty stretch')]
    parts = []
    rows = []
    for level in sorted(by_level):
        floors = by_level[level]
        for name, label in fields:
            values = [c.get(name, 0) for c in floors]
            rows.append([f'L{level}', esc(label), num(statistics.mean(values)), num(statistics.median(values)), p90(values), max(values)])
        sizes = [0] * len(GROUP_SIZES)
        for c in floors:
            for i, n in enumerate(c.get('groupsBySize') or []):
                if i < len(sizes):
                    sizes[i] += n
        rows.append([f'L{level}', 'Groups by size (per floor)', ' / '.join(f'{s}: {num(n / len(floors))}' for s, n in zip(GROUP_SIZES, sizes)), '', '', ''])
    parts.append(f'<p>{len(batch)} floors built by the floor checker.</p>')
    parts.append(table(['Level', '', 'Average', 'Middle', '90% at most', 'Worst'], rows, numeric_from=2))
    for level in sorted(by_level):
        floors = by_level[level][:maps_per_level]
        if not floors:
            continue
        parts.append(f'<h3>L{level}, the first {len(floors)} floors</h3><div class="maps">')
        parts.extend(f'<figure>{floor_map(c)}<figcaption>{esc(caption(c))}</figcaption></figure>' for c in floors)
        parts.append('</div>')
    return ''.join(parts)


def report(played, batch, summaries, maps_per_level):
    parts = [map_key()]
    parts.append('<h2>Floors you played</h2>')
    if played:
        rows = []
        for census, pacing in played:
            seconds = pacing.get('seconds', 0) if pacing else 0
            quiet = pacing.get('quietSeconds', 0) if pacing else 0
            active = pacing.get('activeSeconds', 0) if pacing else 0
            rows.append([esc(census.get('area', '')), census.get('visit', ''), 'built' if census.get('built') else 'kept',
                         census.get('rooms', 0), census.get('emptyRooms', 0), census.get('propsOnlyRooms', 0), census.get('singleMonsterRooms', 0),
                         census.get('emptyDeadEnds', 0), census.get('monsters', 0), (census.get('groupsBySize') or [0, 0, 0])[2],
                         census.get('rare', 0), census.get('longestEmptyWalk', 0),
                         num(seconds / 60) if pacing else '', num(100 * quiet / active, 0) if active else '',
                         num(pacing.get('longestQuiet', 0), 0) if pacing else '', num(pacing.get('walkBackSeconds', 0), 0) if pacing else '',
                         f"{pacing.get('roomsVisited', 0)}/{pacing.get('rooms', 0)}" if pacing else ''])
        parts.append(table(['Floor', 'Visit', '', 'Rooms', 'Empty', 'Only props', 'One monster', 'Empty dead ends', 'Monsters', 'Groups of 4-6', 'Rare',
                            'Longest empty stretch', 'Minutes', 'Quiet %', 'Longest quiet s', 'Walking back s', 'Rooms seen'], rows))
        parts.append('<div class="maps">')
        parts.extend(f'<figure>{floor_map(c)}<figcaption>{esc(caption(c, p))}</figcaption></figure>' for c, p in played)
        parts.append('</div>')
    else:
        parts.append('<p>No dungeon floors in these sessions (the room log came in with the room logging build).</p>')
    parts.append('<h2>Floor checker</h2>')
    if batch:
        parts.append(batch_tables(batch, maps_per_level))
    else:
        parts.append('<p>No floor checker runs yet. Stand on a floor and type <code>censusfloors(40)</code> in the console.</p>')
    return parts


def page(body):
    title = 'Floor report'
    sub = 'What each dungeon floor held, room by room, and how the time on it was spent.'
    return (f'<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">'
            f'<title>{title}</title><style>{CSS}{EXTRA_CSS}</style></head><body><main><h1>{title}</h1><p class="sub">{sub}</p>{"".join(body)}</main></body></html>')


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('folder', nargs='?', default=os.path.join(os.path.dirname(here), 'poe2_logs'))
    parser.add_argument('--since', help='only sessions from this date (YYYY-MM-DD)')
    parser.add_argument('--last', type=int, help='only the last N sessions')
    parser.add_argument('--maps', type=int, default=4, help='floor checker maps to draw per level')
    parser.add_argument('--out', help='where to write the page (default level_report.html in the log folder)')
    args = parser.parse_args()
    if not os.path.isdir(args.folder):
        sys.exit(f"No log folder at {args.folder}")
    paths = find_sessions(args.folder, since=args.since, last=args.last)
    if not paths:
        sys.exit("No session event files found.")
    played, batch, summaries = read(paths)
    out = args.out or os.path.join(args.folder, 'level_report.html')
    with open(out, 'w', encoding='utf-8') as f:
        f.write(page(report(played, batch, summaries, args.maps)))
    print(f"{len(played)} floors played, {len(batch)} floor checker floors")
    print(f"Wrote {out}")


if __name__ == '__main__':
    main()
