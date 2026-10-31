#!/usr/bin/env python3
"""Power audit of the passive tree (from the tree rework, research/tree-rework/content-pass/power_audit.py): reference builds on two trees, same method.

Usage:
  python3 power_audit.py TODAY.tsv NEW.tsv [--notable-scale 1.0] [--small-scale 1.0] [--stat-scale K=x,...] [--attr-dmg 0.3]
                         [--override NAME_OR_ID=K=v,...] [--budgets 73,123] [--sockets 0,0] [--show NAME]

For each reference build and budget it allocates a fixed greedy build from the class start on both trees and sums
the stats. Ratios are new / today. The scales apply to the NEW tree only, to positive non-rule lines of notables
(--notable-scale) and of cluster smalls (--small-scale; every Small that is not a travel small: Life 3 / Res 2-3 /
Mana 3-4), rounded half up per line, as the content file would round them.

Method (identical on both trees):
  * Graph: the links column, both directions. Ascendancy nodes, shrines and the other classes' starts are blocked
    (shrines are leaves; their rules are not valued, so no build takes one; this is the same on both trees).
  * Each allocated node costs 1 point; the start is free. Budgets are 73 (level 50) and 123 (level 100).
  * Greedy: from the allocated set, a multi-source BFS gives each node its distance; among shortest paths the one
    with the most value is kept. The node with the best value / distance (ties: more value) whose distance fits the
    budget is allocated with its whole path. Then a local search drops the branch that loses least per point,
    refills greedily and keeps the result when it is better.
  * '+5 any attribute' nodes count as +5 to the build's main attribute. Jewel sockets count as empty (value 0).
  * Rule stats (capstone rules, weapon-enabler crossovers, shrine rules, 'more vs rare') are valued 0 on both trees:
    the audit measures plain stat lines. Negative lines (Profane drawbacks, old trade-off notables) count.
  * Value (utility): increases add up, so each is worth log(1 + total/100): damage x1, speed x1 (only up to the 30%
    frame cap), main defence x W_DEF, life x0.8, mana x0.15 (casters 0.3); resistances linear to 60 (x0.3). The greedy
    re-linearises it at the current totals every step. Each row averages three builds, W_DEF 0.35 / 0.5 / 0.7.
  * Damage attributes (Strength for melee, Dexterity and Strength for bows and staves, Magic for spells) add
    --attr-dmg % main damage per point (default 0.3: D1's damage bonus is clvl * Str / 100 for a Warrior).
    Vitality adds 0.5% life per point.
  * Columns: main damage %, main defence %, life %, surv = ratio of (1+def)^0.5 * (1+life)^0.8, resistances, speed
    (capped at 30), attribute points, notables; T = today, N = new. Exit code 0 when check 3d passes.
"""
import argparse
import csv
import math
import sys
from collections import defaultdict

TRAVEL = {('LifePercent', 3), ('AllResist', 2), ('AllResist', 3), ('ManaPercent', 3), ('ManaPercent', 4)}
RULE_STATS = {
    'TwoHandKillFrenzy', 'BlockRiposte', 'ElementRotation', 'MinionSpellDamage', 'StaffToLightning',
    'UnarmedHitStacks', 'ArrowPierce', 'SwapStrike', 'BowStrength', 'SpellDamageToFireAttacks',
    'BowAppliesToStaff', 'StaffAppliesToBow', 'ManaSpellDamage', 'DamageVsRareUnique', 'HitDamageFromMana',
    'ArmourVsElements', 'EvasionVsSpells', 'StationaryDefences', 'PhysicalToFire', 'MaxElementalResist',
    'MaxColdResist', 'MaxChaosResist',
}
BLOCKED_KINDS = {'Ascendancy', 'AscendancyNotable', 'Keystone', 'Shrine'}
VIT_LIFE = 0.5

# name: class, main damage lines, main defence lines, damage attributes (first = main), speed stat, extra weights
BUILDS = {
    'Warrior two-hand': dict(cls='Warrior', dmg={'TwoHandDamage': 1, 'MeleeDamage': 1, 'Damage': 1},
                             dfn={'ArmorPercent': 1}, attrs=('Strength',), speed='AttackSpeed', caster=False),
    'Sorcerer elemental (lightning)': dict(cls='Sorcerer', dmg={'SpellDamage': 1, 'LightningDamage': 1},
                                           dfn={'EnergyShieldPercent': 1}, attrs=('Magic',), speed='CastSpeed', caster=True),
    'Rogue bow': dict(cls='Rogue', dmg={'BowDamage': 1, 'Damage': 1}, dfn={'EvasionPercent': 1},
                      attrs=('Dexterity', 'Strength'), speed='AttackSpeed', caster=False),
    'Monk staff': dict(cls='Monk', dmg={'StaffDamage': 1, 'MeleeDamage': 1, 'TwoHandDamage': 1, 'Damage': 1},
                       dfn={'EvasionPercent': 0.5, 'EnergyShieldPercent': 0.5}, attrs=('Dexterity', 'Strength'),
                       speed='AttackSpeed', caster=False),
    'Warrior sword+board, Khanduras': dict(cls='Warrior', dmg={'OneHandDamage': 1, 'MeleeDamage': 1, 'Damage': 1},
                                           dfn={'ArmorPercent': 1, 'EvasionPercent': 0.5}, attrs=('Strength',),
                                           speed='AttackSpeed', caster=False, extra={'BlockChance': 0.5}),
    'Sorcerer minions, Zakarum': dict(cls='Sorcerer', dmg={'MinionDamage': 1, 'Spirit': 1.5},
                                      dfn={'EnergyShieldPercent': 1, 'ArmorPercent': 0.5}, attrs=('Magic',),
                                      speed='CastSpeed', caster=True, extra={'MinionLife': 0.1}),
}


def round_half_up(x):
    return int(x + 0.5) if x >= 0 else -int(-x + 0.5)


def load(path, notable_scale=1.0, small_scale=1.0, stat_scale=None):
    stat_scale = stat_scale or {}
    nodes = {}
    with open(path, newline='') as f:
        for r in csv.DictReader(f, delimiter='\t'):
            stats = []
            for kv in (r.get('stats') or '').split(','):
                if '=' in kv:
                    k, v = kv.split('=')
                    stats.append((k.strip(), int(v)))
            kind = r['kind']
            is_travel = kind == 'Small' and all((k, v) in TRAVEL for k, v in stats)
            scale = notable_scale if kind == 'Notable' else small_scale if (kind == 'Small' and not is_travel) else 1.0
            if kind == 'Notable' or (kind == 'Small' and not is_travel):
                stats = [(k, round_half_up(v * scale * stat_scale.get(k, 1.0)) if v > 0 and k not in RULE_STATS else v)
                         for k, v in stats]
            nodes[r['id']] = dict(id=r['id'], name=r['name'], kind=kind, cls=r.get('class', ''), stats=stats,
                                  links=[l for l in (r.get('links') or '').split(',') if l], group=r.get('group', ''))
    adj = defaultdict(set)
    for n in nodes.values():
        for l in n['links']:
            if l in nodes:
                adj[n['id']].add(l)
                adj[l].add(n['id'])
    return nodes, adj


def node_stats(n, build):
    """The node's stat lines for this build: '+5 any' becomes +5 to the main attribute."""
    out = []
    for k, v in n['stats']:
        out.append((build['attrs'][0], v) if k == 'AnyAttribute' else (k, v))
    return out


def summarize(stat_totals, build, attr_dmg):
    s = stat_totals
    attr_pts = sum(s.get(a, 0) for a in build['attrs'])
    dmg_lines = sum(w * s.get(k, 0) for k, w in build['dmg'].items())
    dmg = dmg_lines + attr_dmg * attr_pts
    dfn = sum(w * s.get(k, 0) for k, w in build['dfn'].items())
    life = s.get('LifePercent', 0) + VIT_LIFE * s.get('Vitality', 0)
    # Chaos resistance counts like one element's: a third of an all-resistances line.
    res = s.get('AllResist', 0) + (s.get('FireResist', 0) + s.get('LightningResist', 0) + s.get('ColdResist', 0) + s.get('ChaosResist', 0)) / 3
    speed = s.get(build['speed'], 0)
    mana = s.get('ManaPercent', 0)
    extra = sum(w * s.get(k, 0) for k, w in build.get('extra', {}).items())
    return dict(dmg=dmg, dfn=dfn, life=life, res=res, speed=speed, mana=mana, extra=extra, attr=attr_pts,
                value=utility(dmg, dfn, life, res, speed, mana, extra, build))


# The build's value: increases add up, so each stat is worth the log of (1 + total); the greedy re-linearises this
# at the current totals every step, which balances damage and defence the way a player would.
W_DMG, W_SPEED, W_DEF, W_LIFE, W_RES, W_MANA, W_MANA_CASTER = 1.0, 1.0, 0.5, 0.8, 0.3, 0.15, 0.3
SPEED_CAP = 30      # MaxPassiveSpeedFrames 3 x SpeedPercentPerFrame 10: tree speed past 30% is wasted
RES_CAP = 60        # gear is assumed to give 15% of the 75% cap; tree resistances are worth W_RES / RES_CAP per %, to the cap


def utility(dmg, dfn, life, res, speed, mana, extra, build):
    wm = W_MANA_CASTER if build['caster'] else W_MANA
    return (W_DMG * math.log1p(max(dmg, -90) / 100) + W_SPEED * math.log1p(max(min(speed, SPEED_CAP), -90) / 100)
            + W_DEF * math.log1p(max(dfn, -90) / 100) + W_LIFE * math.log1p(max(life, -90) / 100)
            + W_RES * min(res, RES_CAP) / RES_CAP + wm * math.log1p(max(mana, -90) / 100)
            + extra / 100)


def weights(cur, build, attr_dmg):
    """Value per unit of each raw stat at the current totals (the derivative of utility)."""
    wm = W_MANA_CASTER if build['caster'] else W_MANA
    d = W_DMG / (100 + cur['dmg'])
    w = defaultdict(float)
    for k, x in build['dmg'].items():
        w[k] += x * d
    for a in build['attrs']:
        w[a] += attr_dmg * d
    for k, x in build['dfn'].items():
        w[k] += x * W_DEF / (100 + cur['dfn'])
    w['LifePercent'] += W_LIFE / (100 + cur['life'])
    w['Vitality'] += VIT_LIFE * W_LIFE / (100 + cur['life'])
    r = W_RES / RES_CAP if cur['res'] < RES_CAP else 0.0
    w['AllResist'] += r
    for k in ('FireResist', 'LightningResist', 'ColdResist', 'ChaosResist'):
        w[k] += r / 3
    w[build['speed']] += W_SPEED / (100 + cur['speed']) if cur['speed'] < SPEED_CAP else 0.0
    w['ManaPercent'] += wm / (100 + cur['mana'])
    for k, x in build.get('extra', {}).items():
        w[k] += x / 100
    return w


def node_value(n, build, w):
    return sum(w.get(k, 0.0) * v for k, v in node_stats(n, build) if k not in RULE_STATS)


def totals_of(nodes, taken, build):
    totals = defaultdict(int)
    for i in taken:
        for k, v in node_stats(nodes[i], build):
            if k not in RULE_STATS:
                totals[k] += v
    return totals


def sortkey(i):
    return (0, int(i), '') if i.isdigit() else (1, 0, i)


def greedy(nodes, adj, build, budget, attr_dmg, force_sockets=0, improve=True):
    start = next(n['id'] for n in nodes.values() if n['kind'] == 'Start' and n['cls'] == build['cls'])
    blocked = {i for i, n in nodes.items() if n['kind'] in BLOCKED_KINDS or (n['kind'] == 'Start' and i != start)}

    def utility_of(taken):
        return summarize(totals_of(nodes, taken, build), build, attr_dmg)['value']

    def bfs(taken, val):
        order = sorted(taken, key=sortkey)   # sets of ids iterate in hash order; sort so every run is identical
        dist = {t: 0 for t in order}
        gain = {t: 0.0 for t in order}
        parent = {}
        frontier = order
        while frontier:
            nxt = []
            for u in frontier:
                for w in sorted(adj[u], key=sortkey):
                    if w in blocked or w in taken:
                        continue
                    g = gain[u] + val[w]
                    if w not in dist:
                        dist[w] = dist[u] + 1
                        gain[w] = g
                        parent[w] = u
                        nxt.append(w)
                    elif dist[w] == dist[u] + 1 and g > gain[w]:
                        gain[w] = g
                        parent[w] = u
            frontier = nxt
        return dist, gain, parent

    def revalue(taken):
        w = weights(summarize(totals_of(nodes, taken, build), build, attr_dmg), build, attr_dmg)
        return {i: node_value(n, build, w) for i, n in nodes.items()}

    def take(taken, t, parent):
        while t not in taken:
            taken.add(t)
            t = parent[t]

    def fill(taken):
        """Greedy: add the best value-per-point path until no path fits the budget."""
        while len(taken) - 1 < budget:
            val = revalue(taken)
            dist, gain, parent = bfs(taken, val)
            left = budget - (len(taken) - 1)
            best = None
            for i, d in dist.items():
                if d == 0 or d > left:
                    continue
                key = (gain[i] / d, gain[i])
                if best is None or key > best[0]:
                    best = (key, i)
            if best is None or best[0][0] <= 0:
                break
            take(taken, best[1], parent)
        return taken

    taken = {start}
    keep = {start}
    # Optional landmark tax: the cheapest sockets first (they hold no value here).
    for _ in range(force_sockets):
        dist, gain, parent = bfs(taken, revalue(taken))
        socks = [i for i in dist if nodes[i]['kind'] == 'JewelSocket' and i not in taken]
        if not socks:
            break
        t = min(socks, key=lambda i: (dist[i], -gain[i]))
        if len(taken) - 1 + dist[t] > budget:
            break
        before = set(taken)
        take(taken, t, parent)
        keep |= taken - before
    fill(taken)

    def branches(taken):
        """Each removable branch: a leaf and the chain behind it up to a fork, the start or a kept node."""
        deg = {i: sum(1 for w in adj[i] if w in taken) for i in taken}
        out = []
        for leaf in sorted(taken, key=sortkey):
            if leaf in keep or deg[leaf] != 1:
                continue
            chain = [leaf]
            prev, cur = leaf, min((w for w in adj[leaf] if w in taken), key=sortkey)
            while cur not in keep and deg[cur] == 2:
                chain.append(cur)
                prev, cur = cur, min((w for w in adj[cur] if w in taken and w != prev), key=sortkey)
            out.append(chain)
        return out

    # Improvement (better than plain greedy): drop the branch that loses least per point, refill, keep if better.
    if improve:
        best_u = utility_of(taken)
        for _ in range(60):
            cands = []
            for ch in branches(taken):
                rest = taken - set(ch)
                cands.append(((best_u - utility_of(rest)) / len(ch), ch))
            cands.sort(key=lambda c: c[0])
            improved = False
            for _, ch in cands[:6]:
                trial = fill(taken - set(ch))
                u = utility_of(trial)
                if u > best_u + 1e-9:
                    taken, best_u, improved = trial, u, True
                    break
            if not improved:
                break

    totals = totals_of(nodes, taken, build)
    kinds = defaultdict(int)
    for i in taken:
        kinds[nodes[i]['kind']] += 1
    res = summarize(totals, build, attr_dmg)
    res.update(points=len(taken) - 1, notables=kinds['Notable'], attrnodes=kinds['Attribute'],
               sockets=kinds['JewelSocket'], taken=taken, totals=totals)
    return res


DEF_WEIGHTS = (0.35, 0.5, 0.7)   # the audit averages three greedy builds per case, one per defence weight W_DEF


def run_case(tree, build, budget, attr_dmg, sockets):
    """Average of the greedy builds over DEF_WEIGHTS (single greedy builds jump by 10-30% on small changes)."""
    global W_DEF
    keep_w = W_DEF
    acc = defaultdict(float)
    last = None
    for w in DEF_WEIGHTS:
        W_DEF = w
        r = greedy(*tree, build, budget, attr_dmg, sockets)
        for k in ('dmg', 'dfn', 'life', 'res', 'speed', 'attr', 'notables', 'attrnodes'):
            acc[k] += r[k] / len(DEF_WEIGHTS)
        acc['speed_capped'] += min(r['speed'], SPEED_CAP) / len(DEF_WEIGHTS)
        # survival index: main defence and life together, with the utility's weights
        acc['surv'] += math.exp(0.5 * math.log1p(r['dfn'] / 100) + W_LIFE * math.log1p(r['life'] / 100)) / len(DEF_WEIGHTS)
        last = r
    W_DEF = keep_w
    acc['taken'] = last['taken']
    return acc


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('today')
    ap.add_argument('new')
    ap.add_argument('--notable-scale', type=float, default=1.0)
    ap.add_argument('--small-scale', type=float, default=1.0)
    ap.add_argument('--stat-scale', default='', help='per-stat scale on the new tree, e.g. ArmorPercent=1.3,AttackSpeed=1.25')
    ap.add_argument('--override', action='append', default=[],
                    help='replace a new-tree node\'s stats by name or id, e.g. "Unseen Hand=EvasionPercent=25,EnergyShieldPercent=25"')
    ap.add_argument('--attr-dmg', type=float, default=0.3)
    ap.add_argument('--budgets', default='73,123')
    ap.add_argument('--sockets', default='0,0', help='sockets forced first at each budget, e.g. 3,4')
    ap.add_argument('--show', default='', help='print the notables a build takes (build name prefix; last ensemble run)')
    a = ap.parse_args()
    budgets = [int(b) for b in a.budgets.split(',')]
    socks = [int(s) for s in a.sockets.split(',')]
    today = load(a.today)
    stat_scale = {kv.split('=')[0]: float(kv.split('=')[1]) for kv in a.stat_scale.split(',') if '=' in kv}
    new = load(a.new, a.notable_scale, a.small_scale, stat_scale)
    for ov in a.override:
        name, stats = ov.split('=', 1)
        hits = [n for n in new[0].values() if n['name'] == name or n['id'] == name]
        if not hits:
            sys.exit(f'--override: no node named {name!r}')
        for n in hits:
            n['stats'] = [(kv.split('=')[0], int(kv.split('=')[1])) for kv in stats.split(',') if '=' in kv]
    print(f'Power audit, new / today. New tree: notables x{a.notable_scale}, cluster smalls x{a.small_scale}, '
          f'per stat {a.stat_scale or "none"}, {len(a.override)} overrides. Attribute damage {a.attr_dmg}% per point. '
          f'Sockets forced: {a.sockets}. Each row averages {len(DEF_WEIGHTS)} greedy builds (W_DEF {DEF_WEIGHTS}).')
    hdr = (f"{'build':32} {'pts':>4} | {'main dmg% T/N':>13} {'ratio':>6} | {'main def% T/N':>13} {'ratio':>6} | "
           f"{'life% T/N':>11} {'surv':>5} | {'res% T/N':>9} {'speed* T/N':>10} {'attr T/N':>9} {'notab T/N':>9}")
    print(hdr)
    print('-' * len(hdr))
    ratios = []
    for name, b in BUILDS.items():
        for bi, budget in enumerate(budgets):
            sk = socks[bi] if bi < len(socks) else 0
            t = run_case(today, b, budget, a.attr_dmg, sk)
            n = run_case(new, b, budget, a.attr_dmg, sk)
            rd = n['dmg'] / t['dmg'] if t['dmg'] else float('nan')
            rf = n['dfn'] / t['dfn'] if t['dfn'] else float('nan')
            rs = n['speed_capped'] / t['speed_capped'] if t['speed_capped'] else float('nan')
            rv = n['surv'] / t['surv']
            ratios.append((name, budget, rd, rf, rs, rv))
            print(f"{name:32} {budget:>4} | {t['dmg']:6.0f}/{n['dmg']:<6.0f} {rd:6.0%} | {t['dfn']:6.0f}/{n['dfn']:<6.0f} "
                  f"{rf:6.0%} | {t['life']:5.0f}/{n['life']:<5.0f} {rv:5.0%} | {t['res']:4.0f}/{n['res']:<4.0f} "
                  f"{t['speed_capped']:4.0f}/{n['speed_capped']:<5.0f} {t['attr']:4.0f}/{n['attr']:<4.0f} "
                  f"{t['notables']:4.0f}/{n['notables']:<4.0f}")
            if a.show and name.startswith(a.show):
                print('   new notables:', '; '.join(sorted(new[0][i]['name'] for i in n['taken'] if new[0][i]['kind'] == 'Notable')))
                print('   today notables:', '; '.join(sorted(today[0][i]['name'] for i in t['taken'] if today[0][i]['kind'] == 'Notable')))
    print('* speed counts only up to the 30% frame cap (MaxPassiveSpeedFrames 3 x 10%). '
          'surv = ratio of (1 + def)^0.5 x (1 + life)^0.8, main defence and life together.')
    for label, idx in (('main damage', 2), ('main defence', 3), ('survival', 5), ('speed (capped)', 4)):
        r = [x[idx] for x in ratios]
        print(f'{label:15} ratio {min(r):.0%}-{max(r):.0%}, mean {sum(r) / len(r):.0%}')
    ok = all(0.9 <= x[i] <= 1.1 for x in ratios for i in (2, 3))
    print('check 3d (main damage and main defence 90-110% on every row):', 'PASS' if ok else 'FAIL')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
