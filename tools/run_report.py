#!/usr/bin/env python3
"""Builds a run report page from POE2 Mashup event files (poe2_logs/session_*.events).

Each session writes a readable log and, beside it, an event file: one JSON object per line with a kind ("k"), a
time in ms since the session started ("t"), the area ("area") and named fields (Source/poe2/telemetry.cpp). This
reads only the event files, so a change in the log's wording can't break a number here.

The page answers the playtest questions in order: is it too easy (fights and pressure, per area), which skills
carry, is levelling on pace, is loot right, and what went wrong (deaths, crashes, warnings). With --compare it
lines two sets of sessions up side by side, to see whether a balance change did what it was meant to.

Usage:
  python3 tools/run_report.py [log folder] [--since YYYY-MM-DD] [--last N] [--hero NAME] [--out FILE]
  python3 tools/run_report.py [log folder] --compare 20261020 20261022 [--out FILE]

The folder defaults to poe2_logs beside this script's folder (the game folder's tools/ and poe2_logs/). The page
goes to run_report.html in the log folder unless --out says otherwise. Run check_marks.py first: notes come first.
"""
import argparse
import collections
import html
import json
import os
import re
import statistics
import sys

# The "is it too easy" targets from the Levels 1 to 100 plan.
ONE_HIT_KILL_TARGET = 20      # percent of kills, at most
LOW_MANA_FIGHTS = (10, 20)    # percent of fights that run mana under 20%
HARD_FIGHT_LIFE = 50          # every floor should take the hero under this percent of life at least once


def find_sessions(folder, since=None, last=None, match=None):
    names = sorted(f for f in os.listdir(folder) if f.startswith('session_') and f.endswith('.events'))
    if since:
        stamp = since.replace('-', '')
        names = [n for n in names if n[len('session_'):len('session_') + 8] >= stamp]
    if match:
        names = [n for n in names if match in n]
    if last:
        names = names[-last:]
    return [os.path.join(folder, n) for n in names]


def read_events(path):
    events = []
    with open(path, encoding='utf-8', errors='replace') as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                events.append(json.loads(line))
            except json.JSONDecodeError:
                # A crash can cut the last line short.
                pass
    return events


def pct(part, whole):
    return 100.0 * part / whole if whole else 0.0


class Run:
    """Everything the report needs from a set of sessions."""

    def __init__(self, paths, hero=None):
        self.sessions = []
        self.games = []
        self.fights = []
        self.kills = []
        self.areas = []          # area entries, in order
        self.area_left = []
        self.levelups = []
        self.summaries = []
        self.skills = collections.defaultdict(lambda: collections.Counter())
        self.attackers = collections.defaultdict(lambda: collections.Counter())
        self.drops = []
        self.pickups = []
        self.crafts = []
        self.gold_out = 0
        self.deaths = []
        self.crashes = []
        self.warnings = collections.Counter()
        self.marks = []
        self.dropped = collections.Counter()
        self.dealt_detail = []
        self.taken_detail = []
        self.hits_taken = []
        self.flasks = []
        for path in paths:
            self.read(path, hero)

    def read(self, path, hero):
        events = read_events(path)
        if not events:
            return
        name = os.path.basename(path)
        current_hero = None
        session = {'name': name, 'build': events[0].get('build', ''), 'started': events[0].get('started', '')}
        self.sessions.append(session)
        for e in events:
            k = e.get('k')
            if k == 'game_start':
                current_hero = e.get('hero')
            # Session-wide events count whoever is playing; the rest only for the chosen hero.
            if hero and current_hero != hero and k not in ('crash', 'engine', 'bug', 'dropped'):
                continue
            e['session'] = name
            if k == 'game_start':
                self.games.append(e)
            elif k == 'fight':
                self.fights.append(e)
            elif k == 'kill':
                self.kills.append(e)
            elif k == 'area':
                self.areas.append(e)
            elif k == 'area_left':
                self.area_left.append(e)
            elif k == 'levelup':
                self.levelups.append(e)
            elif k == 'summary':
                self.summaries.append(e)
            elif k == 'skill_summary':
                s = self.skills[e['skill']]
                for key in ('damage', 'hits', 'kills', 'uses', 'mana'):
                    s[key] += e.get(key, 0)
                # 'damage' is each hit's full damage; older logs counted only the life it took away, as lifeRemoved now does.
                s['lifeRemoved'] += e.get('lifeRemoved', e.get('damage', 0))
                s['killSecondsSum'] += e.get('killSeconds', 0) * e.get('kills', 0)
            elif k == 'attacker_summary':
                a = self.attackers[e['attacker']]
                a['damage'] += e.get('damage', 0)
                a['hits'] += e.get('hits', 0)
                a['biggest'] = max(a['biggest'], e.get('biggest', 0))
            elif k == 'drop':
                self.drops.append(e)
            elif k == 'pickup':
                self.pickups.append(e)
            elif k == 'craft':
                self.crafts.append(e)
            elif k == 'gold_out':
                self.gold_out += e.get('amount', 0)
            elif k == 'death':
                self.deaths.append(e)
            elif k == 'crash':
                self.crashes.append(e)
            elif k in ('engine', 'bug'):
                text = re.sub(r'\d+', '#', e.get('text', ''))
                self.warnings[f"{k}: {text}"] += 1
            elif k == 'mark':
                self.marks.append(e)
            elif k == 'dropped':
                self.dropped[e.get('of', '?')] += e.get('n', 0)
            elif k == 'dealt' and 'steps' in e:
                self.dealt_detail.append(e)
            elif k == 'hit_taken':
                self.hits_taken.append(e)
                if 'steps' in e:
                    self.taken_detail.append(e)
            elif k == 'flask':
                self.flasks.append(e)

    # Headline numbers, shared by the summary and the compare table.
    def headline(self):
        minutes = sum(s.get('minutes', 0) for s in self.summaries)
        if not minutes and self.areas:
            minutes = max(a.get('gameMinutes', 0) for a in self.areas)
        hours = minutes / 60 if minutes else 0
        kills = len(self.kills)
        one_hit = sum(1 for k in self.kills if k.get('oneHit'))
        fights = len(self.fights)
        hard = sum(1 for f in self.fights if f.get('lowestLife', 100) < HARD_FIGHT_LIFE or f.get('died'))
        low_mana = sum(1 for f in self.fights if f.get('lowestMana', 100) < 20)
        pressures = [f.get('pressure', 0) for f in self.fights]
        rarity = collections.Counter(d.get('rarity') for d in self.drops)
        xp = sum(s.get('xp', 0) for s in self.summaries)
        return collections.OrderedDict([
            ('Minutes played', minutes),
            ('Deaths', len(self.deaths)),
            ('Kills', kills),
            ('Kills per minute', kills / minutes if minutes else 0),
            ('One-hit kills %', pct(one_hit, kills)),
            ('Fights', fights),
            ('Fights under half life %', pct(hard, fights)),
            ('Average pressure', statistics.mean(pressures) if pressures else 0),
            ('Fights low on mana %', pct(low_mana, fights)),
            ('Flasks per fight', len([f for f in self.flasks if f.get('inFight')]) / fights if fights else 0),
            ('Hits taken per minute', len(self.hits_taken) / minutes if minutes else 0),
            ('Seconds per kill (median)', statistics.median([k.get('seconds', 0) for k in self.kills if k.get('hits')]) if any(k.get('hits') for k in self.kills) else 0),
            ('XP per hour', xp / hours if hours else 0),
            ('Magic drops per hour', rarity['magic'] / hours if hours else 0),
            ('Rare drops per hour', rarity['rare'] / hours if hours else 0),
            ('Unique drops per hour', rarity['unique'] / hours if hours else 0),
            ('Crashes', len(self.crashes)),
        ])

    def per_area(self):
        """Per area label, in the order first visited."""
        rows = collections.OrderedDict()

        def row(label):
            if label not in rows:
                rows[label] = {'visits': 0, 'seconds': 0, 'monsterLevels': [], 'heroLevels': [], 'kills': 0, 'oneHit': 0,
                               'fights': 0, 'hard': 0, 'pressure': [], 'deaths': 0, 'damage': 0, 'drops': 0}
            return rows[label]
        for a in self.areas:
            r = row(a['area'])
            r['visits'] += 1
            if a.get('monsterLevel'):
                r['monsterLevels'].append(a['monsterLevel'])
            r['heroLevels'].append(a.get('heroLevel', 0))
        for a in self.area_left:
            r = row(a['area'])
            r['seconds'] += a.get('seconds', 0)
            r['damage'] += a.get('damageTaken', 0) + a.get('shieldTaken', 0)
            r['drops'] += a.get('drops', 0)
        for k in self.kills:
            r = row(k['area'])
            r['kills'] += 1
            r['oneHit'] += 1 if k.get('oneHit') else 0
        for f in self.fights:
            r = row(f['area'])
            r['fights'] += 1
            r['hard'] += 1 if f.get('lowestLife', 100) < HARD_FIGHT_LIFE or f.get('died') else 0
            r['pressure'].append(f.get('pressure', 0))
        for d in self.deaths:
            row(d['area'])['deaths'] += 1
        return [(label, r) for label, r in rows.items() if label != 'town']


def esc(text):
    return html.escape(str(text))


def num(value, digits=1):
    if isinstance(value, float):
        return f"{value:,.{digits}f}"
    return f"{value:,}"


def table(headers, rows, numeric_from=1):
    out = ['<div class="scroll"><table><thead><tr>']
    out += [f'<th class="{"n" if i >= numeric_from else ""}">{esc(h)}</th>' for i, h in enumerate(headers)]
    out.append('</tr></thead><tbody>')
    for r in rows:
        out.append('<tr>' + ''.join(f'<td class="{"n" if i >= numeric_from else ""}">{c}</td>' for i, c in enumerate(r)) + '</tr>')
    out.append('</tbody></table></div>')
    return ''.join(out)


def target_checks(run):
    h = run.headline()
    checks = []
    one_hit = h['One-hit kills %']
    checks.append(('One-hit kills under %d%%' % ONE_HIT_KILL_TARGET, one_hit <= ONE_HIT_KILL_TARGET, f"{one_hit:.0f}% of kills"))
    low = h['Fights low on mana %']
    checks.append(('Low on mana in %d to %d%% of fights' % LOW_MANA_FIGHTS, LOW_MANA_FIGHTS[0] <= low <= LOW_MANA_FIGHTS[1], f"{low:.0f}% of fights"))
    floors = run.per_area()
    easy = [label for label, r in floors if r['fights'] and r['hard'] == 0]
    checks.append(('Every floor takes you under half life at least once', not easy and bool(floors),
                   'all floors did' if not easy else f"{len(easy)} of {len(floors)} floors never did: {', '.join(easy[:8])}"))
    return checks


CSS = """
:root { --bg:#f7f6f3; --panel:#ffffff; --text:#1d1c1a; --muted:#6b6862; --line:#e3e0da; --good:#2f7d4f; --bad:#b3412f; --accent:#8a5a1f; }
@media (prefers-color-scheme: dark) { :root:not([data-theme="light"]) { --bg:#171614; --panel:#201f1c; --text:#ecebe7; --muted:#a19d95; --line:#34322e; --good:#6cc08f; --bad:#e2826f; --accent:#d9a55c; } }
:root[data-theme="dark"] { --bg:#171614; --panel:#201f1c; --text:#ecebe7; --muted:#a19d95; --line:#34322e; --good:#6cc08f; --bad:#e2826f; --accent:#d9a55c; }
* { box-sizing: border-box; }
body { margin:0; background:var(--bg); color:var(--text); font:15px/1.5 system-ui, -apple-system, Segoe UI, sans-serif; }
main { max-width: 900px; margin: 0 auto; padding: 20px 16px 48px; }
h1 { font-size: 1.5rem; margin: 0 0 4px; } h2 { font-size: 1.15rem; margin: 28px 0 8px; color: var(--accent); }
p.sub { color: var(--muted); margin: 0 0 16px; }
.scroll { overflow-x: auto; background: var(--panel); border: 1px solid var(--line); border-radius: 8px; }
table { border-collapse: collapse; width: 100%; font-size: 0.9rem; }
th, td { padding: 6px 10px; border-bottom: 1px solid var(--line); text-align: left; white-space: nowrap; }
th { color: var(--muted); font-weight: 600; } td.n, th.n { text-align: right; font-variant-numeric: tabular-nums; }
.good { color: var(--good); font-weight: 600; } .bad { color: var(--bad); font-weight: 600; }
ul { padding-left: 20px; } li { margin: 4px 0; } code { font-size: 0.85rem; }
"""


def page(title, subtitle, body):
    return (f'<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">'
            f'<title>{esc(title)}</title><style>{CSS}</style></head><body><main><h1>{esc(title)}</h1><p class="sub">{esc(subtitle)}</p>{body}</main></body></html>')


def report(run):
    parts = []
    h = run.headline()
    heroes = sorted({f"{g.get('hero')} ({g.get('class')})" for g in run.games})
    parts.append('<h2>Your notes</h2>')
    if run.marks:
        parts.append('<ul>' + ''.join(f"<li><b>{esc(m['session'][8:23])} {esc(m.get('area', ''))}</b>: {esc(m.get('text', ''))}</li>" for m in run.marks) + '</ul>')
    else:
        parts.append('<p>No notes typed with mark() in these sessions.</p>')

    parts.append('<h2>Summary</h2>')
    parts.append(table(['', 'Value'], [[esc(k), num(v)] for k, v in h.items()]))
    parts.append('<h2>Targets</h2>')
    parts.append('<ul>' + ''.join(f'<li><span class="{"good" if ok else "bad"}">{"Met" if ok else "Missed"}</span>: {esc(name)} ({esc(detail)})</li>'
                                  for name, ok, detail in target_checks(run)) + '</ul>')

    parts.append('<h2>Per area</h2>')
    rows = []
    for label, r in run.per_area():
        rows.append([esc(label), r['visits'], num(r['seconds'] / 60), statistics.median(r['monsterLevels']) if r['monsterLevels'] else '',
                     min(r['heroLevels']) if r['heroLevels'] else '', r['kills'], num(pct(r['oneHit'], r['kills']), 0), r['fights'], r['hard'],
                     num(statistics.mean(r['pressure']), 0) if r['pressure'] else '', r['deaths'], r['damage'], r['drops']])
    parts.append(table(['Area', 'Visits', 'Minutes', 'Monster lvl', 'Hero lvl', 'Kills', 'One-hit %', 'Fights', 'Under half', 'Pressure', 'Deaths', 'Damage taken', 'Drops'], rows))

    parts.append('<h2>Hardest fights</h2>')
    hardest = sorted(run.fights, key=lambda f: -f.get('pressure', 0))[:12]
    parts.append(table(['Area', 'Monsters', 'Rare/unique', 'Mon lvl', 'Hero lvl', 'Seconds', 'Lowest life', 'Biggest hit', 'Flasks', 'Pressure'],
                       [[esc(f['area']), f.get('monsters', 0), f"{f.get('rare', 0)}/{f.get('unique', 0)}", f.get('monsterLevel', 0), f.get('heroLevel', 0),
                         num(f.get('seconds', 0)), f"{f.get('lowestLife', 0)}%", f"{f.get('biggestHit', 0)} ({esc(f.get('biggestHitFrom', ''))})",
                         f.get('flasks', 0), f.get('pressure', 0)] for f in hardest]))

    parts.append('<h2>Skills</h2>')
    skill_rows = []
    for name, s in sorted(run.skills.items(), key=lambda kv: -kv[1]['damage']):
        skill_rows.append([esc(name), s['damage'], s['lifeRemoved'], s['hits'], s['kills'], num(s['killSecondsSum'] / s['kills']) if s['kills'] else '',
                           s['uses'], s['mana'], num(s['damage'] / s['mana']) if s['mana'] else ''])
    parts.append(table(['Skill', 'Damage', 'Life removed', 'Hits', 'Kills', 's to kill', 'Uses', 'Mana', 'Dmg per mana'], skill_rows))

    parts.append('<h2>What hurt you</h2>')
    parts.append(table(['Attacker', 'Damage', 'Hits', 'Biggest'],
                       [[esc(n), a['damage'], a['hits'], a['biggest']] for n, a in sorted(run.attackers.items(), key=lambda kv: -kv[1]['damage'])[:15]]))

    parts.append('<h2>Levelling</h2>')
    parts.append(table(['Game minutes', 'Area', 'Hero lvl', 'Monster lvl', 'Minutes since last area'],
                       [[num(a.get('gameMinutes', 0)), esc(a['area']), a.get('heroLevel', 0), a.get('monsterLevel', 0), num(a.get('minutesSinceLastArea', 0))]
                        for a in run.areas if a['area'] != 'town'], numeric_from=2))
    if run.levelups:
        parts.append('<p>Level-ups: ' + ', '.join(f"{l.get('heroLevel')} at {num(l.get('gameMinutes', 0))} min" for l in run.levelups) + '.</p>')

    parts.append('<h2>Loot</h2>')
    by_rarity = collections.Counter(d.get('rarity') for d in run.drops)
    picked = collections.Counter(p.get('rarity') for p in run.pickups)
    parts.append(table(['Rarity', 'Dropped', 'Picked up'], [[r, by_rarity[r], picked[r]] for r in ('normal', 'magic', 'rare', 'unique')]))
    currency = collections.Counter(d.get('base') or d.get('item') for d in run.drops if d.get('currency'))
    if currency:
        parts.append('<p>Currency dropped: ' + ', '.join(f"{esc(n)} x{c}" for n, c in currency.most_common()) + '.</p>')
    crafts = collections.Counter(c.get('with') for c in run.crafts)
    if crafts:
        parts.append('<p>Currency used: ' + ', '.join(f"{esc(n)} x{c}" for n, c in crafts.most_common()) + '.</p>')
    uniques = [d for d in run.drops if d.get('rarity') == 'unique']
    if uniques:
        parts.append('<p>Uniques: ' + ', '.join(f"{esc(u.get('item'))} ({esc(u['area'])})" for u in uniques) + '.</p>')
    gold_in = sum(s.get('goldIn', 0) for s in run.summaries)
    parts.append(f'<p>Gold picked up {gold_in:,}, gold out (shops and stash) {run.gold_out:,}.</p>')

    parts.append('<h2>Deaths, crashes and warnings</h2>')
    items = [f"<li>Died in {esc(d['area'])} at level {d.get('heroLevel')}: {esc(d.get('lastHit', ''))}</li>" for d in run.deaths]
    items += [f"<li>Crash in {esc(c['area'])}: {esc(c.get('text', ''))}</li>" for c in run.crashes]
    items += [f"<li>{esc(w)} (x{n})</li>" for w, n in run.warnings.most_common(20)]
    items += [f"<li>{n} {esc(kind)} lines skipped by the rate limit</li>" for kind, n in run.dropped.most_common(5)]
    parts.append('<ul>' + ''.join(items) + '</ul>' if items else '<p>None.</p>')

    if run.dealt_detail or run.taken_detail:
        parts.append('<h2>Damage breakdowns (deep logging)</h2>')
        for title, details in (('Your hits', run.dealt_detail[-8:]), ('Hits on you', run.taken_detail[-8:])):
            if not details:
                continue
            parts.append(f'<p><b>{title}</b>, latest first:</p><ul>')
            for d in reversed(details):
                steps = ' &rarr; '.join(f"{esc(s['step'])} {s['percent']}%" if 'percent' in s else f"{esc(s['step'])} {s['damage']:.1f}" for s in d['steps'])
                who = d.get('monster') or d.get('attacker', '')
                parts.append(f"<li>{esc(d.get('hit', ''))} {esc(who)}: {steps} &rarr; <b>{d.get('damage', 0):.1f}</b></li>")
            parts.append('</ul>')

    sessions = ', '.join(s['name'][8:23] for s in run.sessions)
    subtitle = f"{len(run.sessions)} sessions ({sessions}), heroes: {', '.join(heroes) or 'none'}"
    return page('Run Report', subtitle, ''.join(parts))


def compare(a, b, name_a, name_b):
    ha, hb = a.headline(), b.headline()
    rows = []
    for key in ha:
        va, vb = ha[key], hb[key]
        change = ''
        if isinstance(va, (int, float)) and va:
            change = f"{(vb - va) * 100.0 / va:+.0f}%"
        rows.append([esc(key), num(va), num(vb), change])
    body = table(['', esc(name_a), esc(name_b), 'Change'], rows)
    body += '<h2>Targets</h2>' + table(['', esc(name_a), esc(name_b)],
                                       [[esc(n), ('Met' if ok_a else 'Missed') + f' ({esc(d_a)})', ('Met' if ok_b else 'Missed') + f' ({esc(d_b)})']
                                        for (n, ok_a, d_a), (_, ok_b, d_b) in zip(target_checks(a), target_checks(b))])
    return page('Run Comparison', f"{name_a} ({len(a.sessions)} sessions) against {name_b} ({len(b.sessions)} sessions)", body)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('folder', nargs='?', default=os.path.join(os.path.dirname(here), 'poe2_logs'))
    parser.add_argument('--since', help='only sessions from this date (YYYY-MM-DD)')
    parser.add_argument('--last', type=int, help='only the last N sessions')
    parser.add_argument('--hero', help='only this hero')
    parser.add_argument('--compare', nargs=2, metavar=('A', 'B'), help='compare sessions whose names contain A with those containing B (e.g. 20261020 20261022)')
    parser.add_argument('--out', help='where to write the page (default run_report.html in the log folder)')
    args = parser.parse_args()
    if not os.path.isdir(args.folder):
        sys.exit(f"No log folder at {args.folder}")
    out = args.out or os.path.join(args.folder, 'run_report.html')
    if args.compare:
        runs = [Run(find_sessions(args.folder, since=args.since, match=m), args.hero) for m in args.compare]
        for m, r in zip(args.compare, runs):
            if not r.sessions:
                sys.exit(f"No sessions with event files match {m}")
        text = compare(runs[0], runs[1], *args.compare)
    else:
        paths = find_sessions(args.folder, since=args.since, last=args.last)
        if not paths:
            sys.exit("No session event files found (builds before the event file only have .log files: use balance_report.py for those).")
        run = Run(paths, args.hero)
        text = report(run)
        for key, value in run.headline().items():
            print(f"{key}: {num(value)}")
    with open(out, 'w', encoding='utf-8') as f:
        f.write(text)
    print(f"Wrote {out}")


if __name__ == '__main__':
    main()
