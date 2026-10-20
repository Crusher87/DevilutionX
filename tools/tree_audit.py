#!/usr/bin/env python3
"""Checks the passive tree file itself, with no play needed: what everything costs, where the stats sit, which
notables stand out, and (with --old) exactly what changed since an earlier version of the tree.

Sections of the page:
  Costs        points from each class start to every notable, shrine, jewel socket and Profane Road door, counted the
               way PassiveRoute does (the target counts, the start doesn't; other starts and ascendancies are blocked;
               a road is walked only from its door to its far end).
  Areas        node counts and the sum of each stat in each area of the tree.
  Outliers     notable stat lines far above or below the median line of the same stat on other notables.
  Changes      (--old) nodes added, removed, renamed, moved between kinds, and every stat or link change.
  Power        (--power) the reference builds of tree_power_audit.py at levels 50 and 100, on this tree (and the
               old one with --old), so a balance pass shows what it did to whole builds.

Usage:
  python3 tools/tree_audit.py [passive_tree.tsv] [--old OLD.tsv] [--power] [--out FILE]

The tree defaults to mods/poe2/txtdata/poe2/passive_tree.tsv beside this script's folder; the page goes to
tree_audit.html in the current folder unless --out says otherwise. The exit code is 0; nothing here is a pass or fail.
"""
import argparse
import collections
import csv
import os
import statistics
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from run_report import esc, num, page, table  # noqa: E402

CLASSES = ('Warrior', 'Rogue', 'Sorcerer', 'Monk')
BLOCKED = {'Ascendancy', 'AscendancyNotable'}
TARGET_KINDS = ('Notable', 'Shrine', 'JewelSocket')
# A notable's stat line is an outlier at this many times the median line of that stat, or below one over it.
OUTLIER_RATIO = 2.0
# Stats seen on fewer notables than this aren't judged.
OUTLIER_MIN_SAMPLES = 5
# Notable scores (1.0 is a typical notable) flagged as maybe too strong or too weak.
STRONG_SCORE = 1.6
WEAK_SCORE = 0.6


def load(path):
    nodes = {}
    with open(path, encoding='utf-8', newline='') as f:
        for row in csv.DictReader(f, delimiter='\t', quoting=csv.QUOTE_NONE):
            row = {k: (v or '').strip() for k, v in row.items()}
            if not row.get('id', '').isdigit():
                continue
            node = {
                'id': int(row['id']), 'name': row.get('name', ''), 'kind': row.get('kind', ''), 'class': row.get('class', ''),
                'region': row.get('region', ''), 'group': row.get('group', ''),
                'stats': [], 'links': set(), 'road': None, 'step': 0,
                'x': int(row.get('x') or 0), 'y': int(row.get('y') or 0),
            }
            for entry in filter(None, row.get('stats', '').split(',')):
                stat, _, value = entry.partition('=')
                try:
                    node['stats'].append((stat, int(value)))
                except ValueError:
                    pass
            node['links'] = {int(x) for x in row.get('links', '').split(',') if x.strip().isdigit()}
            if ':' in row.get('road', ''):
                road, _, step = row['road'].partition(':')
                node['road'], node['step'] = road, int(step) if step.isdigit() else 0
            nodes[node['id']] = node
    # Links go both ways, as the game makes them.
    for node in list(nodes.values()):
        for link in list(node['links']):
            if link in nodes and link != node['id']:
                nodes[link]['links'].add(node['id'])
    return nodes


def label(node):
    if node['name']:
        return node['name']
    return ', '.join(f"{s} {v:+d}" for s, v in node['stats']) or f"node {node['id']}"


def can_step(nodes, a, b):
    """The game's CanStepPassive: a road is entered at step 1 from its door and walked one way to step 8 and off."""
    na, nb = nodes[a], nodes[b]
    if nb['road'] is None and na['road'] is None:
        return True
    if na['road'] is None:
        return nb['step'] == 1
    if nb['road'] is None:
        return na['step'] == 8
    return na['road'] == nb['road'] and nb['step'] == na['step'] + 1


def costs_from(nodes, start):
    """Points to take each node from a class start (the target counts, the start doesn't)."""
    dist = {start: 0}
    queue = collections.deque([start])
    while queue:
        a = queue.popleft()
        for b in nodes[a]['links']:
            if b in dist or b not in nodes:
                continue
            nb = nodes[b]
            if nb['kind'] in BLOCKED or (nb['kind'] == 'Start' and b != start) or not can_step(nodes, a, b):
                continue
            dist[b] = dist[a] + 1
            queue.append(b)
    return dist


def starts(nodes):
    return {n['class'] or n['name']: n['id'] for n in nodes.values() if n['kind'] == 'Start'}


def costs_section(nodes):
    start_ids = starts(nodes)
    dists = {cls: costs_from(nodes, start_ids[cls]) for cls in CLASSES if cls in start_ids}
    parts = ['<h2>Costs from each start</h2>',
             '<p>Points to take each notable, shrine, jewel socket and Profane Road door, counting the target. The nearest class is bold.</p>']
    rows = []
    targets = [n for n in nodes.values() if n['kind'] in TARGET_KINDS or (n['road'] and n['step'] == 1)]
    for n in sorted(targets, key=lambda n: (n['region'], n['kind'], label(n))):
        values = [dists[c].get(n['id']) for c in dists]
        best = min((v for v in values if v is not None), default=None)
        cells = [f"<b>{v}</b>" if v is not None and v == best else ('' if v is None else str(v)) for v in values]
        what = f"{n['road']} road door" if n['road'] else n['kind']
        rows.append([esc(label(n)), esc(what), esc(n['region'])] + cells)
    parts.append(table(['Node', 'Kind', 'Area'] + list(dists), rows, numeric_from=3))
    # Per class: how far its own area's notables sit, and the nearest of each road door.
    summary = []
    for cls, dist in dists.items():
        own = [dist[n['id']] for n in nodes.values() if n['kind'] == 'Notable' and n['region'] == cls and n['id'] in dist]
        doors = {n['road']: dist.get(n['id']) for n in nodes.values() if n['road'] and n['step'] == 1}
        every = sorted(dist[n['id']] for n in nodes.values() if n['kind'] == 'Notable' and n['id'] in dist)
        summary.append([esc(cls), len(own), every[0] if every else '', sum(1 for d in every if d <= 10), sum(1 for d in every if d <= 20),
                        num(statistics.median(own), 0) if own else '', max(own) if own else '',
                        esc(', '.join(f"{r} {d}" for r, d in sorted(doors.items()) if d is not None))])
    parts.append('<p>How the start feels: points to the first notable, and how many notables are within 10 and 20 points.</p>')
    parts.append(table(['Class', 'Own notables', 'First notable', 'Within 10', 'Within 20', 'Median to own', 'Farthest own', 'Road doors (points)'], summary, numeric_from=1))
    return ''.join(parts), dists


def areas_section(nodes):
    by_area = collections.defaultdict(lambda: {'kinds': collections.Counter(), 'stats': collections.Counter()})
    for n in nodes.values():
        a = by_area[n['region'] or 'none']
        a['kinds'][n['kind']] += 1
        for stat, value in n['stats']:
            a['stats'][stat] += value
    rows = []
    for area, a in sorted(by_area.items()):
        kinds = ', '.join(f"{k} {c}" for k, c in a['kinds'].most_common())
        top = ', '.join(f"{s} {v}" for s, v in a['stats'].most_common(10))
        rows.append([esc(area), sum(a['kinds'].values()), esc(kinds), esc(top)])
    return '<h2>Areas</h2><p>Node counts and the ten biggest stat sums in each area.</p>' + table(['Area', 'Nodes', 'Kinds', 'Biggest stat sums'], rows, numeric_from=1)


def notable_power(nodes):
    """Each notable's plain stat lines against a typical notable's: the sum over its lines of value / the median line of
    that stat, divided by the median of those sums. 1.0 is a typical notable; rule stats and prices aren't counted."""
    lines = collections.defaultdict(list)
    for n in nodes.values():
        if n['kind'] == 'Notable':
            for stat, value in n['stats']:
                if value > 0:
                    lines[stat].append(value)
    medians = {s: statistics.median(v) for s, v in lines.items() if len(v) >= OUTLIER_MIN_SAMPLES}
    raw = {}
    for n in nodes.values():
        if n['kind'] == 'Notable':
            raw[n['id']] = sum(value / medians[stat] for stat, value in n['stats'] if value > 0 and stat in medians)
    typical = statistics.median(v for v in raw.values() if v > 0) if any(raw.values()) else 1
    return {i: v / typical for i, v in raw.items()}


def power_score_section(nodes, dists):
    power = notable_power(nodes)
    rows = []
    for i, score in power.items():
        n = nodes[i]
        if score >= STRONG_SCORE or (0 < score <= WEAK_SCORE):
            near = min((d[i] for d in dists.values() if i in d), default=None)
            rows.append((score, n, near))
    rows.sort(key=lambda r: -r[0])
    body = table(['Notable', 'Area', 'Score', 'Verdict', 'Points from nearest start', 'Stats'],
                 [[esc(label(n)), esc(n['region']), num(score, 2), 'too strong?' if score >= STRONG_SCORE else 'too weak?', near if near is not None else '',
                   esc(', '.join(f"{s} {v}" for s, v in n['stats']))] for score, n, near in rows], numeric_from=2)
    return (f'<h2>Notable scores</h2><p>Each notable\'s plain stat lines against a typical notable\'s (1.00). At {STRONG_SCORE:g} or more it may be too '
            f'strong, at {WEAK_SCORE:g} or less too weak, unless a rule or price on it makes up the difference: rule stats score nothing here. '
            f'{len(rows)} of {len(power)} notables flagged.</p>' + (body if rows else ''))


def outliers_section(nodes):
    lines = collections.defaultdict(list)
    for n in nodes.values():
        if n['kind'] == 'Notable':
            for stat, value in n['stats']:
                if value > 0:
                    lines[stat].append(value)
    medians = {s: statistics.median(v) for s, v in lines.items() if len(v) >= OUTLIER_MIN_SAMPLES}
    rows = []
    for n in nodes.values():
        if n['kind'] != 'Notable':
            continue
        for stat, value in n['stats']:
            m = medians.get(stat)
            if not m or value <= 0:
                continue
            ratio = value / m
            if ratio >= OUTLIER_RATIO or ratio <= 1 / OUTLIER_RATIO:
                rows.append((ratio, n, stat, value, m))
    rows.sort(key=lambda r: -abs(r[0] - 1))
    body = table(['Notable', 'Area', 'Stat', 'Value', 'Median on notables', 'Times the median'],
                 [[esc(label(n)), esc(n['region']), esc(stat), value, num(m, 1), num(ratio, 2)] for ratio, n, stat, value, m in rows], numeric_from=3)
    return (f'<h2>Outliers</h2><p>Notable stat lines at {OUTLIER_RATIO:g} times the median line of the same stat or more, or at half or less '
            f'({len(medians)} stats judged, each seen on at least {OUTLIER_MIN_SAMPLES} notables). Rule stats and Profane prices are flags, '
            'so they aren\'t judged here.</p>' + (body if rows else '<p>None.</p>'))


def changes_section(old, new):
    parts = ['<h2>Changes since the old tree</h2>']
    added = [new[i] for i in new if i not in old]
    removed = [old[i] for i in old if i not in new]
    changed = []
    for i in sorted(set(old) & set(new)):
        a, b = old[i], new[i]
        what = []
        if a['name'] != b['name']:
            what.append(f"renamed from {a['name'] or '(small)'}")
        if a['kind'] != b['kind']:
            what.append(f"{a['kind']} to {b['kind']}")
        if a['region'] != b['region']:
            what.append(f"moved from {a['region']} to {b['region']}")
        sa, sb = dict(a['stats']), dict(b['stats'])
        for stat in sorted(set(sa) | set(sb)):
            if sa.get(stat) != sb.get(stat):
                what.append(f"{stat} {sa.get(stat, 0)} to {sb.get(stat, 0)}")
        if a['links'] != b['links']:
            plus, minus = sorted(b['links'] - a['links']), sorted(a['links'] - b['links'])
            what.append('links' + (f" +{plus}" if plus else '') + (f" -{minus}" if minus else ''))
        if (a['road'], a['step']) != (b['road'], b['step']):
            what.append(f"road {a['road'] or 'none'}:{a['step']} to {b['road'] or 'none'}:{b['step']}")
        if what:
            changed.append((b, what))
    parts.append(f'<p>{len(added)} nodes added, {len(removed)} removed, {len(changed)} changed.</p>')
    if added:
        parts.append(table(['Added', 'Kind', 'Area', 'Stats'], [[esc(label(n)), esc(n['kind']), esc(n['region']), esc(', '.join(f"{s} {v}" for s, v in n['stats']))] for n in added]))
    if removed:
        parts.append(table(['Removed', 'Kind', 'Area'], [[esc(label(n)), esc(n['kind']), esc(n['region'])] for n in removed]))
    if changed:
        parts.append(table(['Node', 'Id', 'Area', 'What changed'], [[esc(label(n)), n['id'], esc(n['region']), esc('; '.join(w))] for n, w in changed], numeric_from=9))
    return ''.join(parts)


def power_section(tree_path, old_path):
    script = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'tree_power_audit.py')
    result = subprocess.run([sys.executable, script, old_path or tree_path, tree_path], capture_output=True, text=True)
    title = 'old tree (T) against this one (N)' if old_path else 'this tree (T and N are the same)'
    return f'<h2>Power of reference builds</h2><p>tree_power_audit.py, {title}.</p><pre>{esc(result.stdout.strip() or result.stderr.strip())}</pre>'


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('tree', nargs='?', default=os.path.join(os.path.dirname(here), 'mods', 'poe2', 'txtdata', 'poe2', 'passive_tree.tsv'))
    parser.add_argument('--old', help='an earlier tree file, to list what changed')
    parser.add_argument('--power', action='store_true', help='also run the reference build power audit (about 10 s, 20 with --old)')
    parser.add_argument('--out', default='tree_audit.html')
    args = parser.parse_args()
    nodes = load(args.tree)
    if not nodes:
        sys.exit(f"No nodes in {args.tree}")
    costs, dists = costs_section(nodes)
    parts = [costs, power_score_section(nodes, dists), areas_section(nodes), outliers_section(nodes)]
    if args.old:
        parts.append(changes_section(load(args.old), nodes))
    if args.power:
        parts.append(power_section(args.tree, args.old))
    kinds = collections.Counter(n['kind'] for n in nodes.values())
    subtitle = f"{os.path.basename(args.tree)}: {len(nodes)} nodes (" + ', '.join(f"{k} {c}" for k, c in kinds.most_common()) + ')'
    with open(args.out, 'w', encoding='utf-8') as f:
        f.write(page('Passive Tree Audit', subtitle, ''.join(parts)))
    print(subtitle)
    for cls, dist in dists.items():
        unreachable = sum(1 for n in nodes.values() if n['kind'] in TARGET_KINDS and n['id'] not in dist)
        print(f"{cls}: reaches {len(dist) - 1} nodes; {unreachable} notables, shrines or sockets out of reach")
    print(f"Wrote {args.out}")


if __name__ == '__main__':
    main()
