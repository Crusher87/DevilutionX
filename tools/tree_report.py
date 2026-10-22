#!/usr/bin/env python3
"""Builds the passive tree report page from POE2 Mashup's tree logs.

The game keeps two files in poe2_logs that are never pruned (Source/poe2/tree_telemetry.cpp):
  tree_history.tsv  one line for every node taken or refunded and every reset, for every hero and run
  tree_builds.tsv   the whole tree at game start, each level-up and game end
Each session's event file (session_*.events, in poe2_logs and poe2_logs/old) also has the same as "tree_take",
"tree_refund", "tree_clear" and "tree" events, timed against the fight records, so the report can line up the fights
before a notable was taken with the fights after.

The page opens with questions for the player about why they took what they took (Curtis asked for these on every
review of the logs), each with a picture from tree_pictures.py marking the nodes it is about. Then it answers, in order: what each hero took and when, which notables are taken by every hero of a class and which
by none, what gets refunded, what each notable seemed to change in the fights after it, how far the Profane Roads
get walked, what goes into the weapon sets, and what the tree adds up to at each level per class.

Usage:
  python3 tools/tree_report.py [log folder] [--tree passive_tree.tsv] [--hero NAME] [--since YYYY-MM-DD] [--out FILE]

The folder defaults to poe2_logs beside this script's folder. --tree is the tree file, for the notables nobody took;
it defaults to mods/poe2/txtdata/poe2/passive_tree.tsv beside this script's folder when that exists. The page goes to
tree_report.html in the log folder unless --out says otherwise.
"""
import argparse
import collections
import csv
import os
import statistics
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from run_report import esc, num, page, read_events, table  # noqa: E402
import tree_audit  # noqa: E402
import tree_pictures  # noqa: E402

NOTABLE_KINDS = {'Notable', 'Keystone', 'Shrine', 'AscendancyNotable', 'Ascendancy', 'JewelSocket'}
ROADS = ('Leoric', 'Bartuc', 'Sickness', 'Maiden')
# Fights compared on each side of a take; fewer than MIN_FIGHTS on a side and the change isn't shown.
FIGHTS_EACH_SIDE = 10
MIN_FIGHTS = 3
# Flags: taken by at least this share of a class's heroes, refunded at least this share of the times taken.
POPULAR_SHARE = 0.75
REGRET_SHARE = 0.5
# Stats shown in the power curve, with their labels.
CURVE_STATS = [('LifePercent', 'Life %'), ('Damage', 'Damage %'), ('ArmorPercent', 'Armour %'), ('EvasionPercent', 'Evasion %'),
               ('EnergyShieldPercent', 'ES %'), ('AllResist', 'All res'), ('AttackSpeed', 'Atk speed'), ('CastSpeed', 'Cast speed'),
               ('Strength', 'Str'), ('Dexterity', 'Dex'), ('Magic', 'Mag'), ('Vitality', 'Vit')]


def read_tsv(path):
    if not os.path.exists(path):
        return []
    with open(path, encoding='utf-8', errors='replace', newline='') as f:
        return list(csv.DictReader(f, delimiter='\t', quoting=csv.QUOTE_NONE))


def read_tree(path):
    """The tree file's rows by id (as text) and its nodes as tree_audit.py reads them, or None when there's no file."""
    if not path or not os.path.exists(path):
        return None
    return {'rows': {row['id']: row for row in read_tsv(path) if row.get('id', '').isdigit()}, 'nodes': tree_audit.load(path)}


def session_files(folder):
    names = []
    for sub in ('', 'old'):
        where = os.path.join(folder, sub)
        if os.path.isdir(where):
            names += [os.path.join(where, n) for n in os.listdir(where) if n.startswith('session_') and n.endswith('.events')]
    return sorted(names, key=os.path.basename)


def is_notable(kind):
    return kind in NOTABLE_KINDS


class TreeLog:
    def __init__(self, folder, hero=None, since=None):
        keep = (lambda row_hero: not hero or row_hero == hero)
        stamp = since or ''
        self.history = [r for r in read_tsv(os.path.join(folder, 'tree_history.tsv')) if keep(r.get('hero')) and r.get('date', '') >= stamp]
        self.builds = [r for r in read_tsv(os.path.join(folder, 'tree_builds.tsv')) if keep(r.get('hero')) and r.get('date', '') >= stamp]
        self.snapshots = []     # "tree" events
        self.changes = []       # (take or refund event, fights before, fights after)
        self.deaths = []
        since_name = 'session_' + stamp.replace('-', '')
        for path in session_files(folder):
            if os.path.basename(path) < since_name:
                continue
            self.read_session(path, hero)

    def read_session(self, path, hero):
        current = None
        fights = []
        takes = []
        for e in read_events(path):
            k = e.get('k')
            if k == 'game_start':
                current = e.get('hero')
            if hero and current != hero:
                continue
            e['hero'] = current
            if k == 'fight':
                fights.append(e)
            elif k in ('tree_take', 'tree_refund'):
                takes.append((e, len(fights)))
            elif k == 'tree':
                self.snapshots.append(e)
            elif k == 'death':
                self.deaths.append(e)
        for e, at in takes:
            self.changes.append((e, fights[max(0, at - FIGHTS_EACH_SIDE):at], fights[at:at + FIGHTS_EACH_SIDE]))

    def heroes(self):
        """Each hero's class and highest level seen, from the history and the builds."""
        seen = {}
        for r in self.history + self.builds:
            name = r.get('hero', '')
            level = int(r.get('hero level') or 0)
            cls, best = seen.get(name, (r.get('class', ''), 0))
            seen[name] = (r.get('class', '') or cls, max(best, level))
        return seen


def latest_tree_hash(log):
    rows = [r for r in log.history + log.builds if r.get('tree')]
    return max(rows, key=lambda r: r.get('date', ''))['tree'] if rows else ''


def node_stats(log, tree_hash):
    """Per node (on the current tree): who took it, at what level, refunds, and who still holds it."""
    stats = collections.defaultdict(lambda: {'name': '', 'kind': '', 'area': '', 'road': '', 'step': '', 'heroes': set(), 'levels': [],
                                             'takes': 0, 'refunds': 0, 'pools': collections.Counter()})
    for r in log.history:
        if r.get('tree') != tree_hash or r.get('action') not in ('took', 'refunded'):
            continue
        s = stats[r['node']]
        s.update(name=r.get('name', ''), kind=r.get('kind', ''), area=r.get('area', ''), road=r.get('road', ''), step=r.get('road step', ''))
        if r['action'] == 'took':
            s['takes'] += 1
            s['heroes'].add(r.get('hero'))
            s['levels'].append(int(r.get('hero level') or 0))
            s['pools'][r.get('pool', 'main')] += 1
        else:
            s['refunds'] += 1
    return stats


def held_at_end(log, tree_hash):
    """Each hero's latest build on the current tree: the node ids held in each pool."""
    latest = {}
    for b in log.builds:
        if b.get('tree') == tree_hash:
            latest[b.get('hero')] = b
    return {hero: {pool: set((b.get(col) or '').split()) for pool, col in (('main', 'main'), ('set1', 'set I'), ('set2', 'set II'))}
            for hero, b in latest.items()}


def fight_numbers(fights):
    """Seconds per kill, lowest life and pressure: the medians over a few fights."""
    kills = sum(f.get('kills', 0) for f in fights)
    seconds = sum(f.get('seconds', 0) for f in fights)
    return {
        'secPerKill': seconds / kills if kills else None,
        'lowestLife': statistics.median(f.get('lowestLife', 100) for f in fights) if fights else None,
        'pressure': statistics.median(f.get('pressure', 0) for f in fights) if fights else None,
        'lifeLost': statistics.median(f.get('lifeLost', 0) for f in fights) if fights else None,
    }


def fight_changes(log):
    """Per notable: the median change in the fight numbers from the fights before each take to the fights after."""
    out = collections.defaultdict(lambda: collections.defaultdict(list))
    for e, before, after in log.changes:
        if e.get('k') != 'tree_take' or not is_notable(e.get('kind', '')) and not e.get('road'):
            continue
        if len(before) < MIN_FIGHTS or len(after) < MIN_FIGHTS:
            continue
        b, a = fight_numbers(before), fight_numbers(after)
        changes = out[e.get('name', '')]
        changes['n'].append(1)
        for key in ('secPerKill', 'lowestLife', 'pressure', 'lifeLost'):
            if b[key] is not None and a[key] is not None:
                changes[key].append(a[key] - b[key])
    return {name: {k: (statistics.median(v) if k != 'n' else len(v)) for k, v in c.items()} for name, c in out.items()}


def tree_power(tree_rows):
    """Notable scores from tree_audit.py, by node id as text; empty without a tree file."""
    if tree_rows is None:
        return {}
    return {str(i): v for i, v in tree_audit.notable_power(tree_rows['nodes']).items()}


def score_text(score):
    if score is None:
        return ''
    return 'rules only' if score == 0 else f"{score:.2f}"


def verdict(score, popular, regret, change):
    faster = (change.get('secPerKill') or 0) < 0 and (change.get('lowestLife') or 0) >= 0
    if (score is not None and score >= tree_audit.STRONG_SCORE) or (popular and faster):
        return 'too strong?'
    if (score is not None and 0 < score <= tree_audit.WEAK_SCORE) or regret:
        return 'too weak?'
    return ''


def feel_section(log):
    """What the history says about how the tree feels to walk: the wait for the first notable, the gaps between
    notables, points spent travelling, quick second thoughts and points left unspent."""
    rows = []
    for name in sorted(log.heroes()):
        steps = [r for r in log.history if r.get('hero') == name and r.get('action') in ('took', 'refunded')]
        taken = [r for r in steps if r['action'] == 'took' and r.get('pool') == 'main']
        if not taken:
            continue
        notable_at = [i for i, r in enumerate(taken) if is_notable(r.get('kind', ''))]
        first = notable_at[0] + 1 if notable_at else None
        gaps = [b - a for a, b in zip(notable_at, notable_at[1:])]
        travel = sum(1 for r in taken if r.get('kind') in ('Small', 'Attribute'))
        # A refund within three steps of taking the same node reads as a second thought (or a misclick).
        quick = 0
        for i, r in enumerate(steps):
            if r['action'] == 'refunded' and any(p['action'] == 'took' and p.get('node') == r.get('node') for p in steps[max(0, i - 3):i]):
                quick += 1
        unspent = [int(b.get('points total') or 0) - int(b.get('points spent') or 0) for b in log.builds if b.get('hero') == name and b.get('when') == 'levelup']
        rows.append([esc(name), len(taken), first or '', num(statistics.median(gaps), 1) if gaps else '', max(gaps) if gaps else '',
                     f"{travel * 100 // len(taken)}%", quick, num(statistics.median(unspent), 0) if unspent else '', max(unspent) if unspent else ''])
    body = table(['Hero', 'Points taken', 'Points to first notable', 'Median points between notables', 'Longest gap', 'On small and attribute nodes',
                  'Quick refunds', 'Unspent at level-up (median)', 'Most unspent'], rows, numeric_from=1)
    return ('<h2>How the tree feels to walk</h2><p>The numbers can\'t say whether the tree feels good, but they show where it may not: a long '
            'wait for the first notable, long gaps between notables, most points spent travelling, quick refunds (second thoughts or misclicks), '
            'and points left unspent because nothing near looked worth it. Main tree points only.</p>' + (body if rows else '<p>No history yet.</p>'))


# Questions for Curtis about his choices: at most this many per hero, and the unspent points that make one worth asking.
QUESTIONS_PER_HERO = 8
UNSPENT_WORTH_ASKING = 3
TRAVEL_WORTH_ASKING = 6


def questions(log, tree, power):
    """Questions about why a hero took what they took, each with a picture of the nodes it's about.
    Returns (hero, question, svg) in the order they happened."""
    if tree is None:
        return []
    nodes = tree['nodes']
    asked = []
    for hero in sorted(log.heroes()):
        steps = [r for r in log.history if r.get('hero') == hero and r.get('node', '').isdigit()]
        builds = [b for b in log.builds if b.get('hero') == hero]
        held_now = {int(i) for i in (builds[-1].get('main') or '').split() if i.isdigit()} if builds else set()
        held_now &= set(nodes)
        mine = []
        held = set()
        since_notable = []
        first_notable = True
        for i, r in enumerate(steps):
            node = int(r['node'])
            if node not in nodes or r.get('pool') != 'main':
                continue
            n = nodes[node]
            level = r.get('hero level', '?')
            if r['action'] == 'took':
                held.add(node)
                if n['road'] and n['step'] == 1:
                    mine.append((f"Level {level}: you stepped onto {n['road']}'s Profane Road. What pulled you onto it, and did you mean to walk "
                                 f"it to the toll or the pact?", tree_pictures.svg(nodes, held, [node], [], title=f"{hero}: onto {n['road']}'s road")))
                if n['kind'] in ('Notable', 'Keystone', 'Shrine'):
                    if first_notable:
                        mine.append((f"Level {level}: your first notable was {n['name']}, after {len(since_notable)} other points. Why that one first?",
                                     tree_pictures.svg(nodes, held - {node}, [node], since_notable, title=f"{hero}: first notable")))
                        first_notable = False
                    elif len(since_notable) >= TRAVEL_WORTH_ASKING:
                        mine.append((f"Level {level}: you spent {len(since_notable)} points getting to {n['name']}. Was it the goal all along, "
                                     f"or did nothing nearer look worth taking?", tree_pictures.svg(nodes, held - {node}, [node], since_notable,
                                                                                                    title=f"{hero}: the walk to {n['name']}")))
                    since_notable = []
                else:
                    since_notable.append(node)
            elif r['action'] == 'refunded':
                held.discard(node)
                took = next((p for p in reversed(steps[:i]) if p.get('node') == r['node'] and p['action'] == 'took'), None)
                quick = took is not None and steps.index(took) >= i - 3
                name = n['name'] or f"the small node {tree_audit.label(n)}"
                if n['kind'] in ('Notable', 'Keystone', 'Shrine') or quick:
                    when = f"at level {took.get('hero level')}" if took else 'earlier'
                    ask = 'Was that a misclick or a second thought?' if quick else 'What made you give it back?'
                    mine.append((f"Level {level}: you refunded {name}, taken {when}. {ask}",
                                 tree_pictures.svg(nodes, held, [node], [], refunded=[node], title=f"{hero}: refunded {name}")))
        # Strong notables right next to the build that were never taken.
        near = {j for i in held_now for j in nodes[i]['links']} - held_now
        passed = sorted((power.get(str(j), 0), j) for j in near if nodes[j]['kind'] == 'Notable')
        if passed and passed[-1][0] >= 1.0:
            score, j = passed[-1]
            mine.append((f"You hold the node right next to {nodes[j]['name']} (score {score:.2f}) but never took it. Did you look at it, "
                         f"and what put you off?", tree_pictures.svg(nodes, held_now, [], [], skipped=[j], title=f"{hero}: passed by {nodes[j]['name']}")))
        unspent = [(int(b.get('points total') or 0) - int(b.get('points spent') or 0), b) for b in builds if b.get('when') == 'levelup']
        if unspent and max(u for u, _ in unspent) >= UNSPENT_WORTH_ASKING:
            most, b = max(unspent, key=lambda ub: ub[0])
            mine.append((f"At level {b.get('hero level')} you had {most} passive points unspent. Were you saving them for something, "
                         f"or was there nothing nearby worth taking?", tree_pictures.svg(nodes, held_now, [], [], title=f"{hero}: your tree now")))
        asked += [(hero, q, picture) for q, picture in mine[:QUESTIONS_PER_HERO]]
    return asked


def signed(value, digits=1):
    if value is None:
        return ''
    return f"{value:+.{digits}f}"


def report(log, tree):
    parts = []
    heroes = log.heroes()
    tree_hash = latest_tree_hash(log)
    takes = [r for r in log.history if r.get('action') == 'took']
    refunds = [r for r in log.history if r.get('action') == 'refunded']
    clears = [r for r in log.history if (r.get('action') or '').startswith('cleared')]
    sessions = {r.get('session') for r in log.history + log.builds}

    asked = questions(log, tree, tree_power(tree))
    if asked:
        parts.append('<h2>Questions for you</h2><p>About why you took what you took. In each picture the node the question is about has a '
                     'magenta ring, the points spent getting there a cyan line, a refunded node a red cross, a notable you passed by a green '
                     'dashed ring, and the nodes you hold are gold.</p>')
        for number, (hero, question, picture) in enumerate(asked, 1):
            parts.append(f'<div class="q"><p><b>Q{number} ({esc(hero)})</b>: {esc(question)}</p>{picture}</div>')

    parts.append('<h2>Summary</h2>')
    parts.append(table(['', 'Value'], [
        ['Heroes tracked', len(heroes)], ['Sessions', len(sessions)], ['Nodes taken', len(takes)], ['Refunds', len(refunds)],
        ['Resets and tree-update refunds', len(clears)], ['Tree version (latest)', esc(tree_hash or 'none yet')]]))
    if not log.history:
        parts.append('<p>No tree history yet: it starts with the first game played on a build with the tree tracker.</p>')

    # Heroes, then each hero's timeline of notables.
    parts.append('<h2>Heroes</h2>')
    end = held_at_end(log, tree_hash)
    rows = []
    for name, (cls, level) in sorted(heroes.items(), key=lambda kv: (kv[1][0], kv[0])):
        mine = [r for r in takes if r.get('hero') == name]
        held = end.get(name, {})
        rows.append([esc(name), esc(cls), level, len(mine), sum(1 for r in refunds if r.get('hero') == name), len(held.get('main', ())),
                     len(held.get('set1', ())), len(held.get('set2', ()))])
    parts.append(table(['Hero', 'Class', 'Level', 'Taken', 'Refunded', 'Held now', 'Set I', 'Set II'], rows, numeric_from=2))
    for name in sorted(heroes):
        steps = [r for r in log.history if r.get('hero') == name and (is_notable(r.get('kind', '')) or r.get('road') or (r.get('action') or '').startswith('cleared'))]
        if not steps:
            continue
        items = []
        for r in steps:
            action = r.get('action', '')
            if action.startswith('cleared'):
                items.append(f"<li class=\"bad\">Level {esc(r.get('hero level'))}: {esc(action)} ({esc(r.get('node'))} nodes)</li>")
                continue
            where = f"{r.get('road')} road step {r.get('road step')}" if r.get('road') else r.get('area', '')
            pool = '' if r.get('pool') == 'main' else f", {'set I' if r.get('pool') == 'set1' else 'set II'}"
            mark = '<s>' if action == 'refunded' else '<b>'
            close = '</s>' if action == 'refunded' else '</b>'
            items.append(f"<li>Level {esc(r.get('hero level'))}: {esc(action)} {mark}{esc(r.get('name'))}{close} ({esc(where)}{esc(pool)}), "
                         f"{esc(r.get('points spent'))}/{esc(r.get('points total'))} points</li>")
        parts.append(f"<details><summary>{esc(name)}: {len(steps)} notable steps</summary><ul>{''.join(items)}</ul></details>")

    # Notables: how often each is taken, by how many of its class's heroes, refunds, and the fights after.
    stats = node_stats(log, tree_hash)
    changes = fight_changes(log)
    class_heroes = collections.Counter(cls for cls, _ in heroes.values())
    parts.append('<h2>Notables taken</h2>')
    parts.append('<p>Score: the notable\'s stat lines against a typical notable\'s (1.00), from tree_audit.py; rules score nothing. '
                 'The verdict weighs the score with play: too strong? when it scores high, or every hero takes it and fights go better after; '
                 'too weak? when it scores low or gets refunded. Popular: taken by at least three in four heroes of a class. Regret: refunded at least half as often as taken. '
                 'The fight columns compare the ten fights before each take with the ten after, as a median over takes; '
                 'gear and levels change too, so treat them as hints. Faster kills show as a negative change in seconds per kill.</p>')
    power = tree_power(tree)
    rows = []
    for node, s in sorted(stats.items(), key=lambda kv: (-len(kv[1]['heroes']), kv[1]['name'])):
        if not is_notable(s['kind']) and not s['road']:
            continue
        by_class = collections.Counter(heroes.get(h, ('', 0))[0] for h in s['heroes'])
        flags = []
        for cls, n in by_class.items():
            if class_heroes[cls] >= 2 and n / class_heroes[cls] >= POPULAR_SHARE:
                flags.append(f'popular with {cls}')
        if s['takes'] and s['refunds'] / s['takes'] >= REGRET_SHARE:
            flags.append('regret')
        c = changes.get(s['name'], {})
        score = power.get(node)
        flags.insert(0, verdict(score, bool(by_class) and any(f.startswith('popular') for f in flags), 'regret' in flags, c))
        rows.append([esc(s['name']), esc(s['kind']), esc(f"{s['road']} {s['step']}" if s['road'] else s['area']), score_text(score),
                     len(s['heroes']), s['takes'], s['refunds'],
                     num(statistics.median(s['levels']), 0) if s['levels'] else '', c.get('n', ''), signed(c.get('secPerKill'), 2),
                     signed(c.get('lowestLife'), 0), signed(c.get('pressure'), 0), esc(', '.join(f for f in flags if f))])
    parts.append(table(['Notable', 'Kind', 'Area', 'Score', 'Heroes', 'Taken', 'Refunded', 'Median level', 'Takes with fights', 's per kill', 'Lowest life',
                        'Pressure', 'Verdict and flags'], rows, numeric_from=3))

    parts.append(feel_section(log))

    # Never taken: every notable on the tree file that no hero took on this tree.
    parts.append('<h2>Never taken</h2>')
    if tree is None:
        parts.append('<p>No tree file given (--tree), so the notables nobody took can\'t be listed.</p>')
    else:
        taken_ids = {node for node, s in stats.items() if s['takes']}
        never = collections.defaultdict(list)
        for node_id, row in tree['rows'].items():
            if row.get('kind') in ('Notable', 'Keystone', 'Shrine') and node_id not in taken_ids:
                score = power.get(node_id)
                never[row.get('region', '')].append(row.get('name', '') + (f" ({score_text(score)})" if score is not None else ''))
        total = sum(1 for row in tree['rows'].values() if row.get('kind') in ('Notable', 'Keystone', 'Shrine'))
        missing = sum(len(v) for v in never.values())
        parts.append(f'<p>{missing} of {total} notables and shrines have never been taken on this tree (scores in brackets).</p>')
        parts.append(table(['Area', 'Count', 'Notables'], [[esc(area), len(names), esc(', '.join(sorted(names)))] for area, names in sorted(never.items())], numeric_from=1))

    # Profane Roads: how far each hero walked, from the latest build, and deaths in the sessions while on a road.
    parts.append('<h2>Profane Roads</h2>')
    road_steps = collections.defaultdict(dict)
    for s in log.snapshots:
        for road, step in (s.get('roads') or {}).items():
            road_steps[road][s.get('hero')] = max(road_steps[road].get(s.get('hero'), 0), step)
    rows = []
    for road in ROADS:
        walked = road_steps.get(road, {})
        steps = sorted(walked.values())
        road_refunds = sum(1 for r in refunds if r.get('road') == road)
        rows.append([esc(road), len(walked), sum(1 for v in steps if v >= 3), sum(1 for v in steps if v >= 6), sum(1 for v in steps if v >= 8),
                     road_refunds, esc(', '.join(f"{h} {v}" for h, v in sorted(walked.items())))])
    parts.append(table(['Road', 'Heroes on it', 'Reached the toll (3)', 'Reached the pact (6)', 'Walked through (8)', 'Refunds', 'Deepest step per hero'], rows))

    # Weapon sets: what goes into the sets' points.
    parts.append('<h2>Weapon sets</h2>')
    set_rows = [(s['name'], s['pools']['set1'], s['pools']['set2']) for s in stats.values() if s['pools']['set1'] or s['pools']['set2']]
    if set_rows:
        parts.append(table(['Node', 'Set I takes', 'Set II takes'], [[esc(n), a, b] for n, a, b in sorted(set_rows, key=lambda r: -(r[1] + r[2]))[:25]]))
    else:
        parts.append('<p>No weapon set points spent yet.</p>')

    # Power curve: what the tree adds up to, by class and level band, from the snapshots.
    parts.append('<h2>What the tree adds up to</h2>')
    parts.append('<p>Medians of the tree\'s totals in each level band, from the snapshots at game start, level-ups and game end.</p>')
    bands = collections.defaultdict(list)
    for s in log.snapshots:
        level = s.get('heroLevel', 0)
        band = f"{(level - 1) // 10 * 10 + 1}-{(level - 1) // 10 * 10 + 10}" if level > 0 else '0'
        bands[(s.get('class', ''), (level - 1) // 10, band)].append(s)
    rows = []
    for (cls, _, band), snaps in sorted(bands.items()):
        row = [esc(cls), band, len(snaps), num(statistics.median(s.get('spent', 0) for s in snaps), 0)]
        for stat, _label in CURVE_STATS:
            values = [(s.get('totals') or {}).get(stat, 0) for s in snaps]
            row.append(num(statistics.median(values), 0) if any(values) else '')
        rows.append(row)
    parts.append(table(['Class', 'Levels', 'Snapshots', 'Points'] + [label for _, label in CURVE_STATS], rows, numeric_from=2))

    names = ', '.join(sorted(heroes)) or 'none yet'
    return page('Passive Tree Report', f"{len(heroes)} heroes ({names}), tree {tree_hash or 'unknown'}", ''.join(parts))


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('folder', nargs='?', default=os.path.join(os.path.dirname(here), 'poe2_logs'))
    parser.add_argument('--tree', default=os.path.join(os.path.dirname(here), 'mods', 'poe2', 'txtdata', 'poe2', 'passive_tree.tsv'))
    parser.add_argument('--hero', help='only this hero')
    parser.add_argument('--since', help='only from this date (YYYY-MM-DD)')
    parser.add_argument('--out', help='where to write the page (default tree_report.html in the log folder)')
    args = parser.parse_args()
    if not os.path.isdir(args.folder):
        sys.exit(f"No log folder at {args.folder}")
    log = TreeLog(args.folder, args.hero, args.since)
    tree = read_tree(args.tree)
    text = report(log, tree)
    out = args.out or os.path.join(args.folder, 'tree_report.html')
    with open(out, 'w', encoding='utf-8') as f:
        f.write(text)
    print(f"{len(log.heroes())} heroes, {sum(1 for r in log.history if r.get('action') == 'took')} nodes taken, "
          f"{sum(1 for r in log.history if r.get('action') == 'refunded')} refunds")
    print(f"Wrote {out}")


if __name__ == '__main__':
    main()
