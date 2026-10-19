#!/usr/bin/env python3
"""Sums up POE2 Mashup play logs (poe2_logs/session_*.log) into a balance report.

The game writes a play log for every session (Source/poe2/telemetry.cpp). This reads them and answers the balance
questions in one place: how fast monsters die and by what, how hard they hit back, how the hero keeps up with the
monster levels, how quickly levels and loot come, where mana and flasks run short, and how smoothly the game runs.

Usage:
  python3 tools/balance_report.py [log folder] [--last N] [--since YYYY-MM-DD] [--hero NAME] [--out FILE]

The folder defaults to poe2_logs beside this script's folder (the game folder's tools/ and poe2_logs/).
"""
import argparse
import collections
import os
import re
import statistics
import sys

LINE = re.compile(r'^(\d+):(\d\d):(\d\d)\.(\d+) (\S+) \[([a-z ]+)\] (.*)$')
STARTED = re.compile(r'started (\d{4}-\d\d-\d\d) (\d\d:\d\d:\d\d)')
GAME = re.compile(r'^(loaded|new game) (.*): level (\d+) (\w+), life (\d+)/(\d+), mana (\d+)/(\d+), mode (\w+)')
KILL = re.compile(r'^(.*) \((normal|magic|rare|unique), monster level (\d+)\), killed by (.*?) ([\d.]+) s after the first hit, (\d+) overkill')
LEVEL_LEFT = re.compile(r'^left after ([\d.]+) s: (\d+) kills of (\d+) monsters \((\d+)% cleared\), (\d+) drops, (\d+) damage taken(?:, (\d+) to energy shield)?')
LEVELUP = re.compile(r'^character level (\d+), life (\d+), mana (\d+)')
TOOK = re.compile(r'^took (\d+) (\w+) damage from (.*?)(?: \((\d+) to energy shield\))?, life (-?\d+)/(\d+)')
DROP = re.compile(r'^(.*) \((normal|magic|rare|unique)(?:, item level (\d+))?\)(?: from (.*))?$')
PICKUP = re.compile(r'^(.*) \((normal|magic|rare|unique)\)')
STALL = re.compile(r'^game froze for (\d+) ms \((\d+) monsters, (\d+) missiles\)')
LULL = re.compile(r'^([\d.]+) s with no enemy near')
SKILL = re.compile(r'^(.*?), (\d+) mana, (\d+)/(\d+) left')
FLASK = re.compile(r'^(.*), (\d+) charges left, drunk at (\d+)% life and (\d+)% mana')
BUILD_LEVEL = re.compile(r'^level (\d+) (\w+), life (\d+), mana (\d+)')

STATS_PARTS = {
    'minutes': re.compile(r'last ([\d.]+) min'),
    'kills': re.compile(r'kills (\d+) \('),
    'damage_dealt': re.compile(r'damage dealt (\d+)'),
    'hits': re.compile(r'hits taken (\d+) \((\d+) big, (\d+) close calls, (\d+) damage(?:, (\d+) to energy shield, lowest life (\d+)%)?\)'),
    'swings': re.compile(r'melee swings (\d+) \((\d+) evaded, (\d+) blocked, (\d+) dodged\)'),
    'skills': re.compile(r'skills (\d+) \((\d+) failed, (\d+) mana, low on mana (\d+)% of the time\)'),
    'flasks': re.compile(r'flasks (\d+) \((\d+) wasted\), charms (\d+)'),
    'xp': re.compile(r'xp (\d+) \('),
    'gold': re.compile(r'gold (\d+) \('),
    'stalls': re.compile(r'stalls (\d+)'),
    'fps': re.compile(r'fps ([\d.]+) \((\d+) frames under 30 fps, slowest (\d+) ms\)'),
    'density': re.compile(r'density: ([\d.]+) monsters within 5 tiles on average \(peak (\d+)\), ([\d.]+) aware within 10, no enemy near (\d+)% of the time, surrounded (\d+)%, (\d+) swarms'),
}

RARITIES = ('normal', 'magic', 'rare', 'unique')


class Session:
    def __init__(self, path):
        self.path = path
        self.name = os.path.basename(path)
        self.date = ''
        self.hero = ''
        self.hero_class = ''
        self.start_level = 0
        self.end_level = 0
        self.last_seconds = 0.0
        self.events = []  # (seconds, label, category, text)
        self.crashed = False


def seconds_of(match):
    h, m, s, frac = match.group(1), match.group(2), match.group(3), match.group(4)
    return int(h) * 3600 + int(m) * 60 + int(s) + int(frac) / (10 ** len(frac))


def read_session(path):
    session = Session(path)
    with open(path, encoding='utf-8', errors='replace') as f:
        for raw in f:
            line = raw.rstrip('\r\n')
            started = STARTED.search(line)
            if started and not session.date:
                session.date = f'{started.group(1)} {started.group(2)}'
            m = LINE.match(line)
            if not m:
                continue
            t = seconds_of(m)
            label, category, text = m.group(5), m.group(6), m.group(7)
            session.last_seconds = max(session.last_seconds, t)
            session.events.append((t, label, category, text))
            if category == 'game':
                g = GAME.match(text)
                if g:
                    if not session.hero:
                        session.hero = g.group(2)
                        session.hero_class = g.group(4)
                        session.start_level = int(g.group(3))
                    session.end_level = max(session.end_level, int(g.group(3)))
            elif category == 'levelup':
                lu = LEVELUP.match(text)
                if lu:
                    session.end_level = max(session.end_level, int(lu.group(1)))
            elif category == 'crash':
                session.crashed = True
    if not session.date:
        stamp = re.search(r'(\d{4})(\d\d)(\d\d)_(\d\d)(\d\d)(\d\d)', session.name)
        if stamp:
            session.date = f'{stamp.group(1)}-{stamp.group(2)}-{stamp.group(3)} {stamp.group(4)}:{stamp.group(5)}:{stamp.group(6)}'
    return session


def area_of(label):
    """Groups level labels: town, campaign levels, set levels and maps by tier."""
    if label.startswith('map'):
        return label.split('-')[0]
    return label


def pct(part, whole):
    return 100.0 * part / whole if whole else 0.0


def median(values):
    return statistics.median(values) if values else 0.0


def p90(values):
    if not values:
        return 0.0
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int(round(0.9 * (len(ordered) - 1))))]


def level_sort_key(label):
    if label == 'town':
        return (0, 0)
    m = re.match(r'L(\d+)$', label)
    if m:
        return (1, int(m.group(1)))
    m = re.match(r'set(\d+)$', label)
    if m:
        return (2, int(m.group(1)))
    m = re.match(r'map(\d+)', label)
    if m:
        return (3, int(m.group(1)))
    return (4, 0)


def report(sessions, out):
    def w(text=''):
        out.write(text + '\n')

    # The hero's level at every moment, from game, levelup and build lines, to compare against monster levels.
    def hero_levels(session):
        level = session.start_level
        for t, label, category, text in session.events:
            if category == 'levelup':
                m = LEVELUP.match(text)
                if m:
                    level = int(m.group(1))
            elif category == 'build':
                m = BUILD_LEVEL.match(text)
                if m:
                    level = int(m.group(1))
            yield t, label, category, text, level

    w('POE2 Mashup balance report')
    w('=' * 26)
    w()
    w('Sessions')
    w('--------')
    total_minutes = 0.0
    for s in sessions:
        minutes = s.last_seconds / 60
        total_minutes += minutes
        deaths = sum(1 for e in s.events if e[2] == 'death')
        kills = sum(1 for e in s.events if e[2] == 'kill')
        flag = '  CRASHED' if s.crashed else ''
        w(f'{s.date or s.name}  {s.hero or "?"} ({s.hero_class or "?"}) level {s.start_level}->{s.end_level}, '
          f'{minutes:.0f} min, {kills} kills, {deaths} deaths{flag}')
    w(f'Total: {len(sessions)} sessions, {total_minutes / 60:.1f} hours')
    w()

    # Areas: time, kill speed, clearing and damage taken per area.
    areas = collections.OrderedDict()
    monster_gap = collections.defaultdict(list)
    for s in sessions:
        for t, label, category, text, hero_level in hero_levels(s):
            area = area_of(label)
            if category == 'level' and text.startswith('left after'):
                m = LEVEL_LEFT.match(text)
                if not m:
                    continue
                a = areas.setdefault(area, collections.Counter())
                a['visits'] += 1
                a['seconds'] += float(m.group(1))
                a['kills'] += int(m.group(2))
                a['monsters'] += int(m.group(3))
                a['drops'] += int(m.group(5))
                a['life'] += int(m.group(6))
                a['shield'] += int(m.group(7) or 0)
                a['hero_level_sum'] += hero_level
            elif category == 'kill':
                m = KILL.match(text)
                if m:
                    monster_gap[area].append(int(m.group(3)) - hero_level)
    w('Areas (dungeon time only)')
    w('-------------------------')
    w(f'{"area":<8}{"visits":>7}{"min":>7}{"kills/min":>10}{"cleared":>9}{"drops/min":>10}{"taken/min":>10}{"to ES":>7}{"hero lvl":>9}{"mon-hero":>9}')
    for area in sorted(areas, key=level_sort_key):
        if area == 'town':
            continue
        a = areas[area]
        minutes = a['seconds'] / 60 or 1e-9
        gaps = monster_gap.get(area, [])
        gap = f'{statistics.mean(gaps):+.1f}' if gaps else '-'
        w(f'{area:<8}{a["visits"]:>7}{minutes:>7.1f}{a["kills"] / minutes:>10.1f}{pct(a["kills"], a["monsters"]):>8.0f}%'
          f'{a["drops"] / minutes:>10.1f}{(a["life"] + a["shield"]) / minutes:>10.0f}{pct(a["shield"], a["life"] + a["shield"]):>6.0f}%'
          f'{a["hero_level_sum"] / max(a["visits"], 1):>9.1f}{gap:>9}')
    w('  taken/min counts life and energy shield; "to ES" is the share energy shield soaked.')
    w('  mon-hero is the average monster level minus the hero level, over kills (positive: monsters ahead).')
    w()

    # Kill speed by rarity, and what does the killing.
    ttk = collections.defaultdict(list)
    overkill = collections.defaultdict(list)
    by_skill = collections.Counter()
    by_monster = collections.Counter()
    for s in sessions:
        for t, label, category, text in s.events:
            if category != 'kill':
                continue
            m = KILL.match(text)
            if not m:
                continue
            rarity = m.group(2)
            ttk[rarity].append(float(m.group(5)))
            overkill[rarity].append(int(m.group(6)))
            by_skill[re.sub(r' gem \d+', '', m.group(4))] += 1
            by_monster[m.group(1)] += 1
    w('Time to kill (first hit to death)')
    w('---------------------------------')
    for rarity in RARITIES:
        values = ttk.get(rarity)
        if not values:
            continue
        instant = sum(1 for v in values if v <= 0.05)
        w(f'{rarity:<7} {len(values):>5} kills, median {median(values):.1f} s, 90% under {p90(values):.1f} s, '
          f'{pct(instant, len(values)):.0f}% die to the first hit, median overkill {median(overkill[rarity]):.0f}')
    total_kills = sum(by_skill.values())
    if total_kills:
        w('Kills by skill: ' + ', '.join(f'{name} {pct(n, total_kills):.0f}%' for name, n in by_skill.most_common(8)))
        w('Most killed: ' + ', '.join(f'{name} {n}' for name, n in by_monster.most_common(8)))
    w()

    # Danger: hits taken, how big, from what, and deaths.
    w('Danger')
    w('------')
    hits = collections.Counter()
    lowest = 100
    swings = collections.Counter()
    for s in sessions:
        for t, label, category, text in s.events:
            if category != 'stats' or 'no dungeon time' in text:
                continue
            m = STATS_PARTS['hits'].search(text)
            if m:
                hits['hits'] += int(m.group(1))
                hits['big'] += int(m.group(2))
                hits['close'] += int(m.group(3))
                hits['life'] += int(m.group(4))
                hits['shield'] += int(m.group(5) or 0)
                if m.group(6) is not None:
                    lowest = min(lowest, int(m.group(6)))
            m = STATS_PARTS['swings'].search(text)
            if m:
                for key, group in (('swings', 1), ('evaded', 2), ('blocked', 3), ('dodged', 4)):
                    swings[key] += int(m.group(group))
    dungeon_minutes = sum(a['seconds'] for area, a in areas.items() if area != 'town') / 60 or 1e-9
    w(f'Hits taken: {hits["hits"]} ({hits["hits"] / dungeon_minutes:.1f}/min in the dungeon), {hits["big"]} big, '
      f'{hits["close"]} close calls; damage {hits["life"]} to life, {hits["shield"]} to energy shield; lowest life {lowest}%')
    if swings['swings']:
        w(f'Monster melee swings: {swings["swings"]}, {pct(swings["evaded"], swings["swings"]):.0f}% evaded, '
          f'{pct(swings["blocked"], swings["swings"]):.0f}% blocked, {pct(swings["dodged"], swings["swings"]):.0f}% dodged')
    else:
        w('Monster melee swings: not in these logs yet (logged from build 31b9377 on).')
    stuns = sum(1 for s in sessions for e in s.events if e[2] == 'stunned')
    w(f'Stunned: {stuns} times')
    biggest = []
    for s in sessions:
        for t, label, category, text in s.events:
            if category in ('hit', 'close call'):
                m = TOOK.match(text)
                if m:
                    biggest.append((int(m.group(1)), label, m.group(3), m.group(2), int(m.group(6)), s.date[:10]))
    if biggest:
        w('Biggest hits:')
        for damage, label, attacker, kind, max_life, day in sorted(biggest, reverse=True)[:10]:
            w(f'  {damage} {kind} from {attacker} on {label} ({pct(damage, max_life):.0f}% of max life), {day}')
    for s in sessions:
        taken = [e[3] for e in s.events if e[2] == 'taken']
        if taken and 'none yet' not in taken[-1]:
            w(f'{s.date[:16]}: {taken[-1]}')
    deaths = [(s.date[:16], e[1], e[3]) for s in sessions for e in s.events if e[2] == 'death']
    w(f'Deaths: {len(deaths)}')
    for day, label, text in deaths:
        w(f'  {day} on {label}: {text}')
    w()

    # Pace: experience, level-ups, quiet time.
    w('Pace')
    w('----')
    xp = 0
    for s in sessions:
        for t, label, category, text in s.events:
            if category == 'stats' and 'no dungeon time' not in text:
                m = STATS_PARTS['xp'].search(text)
                if m:
                    xp += int(m.group(1))
    w(f'Experience: {xp} in {dungeon_minutes:.0f} dungeon minutes ({xp / dungeon_minutes:.0f}/min)')
    level_ups = []
    for s in sessions:
        previous = None
        for t, label, category, text in s.events:
            if category == 'levelup':
                m = LEVELUP.match(text)
                if m:
                    if previous is not None:
                        level_ups.append((int(m.group(1)), (t - previous) / 60))
                    previous = t
            elif category == 'game' and text.startswith(('loaded', 'new game')):
                previous = t
    if level_ups:
        w('Minutes played per character level: ' + ', '.join(f'{level}: {minutes:.0f}' for level, minutes in level_ups[-15:]))
    lull = sum(float(LULL.match(e[3]).group(1)) for s in sessions for e in s.events if e[2] == 'lull' and LULL.match(e[3]))
    swarms = sum(1 for s in sessions for e in s.events if e[2] == 'swarm')
    w(f'Quiet stretches (10 s or more with no enemy near): {lull / 60:.1f} min, {pct(lull / 60, dungeon_minutes):.0f}% of dungeon time; {swarms} swarms logged')
    density = []
    for s in sessions:
        for t, label, category, text in s.events:
            if category == 'stats':
                m = STATS_PARTS['density'].search(text)
                if m:
                    density.append((float(m.group(1)), int(m.group(4))))
    if density:
        w(f'Monsters within 5 tiles on average: {statistics.mean(d[0] for d in density):.1f}; '
          f'no enemy near {statistics.mean(d[1] for d in density):.0f}% of the time')
    w()

    # Resources: mana, skills, flasks.
    w('Resources')
    w('---------')
    res = collections.Counter()
    low_mana = []
    for s in sessions:
        for t, label, category, text in s.events:
            if category != 'stats' or 'no dungeon time' in text:
                continue
            m = STATS_PARTS['skills'].search(text)
            if m:
                res['skills'] += int(m.group(1))
                res['failed'] += int(m.group(2))
                res['mana'] += int(m.group(3))
                low_mana.append(int(m.group(4)))
            m = STATS_PARTS['flasks'].search(text)
            if m:
                res['flasks'] += int(m.group(1))
                res['wasted'] += int(m.group(2))
                res['charms'] += int(m.group(3))
    w(f'Skills used: {res["skills"]} ({res["skills"] / dungeon_minutes:.1f}/min), {res["failed"]} failed, '
      f'{res["mana"]} mana; low on mana {statistics.mean(low_mana) if low_mana else 0:.0f}% of the time')
    w(f'Flasks drunk: {res["flasks"]} ({res["wasted"]} wasted at near full), charms went off {res["charms"]} times')
    skill_names = collections.Counter()
    for s in sessions:
        for t, label, category, text in s.events:
            if category == 'skill':
                m = SKILL.match(text)
                if m:
                    skill_names[re.sub(r' gem \d+', '', m.group(1))] += 1
    if skill_names:
        w('Casts: ' + ', '.join(f'{name} {n}' for name, n in skill_names.most_common(10)))
    for s in sessions:
        damage = [e[3] for e in s.events if e[2] == 'damage']
        if damage and 'none yet' not in damage[-1]:
            w(f'{s.date[:16]}: {damage[-1]}')
    w()

    # Loot.
    w('Loot')
    w('----')
    drops = collections.Counter()
    pickups = collections.Counter()
    uniques = []
    currency = collections.Counter()
    for s in sessions:
        for t, label, category, text in s.events:
            if category == 'drop':
                m = DROP.match(text)
                if m:
                    drops[m.group(2)] += 1
                    if m.group(2) == 'unique':
                        uniques.append(f'{m.group(1)} on {label}')
                    if m.group(1).startswith(('Orb of', 'Chaos Orb', 'Exalted', 'Divine', 'Regal', 'Vaal', 'Jeweller', 'Gemcutter', 'Glassblower', 'Artificer', 'Armourer', 'Blacksmith')):
                        currency[m.group(1)] += 1
            elif category == 'pickup':
                m = PICKUP.match(text)
                if m:
                    pickups[m.group(2)] += 1
    hours = dungeon_minutes / 60 or 1e-9
    w('Drops per dungeon hour: ' + ', '.join(f'{r} {drops[r] / hours:.0f}' for r in RARITIES))
    w('Picked up: ' + ', '.join(f'{r} {pickups[r]}' for r in RARITIES))
    if currency:
        w('Currency dropped: ' + ', '.join(f'{name} {n}' for name, n in currency.most_common()))
    if uniques:
        w('Uniques dropped: ' + '; '.join(uniques[-15:]))
    w()

    # Performance and crashes.
    w('Performance')
    w('-----------')
    fps = []
    slow = 0
    worst_frame = 0
    for s in sessions:
        for t, label, category, text in s.events:
            if category == 'stats':
                m = STATS_PARTS['fps'].search(text)
                if m and float(m.group(1)) > 0:
                    fps.append(float(m.group(1)))
                    slow += int(m.group(2))
                    worst_frame = max(worst_frame, int(m.group(3)))
    if fps:
        w(f'Frame rate: median {median(fps):.0f} fps, lowest minute {min(fps):.0f} fps; {slow} frames under 30 fps, slowest {worst_frame} ms')
    stalls = []
    for s in sessions:
        for t, label, category, text in s.events:
            if category == 'stall':
                m = STALL.match(text)
                if m:
                    stalls.append((int(m.group(1)), label, int(m.group(2)), int(m.group(3)), s.date[:16]))
    # Before build 31b9377 the count also took in pauses (a menu open while frames kept drawing), mostly in town.
    w(f'Freezes over 250 ms: {len(stalls)}' + (f', {sum(1 for x in stalls if x[1] == "town")} in town' if stalls else ''))
    for ms, label, monsters, missiles, day in sorted(stalls, reverse=True)[:8]:
        w(f'  {ms} ms on {label} ({monsters} monsters, {missiles} missiles), {day}')
    crashes = [(s.date[:16], e[1], e[3]) for s in sessions for e in s.events if e[2] == 'crash']
    bugs = collections.Counter(e[3] for s in sessions for e in s.events if e[2] == 'bug')
    w(f'Crashes: {len(crashes)}')
    for day, label, text in crashes:
        w(f'  {day} on {label}: {text[:200]}')
    for text, n in bugs.most_common(10):
        w(f'  bug x{n}: {text[:200]}')
    marks = sum(1 for s in sessions for e in s.events if e[2] == 'mark')
    w()
    w(f'Notes typed with mark(): {marks} (read them with tools/check_marks.py)')


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('folder', nargs='?', default=os.path.join(os.path.dirname(here), 'poe2_logs'))
    parser.add_argument('--last', type=int, default=0, help='only the newest N sessions')
    parser.add_argument('--since', default='', help='only sessions started on or after this date (YYYY-MM-DD)')
    parser.add_argument('--hero', default='', help='only sessions played with this hero')
    parser.add_argument('--out', default='', help='also write the report to this file')
    args = parser.parse_args()

    names = sorted(n for n in os.listdir(args.folder) if n.startswith('session_') and n.endswith('.log'))
    sessions = [read_session(os.path.join(args.folder, n)) for n in names]
    sessions = [s for s in sessions if s.events]
    if args.since:
        sessions = [s for s in sessions if s.date[:10] >= args.since]
    if args.hero:
        sessions = [s for s in sessions if s.hero == args.hero]
    if args.last:
        sessions = sessions[-args.last:]
    if not sessions:
        sys.exit('No play logs matched.')

    class Tee:
        def __init__(self, *streams):
            self.streams = streams

        def write(self, text):
            for stream in self.streams:
                stream.write(text)

    if args.out:
        with open(args.out, 'w', encoding='utf-8') as f:
            report(sessions, Tee(sys.stdout, f))
    else:
        report(sessions, sys.stdout)


main()
