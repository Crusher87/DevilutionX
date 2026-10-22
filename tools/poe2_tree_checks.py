#!/usr/bin/env python3
"""Stage 3 checks for the POE2 Mashup passive tree: every check of design-spec §10, with the round-2 review gates
and Curtis's rulings, as one pass/fail table. Called by tools/poe2_passive_tree.py after it builds the tree; it reads
the generator's Tree and the rows it writes, never the game's TSV.

Each row: check number, what it measures, PASS / FAIL / INFO / CONTENT, the measured value, the target, a note.
FAIL rows say why in the note. CONTENT rows are work for the content pass (names, stats, lore), not layout.

Route costs (§1.3, check 2, bundles, fairness) are measured on the plain tree: the Profane roads are left out, as the
route marker never walks an untaken Profane notable. The roads' own checks (15c) and the floors they must keep
(check 6) are measured with them (Curtis 04:19: a road may make the owner's own targets cheaper).
"""

import math
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import poe2_passive_tree as G  # noqa: E402
import poe2_tree_content as C  # noqa: E402

ORDER = G.ORDER
R = G.R


class Table:
    def __init__(self):
        self.rows = []

    def add(self, num, what, ok, value, target, note=''):
        st = ok if isinstance(ok, str) else ('PASS' if ok else 'FAIL')
        self.rows.append((str(num), what, st, str(value), str(target), note))

    def text(self):
        w = [max(len(r[k]) for r in self.rows + [('#', 'check', 'result', 'measured', 'target', '')]) for k in range(5)]
        w = [min(x, cap) for x, cap in zip(w, (5, 46, 7, 44, 30))]
        head = ('#', 'check', 'result', 'measured', 'target')
        out = ['  '.join(h.ljust(x) for h, x in zip(head, w)) + '  note', '-' * 150]
        for r in self.rows:
            out.append('  '.join(str(v)[:x].ljust(x) for v, x in zip(r[:5], w)) + '  ' + r[5])
        n = {s: sum(1 for r in self.rows if r[2] == s) for s in ('PASS', 'FAIL', 'INFO', 'CONTENT')}
        out.append('-' * 150)
        out.append('PASS %(PASS)d, FAIL %(FAIL)d, INFO %(INFO)d, CONTENT %(CONTENT)d' % n)
        return '\n'.join(out)

    def markdown(self):
        def cell(v):
            return str(v).replace('|', '/').replace('\n', ' ')
        out = ['| # | Check | Result | Measured | Target | Note |', '|---|---|---|---|---|---|']
        for r in self.rows:
            out.append('| ' + ' | '.join(cell(v) for v in r) + ' |')
        return '\n'.join(out)

    def failures(self):
        return [r for r in self.rows if r[2] == 'FAIL']


def edge_len(T, e):
    pts = T.poly(e, 8)
    return sum(math.dist(p, q) for p, q in zip(pts, pts[1:]))


def ranks(lists):
    m = min(len(x) for x in lists)
    gaps = [max(x[r] for x in lists) - min(x[r] for x in lists) for r in range(m)]
    return gaps


def check(T, rows, ids, poe2_names=None, tsv_path=None, determinism=True, quick=False):
    K = Table()
    D, Dp = T.D, T.Dp
    n = len(T.pos)
    main = [i for i in range(n) if T.kind[i] != 'S']
    nots = [i for i in main if T.kind[i] == 'N']
    areas = list(ORDER) + list(G.BORDERS)
    prof = G.prof_nodes(T)
    key = T.key

    # ---- 1 counts
    cnt = {k: sum(1 for i in main if T.kind[i] == k) for k in 'stANJK'}
    K.add(1, 'main tree nodes', 1300 <= len(main) <= 1400, len(main), '1,300-1,400')
    K.add(1, 'notables (aim 240)', 225 <= cnt['N'] <= 255, cnt['N'], '225-255, aim 240',
          '' if cnt['N'] >= 240 else 'short of 240: matched fours run out of open ground; see README')
    K.add(1, 'attribute nodes', 200 <= cnt['A'] <= 235, cnt['A'], '200-235')
    K.add(1, 'travel smalls', 100 <= cnt['t'] <= 140, cnt['t'], '100-140 (start redesign)')
    K.add(1, 'sockets / shrines', cnt['J'] == 12 and cnt['K'] == 12, '%d / %d' % (cnt['J'], cnt['K']), '12 / 12')

    # ---- 2 route costs (§1.3), plain tree
    def row(c):
        d, k = D[c], key[c]
        nb, op = G.neighbours(c), G.opposite(c)
        bords = [G.border_of(c, x) for x in nb]
        style = [i for i in nots if T.role[i][0].startswith('style') and T.area[i] == c]
        dfn = [i for i in nots if T.role[i][0] == 'defence' and T.area[i] == c]
        return dict(
            cost1=sum(1 for i in d if d[i] == 1), cost2=sum(1 for i in d if d[i] == 2), openers=sorted(d[x] for x in k['N']), corners=sorted(d[x] for x in k['C']),
            style=sorted(d[i] for i in style), defence=sorted(d[i] for i in dfn), socket=d[k['J']],
            shrine1=d[k['K1']], X=d[k['X']], bsockets=sorted(d[key[b]['B']] for b in bords),
            bshrines=sorted(d[key[b]['K']] for b in bords), deep=d[k['D']],
            nb_sockets=sorted(d[key[x]['J']] for x in nb), shrine2=d[k['K2']],
            nb_notable=min(d[i] for i in nots if T.area[i] in nb and i in d),
            nb_shrines=sorted(d[key[x][kk]] for x in nb for kk in ('K1', 'K2')),
            capstones=sorted(d[x] for x in k['cap']),
            farthest=max(v for i, v in d.items() if T.area[i] == c),
            nb_start=sorted(d[key[x]['S']] for x in nb),
            sockets=sorted(d[i] for i in d if T.kind[i] == 'J'),
            op_notable=min(d[i] for i in nots if T.area[i] == op and i in d),
            op_socket=d[key[op]['J']], op_shrines=sorted(d[key[op][kk]] for kk in ('K1', 'K2')),
            op_start=d[key[op]['S']],
            n10=sum(1 for i in nots if d.get(i, 99) <= 10), n15=sum(1 for i in nots if d.get(i, 99) <= 15),
            n20=sum(1 for i in nots if d.get(i, 99) <= 20),
            nodes6=sum(1 for i in d if 1 <= d[i] <= 6 and T.kind[i] != 'S'))
    RT = {c: row(c) for c in ORDER}
    spec = [('cost1', 'nodes at 1 point (exits)', lambda v: v == 2, '2'),
            ('cost2', 'nodes at 2 points (no sideways roads)', lambda v: v == 2, '2 (start redesign)'),
            ('openers', 'opening notables', lambda v: v == [5, 5], '5, 5'),
            ('corners', 'kite corners', lambda v: v == [8, 8], '8, 8'),
            ('style', 'style notables (2 clusters x 2)', lambda v: len(v) == 4 and max(v) <= 10, '4 notables, 9 (<= 10)'),
            ('defence', 'defence cluster', lambda v: len(v) >= 1 and max(v) <= 10, '9 (<= 10)'),
            ('socket', 'own socket', lambda v: v == 10, '10'),
            ('shrine1', 'first own shrine', lambda v: 11 <= v <= 13, '12 (11-13)'),
            ('X', 'mid-ring junction', lambda v: v == 13, '13'),
            ('bsockets', 'both border sockets', lambda v: v == [15, 15], '15, 15'),
            ('bshrines', 'border shrines', lambda v: all(17 <= x <= 19 for x in v), '18 (17-19)'),
            ('deep', 'deep own socket', lambda v: 18 <= v <= 20, '19 (18-20)'),
            ('nb_sockets', 'neighbour own sockets', lambda v: v == [24, 24], '24 exactly'),
            ('shrine2', 'second own shrine', lambda v: 22 <= v <= 26, '25 (22-26)'),
            ('nb_notable', "neighbour's first notable", lambda v: v >= 25, '25 (>= 25)'),
            ('nb_shrines', 'neighbour shrines', lambda v: min(v) >= 25, '>= 25'),
            ('capstones', 'capstones', lambda v: len(v) == 2 and all(26 <= x <= 30 for x in v), '2 at 26-30'),
            ('farthest', 'farthest node of own area', lambda v: 28 <= v <= 32, '28-32'),
            ('nb_start', "neighbours' starts", lambda v: all(28 <= x <= 32 for x in v), '30 (28-32)'),
            ('op_notable', "opposite's first notable", lambda v: v >= 40, '>= 40'),
            ('op_socket', 'opposite own socket', lambda v: v >= 42, '>= 42'),
            ('op_shrines', 'opposite shrines', lambda v: min(v) >= 41, '>= 41'),
            ('op_start', 'opposite start', lambda v: v == 48, '48'),
            ('n10', 'notables within 10 points', lambda v: v >= 5, '>= 5'),
            ('n15', 'notables within 15 points', lambda v: v >= 19, '>= 19 (Curtis)'),
            ('n20', 'notables within 20 points', lambda v: v >= 34, '>= 34 (Curtis)'),
            ('nodes6', 'nodes within 6 points', lambda v: v == 12, '12 (two columns of six, start redesign)')]
    for k_, what, ok, target in spec:
        vals = {c: RT[c][k_] for c in ORDER}
        same = len({str(v) for v in vals.values()}) == 1
        good = all(ok(v) for v in vals.values())
        exact = k_ in ('cost1', 'cost2', 'openers', 'corners', 'socket', 'X', 'bsockets', 'nb_sockets', 'op_start', 'deep', 'shrine1',
                       'bshrines', 'shrine2', 'capstones', 'nb_start')
        note = '' if same else 'differs by class'
        K.add(2, what, good and (same or not exact),
              ' / '.join(str(vals[c]).replace(' ', '') for c in ORDER) if not same else str(vals['Warrior']).replace(' ', ''),
              target, note)
    socks = {c: RT[c]['sockets'] for c in ORDER}
    K.add(14, 'socket list, every class', len({str(v) for v in socks.values()}) == 1,
          ','.join(str(x) for x in socks['Warrior']), '10,15,15,19,24,24,29,29,33,33,42,45 identical')

    # ---- 2a the placing rule near the starts (start redesign 4.3, Curtis's answer 9)
    sr = T.notes.get('start_rule')
    if sr:
        nb = {c: len(sr['breaks'][c]) for c in ORDER}
        K.add('2a', 'stat pairs and stat lines within 12 points (socket zones exempt)', not any(nb.values()),
              ' / '.join(str(nb[c]) for c in ORDER), '0 every class')
        K.add('2a', '+5 nodes in the tree', sr['attributes'] == C.ATTRIBUTE_NODES, sr['attributes'],
              '%d (unchanged)' % C.ATTRIBUTE_NODES)
        per = {}
        for i in sr['singles']:
            c = T.area[i]
            per[(c, G.node_side(T, c, i))] = per.get((c, G.node_side(T, c, i)), 0) + 1
        K.add('2a', 'road singles per class side (13-30 points)', 'INFO',
              ' '.join('%s%d:%d' % (c[0], s_, per.get((c, s_), 0)) for c in ORDER for s_ in (0, 1)),
              '%d placed' % len(sr['singles']))
        K.add('2a', 'nodes moved out (corner fix + rule)', 'INFO', '%d + %d' % (len(sr['corner']), len(sr['moved'])),
              'felt smalls')
        K.add('2a', 'whole tree: touching +5 pairs / stat nodes mid-line', 'INFO', '%d / %d' % sr['whole'],
              'warnings only; the lattice pass waits on Curtis')
    first = {c: sorted(D[c][x] for x in key[c]['N']) for c in ORDER}
    K.add('2a', 'first notables of every class cost 5', all(v == [5, 5] for v in first.values()),
          ' / '.join(str(first[c]).replace(' ', '') for c in ORDER), '5, 5')

    # ---- 3 fairness (plain tree), Curtis: every rank costing <= 30 within 1
    lists = {c: sorted(D[c][i] for i in nots if i in D[c]) for c in ORDER}
    gaps = ranks([lists[c] for c in ORDER])
    low = [g for r, g in enumerate(gaps) if min(lists[c][r] for c in ORDER) <= 30]
    K.add(3, 'notable ranks costing <= 30: max gap', max(low) <= 1, max(low), '<= 1 (Curtis)',
          '%d ranks compared' % len(low))
    K.add(3, 'notable ranks, all: mean gap (max for info)', sum(gaps) / len(gaps) <= 0.5,
          '%.2f (max %d)' % (sum(gaps) / len(gaps), max(gaps)), '<= 0.5', 'Curtis: the max-gap rule binds ranks <= 30 only')
    for kd, nm in (('J', 'sockets'), ('K', 'shrines'), ('S', 'other starts')):
        ls = [sorted(D[c][i] for i in main + list(T.starts) if T.kind[i] == kd and i in D[c] and D[c][i] > 0) for c in ORDER]
        g = ranks(ls)
        K.add(3, nm + ': max rank gap', max(g) <= 1 and sum(g) / len(g) <= 0.5, max(g), '<= 1')
    g60 = ranks([x[:60] for x in lists.values()])
    K.add('3a', '60 cheapest notables: max / mean gap', max(g60) <= 1 and sum(g60) / 60 <= 0.5,
          '%d / %.2f' % (max(g60), sum(g60) / 60), '<= 1 / <= 0.5')
    listsp = {c: sorted(Dp[c][i] for i in nots if i in Dp[c]) for c in ORDER}
    gp = ranks([listsp[c] for c in ORDER])
    lowp = [g for r, g in enumerate(gp) if min(listsp[c][r] for c in ORDER) <= 30]
    K.add(3, 'same, with the Profane roads taken', 'INFO', '%d (ranks <= 30) / %d all' % (max(lowp), max(gp)),
          'for information')

    # ---- 3b bundles (exact Steiner), 3c travel parity, 3d attribute share
    bund = {}
    for c in ORDER:
        k = key[c]
        s = k['S']
        b1, b2 = (G.border_of(c, x) for x in G.neighbours(c))
        a_ = G.steiner(T, s, list(k['N']))
        b_ = G.steiner(T, s, list(k['N']) + [k['J']])
        l50 = [*k['N'], k['J'], k['D'], k['K1'], key[b1]['B'], key[b1]['K']]
        l50b = [*k['N'], k['J'], k['D'], k['K1'], key[b2]['B'], key[b2]['K']]
        c_ = min(G.steiner(T, s, l50), G.steiner(T, s, l50b))
        d_ = G.steiner(T, s, [*k['N'], k['J'], k['D'], k['K1'], k['K2'], *k['cap'], key[b1]['B'], key[b1]['K']])
        e_cost, e_tree = G.steiner(T, s, l50 + [k['K2'], key[b2]['B'], key[b2]['K']], want_tree=True)
        tree = l123(T, s, e_tree, {c, b1, b2})
        bund[c] = dict(a=a_, b=b_, c=c_, d=d_, L100=e_cost,
                       L100_travel=tuple(sum(1 for i in e_tree if T.kind[i] == 't' and T.stat[i] == st) for st in ('Life', 'Res', 'Mana')),
                       L123=len(tree), L123_notables=sum(1 for i in tree if T.kind[i] == 'N'),
                       L123_attr=sum(1 for i in tree if T.kind[i] == 'A') / max(1, len(tree)),
                       L123_travel=tuple(sum(1 for i in tree if T.kind[i] == 't' and T.stat[i] == st) for st in ('Life', 'Res', 'Mana')))
    T.bundles = bund
    for k_, what, tol, extra in (('a', '(a) both openers', 1, None), ('b', '(b) openers + own socket', 1, None),
                                 ('c', '(c) level-50 bundle', 1, 45), ('d', '(d) own landmarks + one border', 1, None),
                                 ('L100', 'L100 bundle', 2, None)):
        v = [bund[c][k_] for c in ORDER]
        ok = max(v) - min(v) <= tol and (extra is None or max(v) <= extra)
        K.add('3b', what + ' (exact Steiner)', ok, '/'.join(map(str, v)),
              'equal +-%d%s' % (tol, ', <= 45' if extra else ''))
    for k_, lab in (('L100_travel', 'L100'), ('L123_travel', 'L123')):
        v = [bund[c][k_] for c in ORDER]
        spr = [max(x[s] for x in v) - min(x[s] for x in v) for s in range(3)]
        K.add('3c', 'travel parity on %s (Life,Res,Mana)' % lab, max(spr) <= 1,
              ' / '.join('%d,%d,%d' % x for x in v), 'each within +-1')
    life = [bund[c]['L123_travel'][0] for c in ORDER]
    K.add('3c', 'Life travel smalls on the L123 build', min(life) >= 2, '/'.join(map(str, life)), '>= 2 per class (Curtis)')
    att = [bund[c]['L123_attr'] for c in ORDER]
    K.add('3d', 'attribute share of the L123 build', max(att) <= 0.30, '/'.join('%.0f%%' % (100 * a) for a in att), '<= 30%')
    K.add('3d', 'power of the reference builds', 'CONTENT', 'not measured', '90-110% of today', 'power audit pending')
    # 3e / 3f
    for c in ORDER:
        d = D[c]
        dfn = sum(1 for i in nots if d.get(i, 99) <= 10 and T.role[i][0] in ('defence',) or (i in key[c]['N'][:1]))
        off = sum(1 for i in nots if d.get(i, 99) <= 10 and T.role[i][0] in ('style',) or (i in key[c]['N'][1:]))
        RT[c]['def_off'] = (dfn, off)
    K.add('3e', 'defence / offence notables within 10', all(RT[c]['def_off'][0] >= 2 and RT[c]['def_off'][1] >= 2 for c in ORDER),
          ' '.join('%d/%d' % RT[c]['def_off'] for c in ORDER), '>= 2 / >= 2', 'by role; within 15 is content')
    fork = all(len(RT[c]['style']) == 4 and max(RT[c]['style']) <= 10 for c in ORDER)
    K.add('3f', 'weapon/skill fork within 10', fork, 'two style clusters, %s' % RT['Warrior']['style'], '2 families <= 10')

    # ---- 4 border equality, 5 area ownership
    beq = []
    for b, (_, c1, c2) in G.BORDERS.items():
        for i in [key[b]['B'], key[b]['K']] + [i for i in nots if T.area[i] == b and i not in prof]:
            beq.append(abs(D[c1][i] - D[c2][i]))
    K.add(4, 'border socket, shrine, notables: neighbour gap', max(beq) <= 1, 'max %d over %d' % (max(beq), len(beq)), '<= 1')
    mism = sum(1 for i in main if i in D['Warrior'] and T.area[i] != G.area_by_cost(D, i))
    K.add(5, 'areas by point cost', mism == 0, '%d mismatches' % mism, '0')

    # ---- 6 no cheap crossings (with the Profane roads). The four class roads of design v8 break a few floors on
    # purpose (design-v8 §1.4, approved by Curtis 2026-10-18): they are listed, and the check holds for the rest.
    def floor_viol(Dx):
        out = []
        for c in ORDER:
            for x in ORDER:
                if x == c:
                    continue
                opp = x == G.opposite(c)
                for i in nots:
                    if T.area[i] != x or i in prof:
                        continue
                    # a class's first notables sit on its columns, which the road to the neighbour runs through
                    # (start redesign: notable at 5, neighbouring start at 30), so they hold the crossover's 25
                    need = 40 if opp else (25 if T.role[i][0].startswith('crossover') or i in key[x]['N'] else 26)
                    if Dx[c].get(i, 99) < need:
                        out.append((c, i, Dx[c].get(i), need))
        return out
    viol = floor_viol(G.all_costs(T, profane=True, roads=False)) if getattr(T, 'road', None) else floor_viol(Dp)
    K.add(6, 'floors with roads: nb notable 25/26, opp 40', not viol, '%d violations' % len(viol), '0',
          'the border roads; the class roads are listed below')
    if getattr(T, 'road', None):
        rv = [v for v in floor_viol(Dp) if v[:2] not in {w[:2] for w in viol}]
        K.add(6, 'floors the class roads break (design-v8 §1.4)', 'INFO', '%d' % len(rv), 'listed',
              '; '.join('%s to %s %d < %d' % (c, T.name[i] or i, d, n) for c, i, d, n in rv))
    lm = []
    for c in ORDER:
        for x in ORDER:
            if x == c:
                continue
            opp = x == G.opposite(c)
            lm.append(Dp[c][key[x]['S']] >= (48 if opp else 28))
            lm.append(Dp[c][key[x]['J']] >= (42 if opp else 24))
            lm.append(min(Dp[c][key[x]['K1']], Dp[c][key[x]['K2']]) >= (41 if opp else 25))
    K.add(6, 'with roads: nb start >= 28, opp 48, sockets, shrines', all(lm), '%d of %d' % (sum(lm), len(lm)), 'all')
    # 6a crossover prize: neighbour's first notable is the crossover facing the border, at 25 (others 26+); the
    # neighbour's own first notable on the column facing this class may also sit at 25 (start redesign)
    ok6a = []
    for c in ORDER:
        for x in G.neighbours(c):
            cands = sorted((D[c][i], i) for i in nots if T.area[i] == x and i in D[c])
            first = [i for v, i in cands if v == cands[0][0]]
            cross = [i for i in first if T.role[i][0].startswith('crossover')]
            good = cands[0][0] == 25 and len(cross) == 1 and all(i in cross or i in key[x]['N'] for i in first)
            ok6a.append(good)
    K.add('6a', "neighbour's notables at 25: its crossover (and its first notable)", all(ok6a), '%d of 8 sides' % sum(ok6a), '8 of 8')

    # ---- 7 winding
    wind = []
    for a in range(4):
        for b in range(a + 1, 4):
            s, t = key[ORDER[a]]['S'], key[ORDER[b]]['S']
            p = G.shortest_path_nodes(T, s, t)
            L = sum(G.math.dist(T.pos[u], T.pos[v]) for u, v in zip(p, p[1:]))
            wind.append(L / math.dist(T.pos[s], T.pos[t]))
    K.add(7, 'winding (start to start)', min(wind) >= 3.5, '%.1f-%.1f' % (min(wind), max(wind)), '>= 3.5')

    # ---- 8 symmetry
    grid = {}
    for i in main:
        p = T.pos[i]
        grid.setdefault((int(p[0] // 40), int(p[1] // 40)), []).append(i)

    def near_same(p, kind, tol):
        gx, gy = int(p[0] // 40), int(p[1] // 40)
        rr = int(tol // 40) + 1
        for dx in range(-rr, rr + 1):
            for dy in range(-rr, rr + 1):
                for j in grid.get((gx + dx, gy + dy), []):
                    if T.kind[j] == kind and math.dist(T.pos[j], p) <= tol:
                        return True
        return False
    sym = {}
    for rot in (90, 180, 270, 37, 143):
        sym[rot] = sum(1 for i in main if near_same(G.pol(G.rad(T.pos[i]), G.ang_of(T.pos[i]) + rot), T.kind[i], 18)) / len(main)
    s1, s0 = max(sym[r] for r in (90, 180, 270)), max(sym[37], sym[143])
    K.add(8, 'node symmetry minus control', s1 - s0 <= 0.06, '%.3f - %.3f = %.3f' % (s1, s0, s1 - s0), '<= 0.06')
    lmk = [(T.kind[i], T.pos[i]) for i in main if T.kind[i] in 'JK' or T.tag[i] in ('corner', 'junction')]
    lmk += [('set', cl['centre']) for cl in T.clusters if cl['tag'] == 'set']
    for tolr, lab in ((0.06, '6% R'), (0.04, '4% R (Curtis)')):
        ls = {}
        for rot in (90, 180, 270, 37, 143):
            hit = 0
            for kd, p in lmk:
                q = G.pol(G.rad(p), G.ang_of(p) + rot)
                hit += any(k2 == kd and math.dist(q, p2) <= tolr * R for k2, p2 in lmk)
            ls[rot] = hit / len(lmk)
        a1, a0 = max(ls[r] for r in (90, 180, 270)), max(ls[37], ls[143])
        K.add('8b', 'landmark symmetry at ' + lab, a1 <= a0 + 0.10 if tolr == 0.04 else 'INFO',
              '%.3f vs control %.3f' % (a1, a0), '<= control + 0.10',
              '' if tolr == 0.04 else 'spec §B 8: accepted failing at 6% R')
    iou = raster_iou(T)
    K.add('8c', 'raster symmetry (lattice IoU 90/180/270 over 37)', iou[2] <= 1.3, '%.2f' % iou[2], '<= 1.3')
    K.add('8d', 'lattice overlay image', 'INFO', 'lattice-overlay.png', 'written')
    ex = {}
    for c in ORDER:
        s = T.pos[key[c]['S']]
        ex[c] = tuple(sorted(round(G.adiff(G.heading(s, T.pos[e]), G.CLASSES[c] + 180 if False else G.ang_of(s)), 0)
                             for e in key[c]['E']))
    distinct = all(max(abs(a - b) for a, b in zip(ex[c1], ex[c2])) >= 4 for i1, c1 in enumerate(ORDER) for c2 in ORDER[i1 + 1:])
    K.add('8e', 'exit angles distinct per class start', distinct,
          ' '.join('%s%s' % (c[0], tuple(int(x) for x in ex[c])) for c in ORDER), 'every pair differs >= 4 deg')

    # ---- 9 symmetry fade (mirror about the class axis by band)
    bands = {'1-5': ((1, 5), (0.65, 0.85)), '6-10': ((6, 10), (0.25, 0.5)), '11-15': ((11, 15), (0, 0.2)), '16+': ((16, 99), (0, 0.15))}
    mir = {}
    for bn, ((lo, hi), _) in bands.items():
        tot = hit = 0
        for c, a in G.CLASSES.items():
            if c == 'Rogue' and lo <= 10:
                continue      # the Rogue's drawn bow is the one start that is not mirrored (start redesign 4.2)
            for i, v in D[c].items():
                if lo <= v <= hi and T.area[i] == c and T.kind[i] != 'S':
                    p = T.pos[i]
                    tot += 1
                    hit += near_same(G.pol(G.rad(p), 2 * a - G.ang_of(p)), T.kind[i], 15)
        mir[bn] = hit / max(1, tot)
    K.add(9, 'mirror match by band 1-5/6-10/11-15/16+', all(bands[b][1][0] <= mir[b] <= bands[b][1][1] for b in bands),
          ' / '.join('%.2f' % mir[b] for b in bands), '0.65-0.85 / 0.25-0.5 / <= 0.2 / <= 0.15')

    # ---- 10 runs
    worst = same_stat_run(T)
    K.add(10, 'identical travel smalls in a row', worst <= 2, worst, '<= 2 (Curtis)')
    bad = G.run_violations(T)
    K.add('10a', 'straight runs (4+ in 12 deg) off the spine, or > 7', not [p for p in bad if len(p) >= 4 and
                                                                           sum(math.dist(T.pos[a], T.pos[b]) for a, b in zip(p, p[1:])) >= 0],
          len(bad), '0 (spine runs <= 7 nodes; near-straight <= 260 units)')
    lat_links = [e for e in T.edges if G.is_lattice(T, e[0]) and G.is_lattice(T, e[1])]
    straight = sum(1 for e in lat_links if abs(e[2]) < 1e-6) / len(lat_links)
    K.add('10b', 'lattice links straight', straight >= 0.999, '%.0f%%' % (100 * straight), '100% (Curtis: bend only at nodes)')
    longest = max(math.dist(T.pos[e[0]], T.pos[e[1]]) for e in lat_links)
    K.add('10b', 'longest lattice link', longest <= 300, '%.0f' % longest, '<= 300 (Curtis)')
    near = G.near_straight_runs(T)
    K.add('10b', 'longest near-straight lattice run', max([L for L, _ in near] + [0]) <= 260,
          '%.0f' % max([L for L, _ in near] + [0]), '<= 260 units (Curtis)')
    cols_ok = 0
    for c in ORDER:
        for col in key[c]['col']:
            if any(T.cl_of[j] >= 0 and T.clusters[T.cl_of[j]]['tag'] == 'style' for j in T.adj[col[5]]):
                cols_ok += 1
    K.add('10c', 'opening columns with their pair off the 6-point small', cols_ok == 8, '%d of 8' % cols_ok, '8 of 8')

    # ---- 11 lead-ins, loops, pass-through
    latt = {i for i in range(n) if G.is_lattice(T, i)}
    dl = G.steps_from(T, latt)
    aj = {i for i in range(n) if T.kind[i] in 'AJ'}
    da = G.steps_from(T, aj)
    nn = [i for i in nots if i not in prof]
    lead = []
    for i in nn:
        if T.tag[i] == 'open':
            lead.append(3)
        elif T.cl_of[i] >= 0:
            cl = T.clusters[T.cl_of[i]]
            own = set(cl['ids'])
            ent = [x for x in cl['ids'] if any(j not in own for j in T.adj[x])]
            dd = G.steps_from(T, set(ent))
            lead.append(min(dd.get(i, 99), dl.get(i, 99) - 1))
        else:
            lead.append(dl.get(i, 99) - 1)
    lead_s = sorted(lead)
    med = lead_s[len(lead_s) // 2]
    one = sum(1 for x in lead if x == 1) / len(lead)
    zero = sum(1 for x in lead if x <= 0)
    K.add(11, 'lead-ins: median / share with 1 / with 0', med == 3 and one <= 0.10 and zero == 0,
          '%d / %.0f%% / %d' % (med, 100 * one, zero), '3 / <= 10% / 0', '2-4 for %.0f%%' % (100 * sum(1 for x in lead if 2 <= x <= 4) / len(lead)))
    a35 = sum(1 for i in nn if 3 <= da.get(i, 0) <= 5) / len(nn)
    K.add('11a', 'notables 3-5 steps from A or socket', a35 >= 0.65, '%.0f%%' % (100 * a35), '>= 65% (start redesign)')
    loops = 0
    mand = 0
    reach_all = set()
    for c in ORDER:
        reach_all |= set(D[c])
    for i in nn:
        nb_ = sorted(T.adj[i])
        if len(nb_) >= 2:
            d0 = G.steps_from(T, {nb_[0]}, block={i})
            loops += any(x in d0 for x in nb_[1:])
            r = set()
            for s in T.starts:
                r |= set(G.steps_from(T, {s}, block={i} | prof))
            mand += len(reach_all - r - {i} - prof) > 0
    lp = loops / len(nn)
    K.add('11b', 'notables on a loop', 0.30 <= lp <= 0.55, '%.0f%%' % (100 * lp), '30-55% (start redesign)')
    pt = (mand + len([i for i in nots if i in prof])) / len(nots)
    K.add('11c', 'mandatory pass-through notables (Profane incl.)', pt <= 0.05, '%.1f%%' % (100 * pt), '<= 5%')
    nnl = sum(1 for a, b, _, _ in T.edges if T.kind[a] == 'N' and T.kind[b] == 'N')
    K.add('11d', 'notable-to-notable links', nnl == 0, nnl, '0')

    # ---- 12 motif mix (§5.1, Curtis's rulings)
    cls_ = [cl for cl in T.clusters if cl['tag'] != 'profane']
    nc = len(cls_)

    def share(f):
        return sum(1 for cl in cls_ if f(cl)) / nc
    MOT = G.MOTIFS
    mix = [('generic shapes', share(lambda cl: not MOT[cl['name']]['diablo']), lambda v: v >= 0.5, '>= 50%'),
           ('Diablo shapes', share(lambda cl: MOT[cl['name']]['diablo']), lambda v: 0.30 <= v <= 0.40, '30-40%'),
           ('spur family (Stub + Spur Arc)', share(lambda cl: cl['name'] in G.SPUR_FAMILY), lambda v: v <= 0.30, '<= 30% (Curtis)'),
           ('pictograms outside set pieces', share(lambda cl: cl['name'] in G.PICTOGRAMS and cl['tag'] != 'set'), lambda v: v <= 0.15, '<= 15%'),
           ('threaded (orbit or pass-through)', share(lambda cl: cl['mode'] in ('orbit', 'through')), lambda v: v >= 0.40, '>= 40%'),
           ('on a stalk', share(lambda cl: cl['mode'] == 'stalk'), lambda v: v <= 0.35, '<= 35%'),
           ('off another cluster', share(lambda cl: cl['mode'] == 'off'), lambda v: 0.15 <= v <= 0.25, '15-25% (Curtis: <= 20-25%)'),
           ('asymmetric variants', share(lambda cl: G.is_asym(cl['var'])), lambda v: v >= 0.5, '>= 50%'),
           ('big (orbits 4-6)', share(lambda cl: cl['size'] == 'big'), lambda v: v >= 0.15, '>= 15%'),
           ('tiny (3-4 nodes)', share(lambda cl: cl['size'] == 'tiny'), lambda v: v >= 0.15, '>= 15%')]
    for what, v, ok, target in mix:
        K.add(12, what, ok(v), '%.0f%%' % (100 * v), target)
    top = max((share(lambda cl, nm=nm: cl['name'] == nm), nm) for nm in MOT if nm not in G.SPUR_FAMILY)
    K.add(12, 'any other single motif', top[0] <= 0.12, '%s %.0f%%' % (top[1], 100 * top[0]), '<= 12%')
    two = share(lambda cl: [T.kind[i] for i in cl['ids']].count('N') >= 2 or G.loop_shape(cl['name']))
    K.add(12, 'two-notable or loop shapes', 'INFO', '%.0f%%' % (100 * two), "Curtis: fill with these")
    depth = max_off_depth(T)
    K.add(12, 'off-cluster chain depth', depth <= 1, depth, '1 (Curtis)')
    # the set pieces and the style pairs beside the columns are placed on purpose, not by the palette
    out_pal = sum(1 for cl in cls_ if MOT[cl['name']]['diablo'] and cl['area'] in G.PALETTE and cl['name'] not in G.PALETTE[cl['area']]
                  and cl['tag'] not in ('set', 'style'))
    K.add(12, 'Diablo shapes outside the area palette', out_pal == 0, out_pal, '0')
    sets = {a: [cl for cl in T.clusters if cl['tag'] == 'set' and cl['area'] == a] for a in areas}
    K.add('12a', 'set pieces: two per area', all(len(v) >= 2 for v in sets.values()),
          ' '.join(str(len(v)) for v in sets.values()), '2 per area')
    K.add('12b', 'reuse rule', 'INFO', 'enforced in the placer', 'copies 110+ apart, no axis mirror')
    # ---- 13 attribute web
    nearc = {}
    for c in ORDER:
        for i, v in D[c].items():
            nearc[i] = min(nearc.get(i, 99), v)
    # start redesign: the +5 nodes leave the first 12 points and come back as road singles 13 to 30 points out
    tgt = [((1, 5), (0.15, 0.25)), ((6, 10), (0.18, 0.30)), ((11, 15), (0.12, 0.22)), ((16, 20), (0.13, 0.24)),
           ((21, 25), (0.10, 0.18)), ((26, 99), (0.02, 0.08))]
    vals = []
    okb = True
    for (lo, hi), (a, b) in tgt:
        idsb = [i for i in main if lo <= nearc.get(i, 99) <= hi]
        v = sum(1 for i in idsb if T.kind[i] == 'A') / max(1, len(idsb))
        vals.append(v)
        okb &= a <= v <= b
    K.add(13, 'attribute share by band', okb, ' '.join('%.0f' % (100 * v) for v in vals), '15-25/18-30/12-22/13-24/10-18/2-8% (start redesign)')
    attrs = [i for i in main if T.kind[i] == 'A']
    far = sum(1 for i in attrs if nearc.get(i, 0) >= 11) / len(attrs)
    deg3 = sum(1 for i in attrs if len(T.adj[i]) >= 3) / len(attrs)
    K.add(13, 'A nodes at 11+ points / degree >= 3', far >= 0.55 and deg3 >= 0.55, '%.0f%% / %.0f%%' % (100 * far, 100 * deg3), '>= 55% / >= 55%')
    grp = attr_groups(T, attrs)
    K.add(13, 'A nodes in linked groups of 2-5', grp >= 0.30, '%.0f%%' % (100 * grp), '>= 30%')
    junk = sorted({T.kind[i] for i in range(n) if G.is_lattice(T, i) and T.kind[i] != 'S' and i not in getattr(T, 'felt', ())
                   and sum(1 for j in T.adj[i] if G.is_lattice(T, j)) >= 3})
    K.add(13, 'lattice junctions are A or socket', set(junk) <= {'A', 'J'}, ''.join(junk), 'A, J only')
    share_aj = []
    zone = G.START_RULE_POINTS
    for a in range(4):
        for b in range(4):
            if a != b:
                # past the 12 points round each start, where no two +5 nodes may touch (start redesign 4.3)
                p = [i for i in G.shortest_path_nodes(T, key[ORDER[a]]['S'], key[ORDER[b]]['S'])
                     if min(D[c].get(i, 999) for c in ORDER) > zone]
                share_aj.append(sum(1 for i in p if T.kind[i] in 'AJ') / max(1, len(p)))
    K.add(13, 'A or socket share of start-to-start routes', min(share_aj) >= 0.55, '%.0f%%' % (100 * min(share_aj)), '>= 55% past 12 points from any start')
    ent = ent_aj = 0
    for cl in cls_:
        own = set(cl['ids'])
        for i in own:
            for j in T.adj[i]:
                if j not in own and j not in prof:
                    ent += 1
                    ent_aj += T.kind[j] in 'AJ'
    K.add('13a', 'cluster entrances on A or socket', ent_aj / ent >= 0.60, '%.0f%%' % (100 * ent_aj / ent), '>= 60%')
    # ---- 14 sockets
    san = sorted(sum(1 for j in T.adj[i] if T.kind[j] == 'A') for i in main if T.kind[i] == 'J')
    K.add(14, 'attribute neighbours per socket', min(san) >= 3 and max(san) <= 4, '%d-%d' % (san[0], san[-1]), '3-4')
    jr = []
    for name in C.JEWEL_SOCKETS:
        j = key[name[:-5]]['D'] if name.endswith(' deep') else (key[name]['J'] if name in G.CLASSES else key[name]['B'])
        jr.append(sum(1 for i in nots if math.dist(T.pos[i], T.pos[j]) < 0.13 * R))
    own_ = [jr[C.JEWEL_SOCKETS.index(c)] for c in ORDER]
    brd_ = [jr[C.JEWEL_SOCKETS.index(b)] for b in G.BORDERS]
    deep_ = [jr[C.JEWEL_SOCKETS.index(c + ' deep')] for c in ORDER]
    par = all(max(x) - min(x) <= 1 for x in (own_, brd_, deep_))
    K.add('14a', 'notables in each jewel radius (0.13 R)', min(jr) >= 3 and max(jr) <= 5 and par,
          ','.join(map(str, jr)), '3-5 each; same role within 1', 'socket order as JEWEL_SOCKETS')
    # ---- 15 routes
    eq = equal_routes(T)
    K.add(15, 'equal routes per neighbour pair', eq[0] <= 1 and eq[1] >= 4, '+%d points, %d nodes differ' % eq, '<= +1, >= 4 nodes')
    sc = T.notes.get('scenic', {})
    scok = len(sc) == 4 and all(2 <= v[3] <= 4 for v in sc.values())
    K.add('15a', 'scenic border clusters: detour', scok, ' '.join('%s+%d' % (b.split()[0], v[3]) for b, v in sc.items()), '+2 to +4, one per border')
    K.add('15b', 'braids', 'INFO', 'by construction', 'long strand +1')
    pr = T.notes.get('profane', {})
    detail = []
    okp = len(pr) == 2
    for a, v in pr.items():
        sv = v['saving']
        detail.append('%s %s' % (a.split()[0][:4], '/'.join(map(str, sv))))
        okp &= all(2 <= x <= 10 for x in sv)
    K.add('15c', 'border Profane roads: savings', okp, ', '.join(detail) or 'none',
          'the two live borders, 2-10 (design-v8 §1.9)')
    roads = road_check(T)
    K.add('15c', 'Profane Roads (design v8): one-way, 8 nodes, toll r3, pact r6', roads[0], roads[1], 'all four',
          'door and far gate costs from each start: research/overnight-2026-10-18/roads-measure')
    K.add('15c', 'Profane Roads pass over links (bridges)', 'INFO', len(G.crossings(T, bridges=True)), '')
    cross = profane_cross_check(T)
    K.add('15c', 'border Profane roads cross the area (sides of the axis)', cross[0] == len(pr) and len(pr) > 0,
          '%d of %d' % cross, 'every road', 'a road may not cross a link, so it meets the axis at one node')
    K.add('15d', 'route marker avoids untaken Profane notables', 'INFO', 'game side', 'Stage 4 test')
    # ---- 16 curves and link lengths
    def curved(e):
        return abs(e[2]) > 0.04
    clu = [e for e in T.edges if not (G.is_lattice(T, e[0]) and G.is_lattice(T, e[1]))]
    tot = sum(curved(e) for e in T.edges) / len(T.edges)
    cc = sum(curved(e) for e in clu) / len(clu)
    K.add(16, 'curved links: total / clusters', 0.50 <= tot <= 0.62 and cc >= 0.70, '%.0f%% / %.0f%%' % (100 * tot, 100 * cc), '50-62% / >= 70%')
    ll = sorted(edge_len(T, e) for e in lat_links)
    cl_ = sorted(edge_len(T, e) for e in clu)
    al = sorted(edge_len(T, e) for e in T.edges)
    med_l, med_c = ll[len(ll) // 2], cl_[len(cl_) // 2]
    mean_l = sum(ll) / len(ll)
    cv = math.sqrt(sum((x - mean_l) ** 2 for x in ll) / len(ll)) / mean_l
    K.add('16a', 'lattice median link / ratio to clusters', med_l >= 85 and med_l >= 2 * med_c, '%.0f / %.2f' % (med_l, med_l / med_c), '>= 85 / >= 2')
    K.add('16a', 'R / median link, lattice CV', R / al[len(al) // 2] >= 26 and cv >= 0.25, '%.1f, %.2f' % (R / al[len(al) // 2], cv), '>= 26, >= 0.25')
    # ---- 17 geometry
    geo = geometry(T)
    K.add(17, 'link crossings', geo['cross'] == 0, geo['cross'], '0')
    K.add(17, 'node spacing / notable spacing', geo['gap'] >= 24 and geo['ngap'] >= 38, '%.1f / %.1f' % (geo['gap'], geo['ngap']), '>= 24 / >= 38')
    K.add(17, 'link clearance', geo['clear'] >= 16, '%.1f' % geo['clear'], '>= 16')
    K.add(17, 'links per node / max radius / unreachable', geo['deg'] <= 5 and geo['rmax'] <= R and geo['unreach'] == 0,
          '%d / %.0f / %d' % (geo['deg'], geo['rmax'], geo['unreach']), '<= 5 / <= 1200 / 0')
    # ---- 18 weapon sets
    ex_ok = all(T.kind[min(T.adj[key[c]['S']], key=lambda j: ids[j])] in 's' for c in ORDER)
    K.add(18, "each start's exits are smalls", ex_ok, 'yes' if ex_ok else 'no', 'smalls')
    # ---- 19 kites and sockets
    def parea(poly):
        return abs(sum(p[0] * q[1] - q[0] * p[1] for p, q in zip(poly, poly[1:] + poly[:1]))) / 2
    ka = {c: parea(T.kites[c]) for c in ORDER}
    apex = {c: G.adiff(G.ang_of(T.pos[key[c]['J']]), G.CLASSES[c]) for c in ORDER}
    bang = {b: G.adiff(G.ang_of(T.pos[key[b]['B']]), 45 + 90 * k_) for k_, b in enumerate(G.BORDERS)}
    deep = sorted(G.rad(T.pos[key[c]['D']]) for c in ORDER)
    K.add(19, 'largest kite / smallest', max(ka.values()) / min(ka.values()) >= 1.3, '%.2f' % (max(ka.values()) / min(ka.values())), '>= 1.3')
    K.add(19, 'apex offsets (deg)', all(5 <= abs(v) <= 8 for v in apex.values()), ' '.join('%+.1f' % apex[c] for c in ORDER), '5-8, mixed signs')
    K.add(19, 'border sockets off the diagonal (deg)', all(5 <= abs(v) <= 12 for v in bang.values()),
          ' '.join('%+.1f' % v for v in bang.values()), '5-12')
    K.add(19, 'deep sockets radius spread', deep[-1] - deep[0] >= 0.05 * R, '%.0f-%.0f' % (deep[0], deep[-1]), '>= 60 apart')
    quad = [T.pos[key[b]['B']] for b in G.BORDERS]
    sides = [math.dist(quad[k_], quad[(k_ + 1) % 4]) for k_ in range(4)]
    r1, r2 = max(sides[0], sides[2]) / min(sides[0], sides[2]), max(sides[1], sides[3]) / min(sides[1], sides[3])
    K.add(19, 'border-socket quadrilateral: opposite sides', max(r1, r2) >= 1.2, '%.2f, %.2f' % (r1, r2), 'not a near-rectangle (>= 1.2)')
    lad = ladder_shapes(T)
    K.add(19, 'eight ladders differ (length, width)', lad[0], lad[1], 'every pair differs >= 10%')
    # ---- 20 rings
    rg = rings(T)
    K.add(20, 'ring jogs at junctions / gap / pinches', rg['jog'] >= 30 and rg['max'] > 220 and 2 <= rg['pinch'] <= 5,
          '%d / %d-%d / %d' % (rg['jog'], rg['min'], rg['max'], rg['pinch']), '>= 30 / over 220 somewhere / 2-5')
    # ---- 21 shrines, 22 capstones, 23 area sizes, 24 islands
    nbs = {c: tuple(sorted(D[c][key[x][kk]] for x in G.neighbours(c) for kk in ('K1', 'K2'))) for c in ORDER}
    s2 = [(G.rad(T.pos[key[c]['K2']]) / R, D[c][key[c]['K2']]) for c in ORDER]
    K.add(21, 'neighbour shrine lists; second shrine radius/cost', len(set(nbs.values())) == 1 and all(0.75 <= r <= 0.85 and 22 <= d <= 26 for r, d in s2),
          '%s; %s' % (list(nbs['Warrior']), ' '.join('%.2f/%d' % x for x in s2)), 'same; 0.75-0.85 R, 22-26')
    caps = {c: sorted(D[c][x] for x in key[c]['cap']) for c in ORDER}
    K.add(22, 'capstones: 2 per class at 26-30, one per side', all(len(v) == 2 and all(26 <= x <= 30 for x in v) for v in caps.values()),
          ' '.join('%s' % caps[c] for c in ORDER).replace(' ', ''), '26-30')
    size = {a: sum(1 for i in main if T.area[i] == a) for a in areas}
    cs = [size[c] for c in ORDER]
    bs = [size[b] for b in G.BORDERS]
    K.add(23, 'area sizes: classes / borders', spread(cs) <= 0.10 and spread(bs) <= 0.15,
          '%s / %s' % ('-'.join(map(str, cs)), '-'.join(map(str, bs))), '+-10% / +-15%')
    isl = islands_check()
    K.add(24, 'islands: in the sanctum, 38 apart, roots apart', isl[0], isl[1], '<= 149 / >= 38 / >= 38')
    # ---- 25 determinism
    if determinism and tsv_path:
        same, note = determinism_check(tsv_path, quick, len(getattr(T, 'fill_scores', [])) or 1)
        K.add(25, 'byte-identical TSV, other PYTHONHASHSEED', same, 'identical' if same else 'differs', 'identical', note)
    else:
        K.add(25, 'determinism', 'INFO', 'not run', 'identical', 'run without --no-determinism')
    # ---- 26 names
    nm = names_check(T, poe2_names)
    for what, ok, val, target, note in nm:
        K.add(26, what, ok, val, target, note)
    # ---- 27 notable content
    for what, ok, val, target, note in content_check(T):
        K.add(27, what, ok, val, target, note)
    # ---- 28 arc data
    for what, ok, val, target, note in arc_check(T, rows, ids):
        K.add(28, what, ok, val, target, note)
    # ---- fun gates
    style_ok = all(len([i for i in (T.notes['style'].get((c, s_)) or []) if T.kind[i] == 'N']) == 2 for c in ORDER for s_ in (0, 1))
    shapes = sorted({T.clusters[T.cl_of[ids_[0]]]['name'] for (c, s_), ids_ in T.notes['style'].items() if ids_})
    K.add('F1', 'style clusters hold 2 notables each', style_ok, ', '.join(shapes), 'Fork or Horns, 2 notables (Curtis)')
    fr = {k_: v for k_, v in getattr(T, 'family_roads', {}).items()}
    fr_ok = len(fr) == 8 and all(len([i for i in v if T.tag[i] != 'capstone']) >= 4 and T.tag[v[-1]] == 'capstone' for v in fr.values())
    K.add('F2', 'family roads: >= 4 notables, end at the capstone', fr_ok,
          ' '.join(str(len(v) - 1) for v in fr.values()), '>= 4 per side, 8 sides')
    cw = [(c, [i for i in nots if T.area[i] == c and T.role[i][0] == 'crossover-weapon']) for c in ORDER]
    cw_ok = all(len(v) == 1 and 13 <= D[c][v[0]] <= 17 for c, v in cw)
    K.add('F3', 'one crossover per class enables a second weapon', cw_ok,
          ' '.join('%s:%s' % (c[0], [D[c][i] for i in v]) for c, v in cw), 'one each, ~15 own points')
    ph = sum(1 for i in nots if T.name[i].startswith('<'))
    K.add('C1', 'placeholder notables for the content pass', 'CONTENT' if ph else True, ph, 'named by the content pass', 'see notable_roles.tsv')
    # every content notable is placed exactly once, unless C.CUT cuts it on purpose
    cut = {nm for lst in getattr(C, 'CUT', {}).values() for nm in lst}
    want = [nm for lst in list(C.CLUSTERS.values()) + list(C.BORDER_CLUSTERS.values()) for f in lst for nm, _, _ in f['notables']]
    placed = [T.name[i] for i in nots]
    bad = [nm for nm in want if (placed.count(nm) != (0 if nm in cut else 1))]
    K.add('C2', 'content notables placed once (or cut on purpose)', not bad, len(bad), '0', ', '.join(bad[:6]))
    T.check_table = K
    head = summary(T, K)
    return head + '\n\n' + K.text() + '\n\n' + details(T, RT, bund)


def spread(v):
    m = sum(v) / len(v)
    return max(abs(x - m) / m for x in v)


def l123(T, s, e_tree, own):
    """The L123 reference build: the L100 tree, then the cheapest notables of the own area and both borders."""
    tree = set(e_tree) | {s}
    prof = G.prof_nodes(T)
    while len(tree) - 1 < 123:
        d = G.steps_from(T, tree, block=prof | (T.starts - {s}))
        cand = [i for i in d if T.kind[i] == 'N' and i not in tree and T.area[i] in own]
        if not cand:
            break
        t = min(cand, key=lambda i: (d[i], i))
        if len(tree) - 1 + d[t] > 125:
            break
        tree |= set(shortest_multi(T, tree, t, prof | (T.starts - {s})))
    tree.discard(s)
    return tree


def shortest_multi(T, sources, t, block):
    from collections import deque
    prev = {x: None for x in sources}
    q = deque(sorted(sources))
    while q:
        u = q.popleft()
        if u == t:
            break
        for v in sorted(T.adj[u]):
            if v not in prev and v not in block:
                prev[v] = u
                q.append(v)
    path = [t]
    while prev[path[-1]] is not None:
        path.append(prev[path[-1]])
    return path


def same_stat_run(T):
    """Longest run of identical travel smalls along lattice chains (attribute nodes of degree 2 in between allowed)."""
    worst = 0
    for i in range(T.n_lat):
        if T.kind[i] != 't':
            continue

        def walk(u, prev, depth):
            best = depth
            for v in T.adj[u]:
                if v == prev or not G.is_lattice(T, v) or T.kind[v] not in 'At':
                    continue
                if T.kind[v] == 't':
                    if T.stat[v] == T.stat[i] and depth < 6:
                        best = max(best, walk(v, u, depth + 1))
                elif len(T.adj[v]) == 2 and depth < 6:
                    best = max(best, walk(v, u, depth))
            return best
        worst = max(worst, walk(i, None, 1))
    return worst


def max_off_depth(T):
    depth = {}

    def d(ci):
        if ci in depth:
            return depth[ci]
        cl = T.clusters[ci]
        a = cl['attach']
        if cl['mode'] != 'off' or T.cl_of[a] < 0:
            depth[ci] = 0
        else:
            depth[ci] = 1 + d(T.cl_of[a])
        return depth[ci]
    return max(d(ci) for ci in range(len(T.clusters)))


def attr_groups(T, attrs):
    """Share of A nodes in linked groups of 2-5 A nodes (A nodes linked directly or through one travel bead)."""
    aset = set(attrs)
    nb = {i: set() for i in attrs}
    for i in attrs:
        for j in T.adj[i]:
            if j in aset:
                nb[i].add(j)
            elif T.kind[j] == 't':
                for k in T.adj[j]:
                    if k in aset and k != i:
                        nb[i].add(k)
    # groups: A nodes within the same short stretch (a node with at most 2 such links is a chain member)
    seen = set()
    inside = 0
    for i in attrs:
        if i in seen:
            continue
        comp, stack = set(), [i]
        while stack:
            u = stack.pop()
            if u in comp:
                continue
            comp.add(u)
            if len(comp) > 6:
                break
            stack += [v for v in nb[u] if len(nb[v]) <= 2 and len(nb[u]) <= 2]
        seen |= comp
        if 2 <= len(comp) <= 5:
            inside += len(comp)
    return inside / len(attrs)


def equal_routes(T):
    """Per neighbour pair: the extra points of the best route that avoids the cheapest one's interior as far as it can,
    and how many of its nodes differ."""
    import heapq
    worst_extra, worst_diff = 0, 99
    prof = G.prof_nodes(T)
    for c in ORDER:
        for x in G.neighbours(c):
            s, t = T.key[c]['S'], T.key[x]['S']
            p1 = G.shortest_path_nodes(T, s, t)
            on = set(p1[1:-1])
            dist = {s: 0.0}
            prev = {}
            pq = [(0.0, s)]
            while pq:
                d, u = heapq.heappop(pq)
                if u == t:
                    break
                if d > dist[u] or (T.kind[u] == 'S' and u != s):
                    continue
                for v in sorted(T.adj[u]):
                    if v in prof:
                        continue
                    nd = d + 1.0 + (0.01 if v in on else 0.0)
                    if nd < dist.get(v, 1e18):
                        dist[v] = nd
                        prev[v] = u
                        heapq.heappush(pq, (nd, v))
            p2 = [t]
            while p2[-1] != s:
                p2.append(prev[p2[-1]])
            worst_extra = max(worst_extra, len(p2) - len(p1))
            worst_diff = min(worst_diff, len(set(p2[1:-1]) - on))
    return worst_extra, worst_diff


def road_ends_from_links(T, ids):
    """A road's door and far gate: the one node off the road linked to its first and to its last node."""
    if not ids:
        return -1, -1
    off = lambda i: [n for n in T.adj[i] if n not in ids]
    return (off(ids[0]) or [-1])[0], (off(ids[-1]) or [-1])[0]


def road_check(T):
    """The four class roads: 8 nodes each in a chain from the door, notables at r3 and r6, and every owner pays 15
    for the door and 23 for r8 (design-v8 §1.1)."""
    road = getattr(T, 'road', {})
    out = []
    ok = len(road) == 32
    for key in G.PROFANE_ROADS:
        ids = sorted((i for i in road if road[i][0] == key), key=lambda i: road[i][1])
        door, far = getattr(T, 'road_ends', {}).get(key) or road_ends_from_links(T, ids)
        chain = [door] + ids + [far]
        ok &= len(ids) == 8 and all(b in T.adj[a] for a, b in zip(chain, chain[1:]))
        ok &= [T.kind[i] for i in ids] == G.ROAD_KINDS
        owner = C.ROADS[key]['owner']
        d = T.Dp[owner]
        cost = (d.get(door, 999), d.get(ids[-1], 999) if ids else 999, d.get(far, 999))
        ok &= cost[0] == 15 and cost[1] == 23
        out.append('%s %d/%d/%d' % ((key,) + cost))
    return ok, ', '.join(out) + ' (door/r8/far gate, points)'


def profane_cross_check(T):
    ok = 0
    tot = 0
    for cl in T.clusters:
        if cl['tag'] != 'profane' or cl.get('road', {}).get('road'):
            continue
        tot += 1
        road = cl.get('road', {})
        ends = [road.get('u'), road.get('v')]
        if None in ends:
            continue
        s = [G.side_of(T, cl['area'], e) for e in ends]
        ok += s[0] * s[1] < 0
    return ok, tot


def geometry(T):
    g = G.grid_for(T)
    n = len(T.pos)
    mind = mind_n = 1e9
    for i in range(n):
        for j in g.near_nodes(T.pos[i], 45):
            if j <= i:
                continue
            dd = math.dist(T.pos[i], T.pos[j])
            mind = min(mind, dd)
            if (T.kind[i] == 'N' and T.kind[j] in 'NJKS') or (T.kind[j] == 'N' and T.kind[i] in 'NJKS'):
                mind_n = min(mind_n, dd)
    clear = 1e9
    for s_ in G.edge_segments(T):
        mid = ((s_[0][0] + s_[1][0]) / 2, (s_[0][1] + s_[1][1]) / 2)
        for j in g.near_nodes(mid, 60):
            if j in (s_[2], s_[3]):
                continue
            clear = min(clear, G.seg_dist(T.pos[j], s_[0], s_[1]))
    reach = set()
    for c in ORDER:
        reach |= set(G.bfs(T, T.key[c]['S']))
    return dict(cross=len(G.crossings(T)), gap=mind, ngap=mind_n, clear=clear, deg=max(len(a) for a in T.adj),
                rmax=max(G.rad(p) for p in T.pos), unreach=sum(1 for i in range(n) if i not in reach))


def ladder_shapes(T):
    shp = []
    for b in G.BORDERS:
        for c in G.BORDERS[b][1:]:
            lt = T.key[b]['lat'][c]
            a_, b_ = lt['a'], lt['b']
            L = math.dist(T.pos[a_[0]], T.pos[a_[-1]])
            W = max(min(math.dist(T.pos[x], T.pos[y]) for y in b_) for x in a_)
            shp.append((L, W))
    worst = 1.0
    for i in range(len(shp)):
        for j in range(i + 1, len(shp)):
            dl = abs(shp[i][0] - shp[j][0]) / max(shp[i][0], shp[j][0])
            dw = abs(shp[i][1] - shp[j][1]) / max(shp[i][1], shp[j][1])
            worst = min(worst, max(dl, dw))
    return worst >= 0.10, 'closest pair differs %.0f%%' % (100 * worst)


def rings(T):
    key = T.key
    jogs = []
    for b in G.BORDERS:
        for ring in ('mid', 'out'):
            ends = []
            for c in G.BORDERS[b][1:]:
                ids = key[c][ring][b]
                ends.append(G.rad(T.pos[ids[-2]]) if T.kind[ids[-2]] == 'A' else G.rad(T.pos[ids[-3]]))
            jogs.append(abs(ends[0] - ends[1]))
    # the ring gap along rays from the centre, measured on the ring links themselves (not on node samples, which
    # make the bead zigzag look like extra pinches)
    def ring_segs(ring):
        return [(T.pos[a], T.pos[b]) for c in ORDER for b_ in key[c][ring] for a, b in zip(key[c][ring][b_], key[c][ring][b_][1:])]

    def ray_hits(segs, deg):
        u = (math.cos(math.radians(deg)), math.sin(math.radians(deg)))
        out = []
        for p, q in segs:
            d = (q[0] - p[0], q[1] - p[1])
            den = u[0] * d[1] - u[1] * d[0]
            if abs(den) < 1e-9:
                continue
            t = (p[0] * d[1] - p[1] * d[0]) / den      # distance along the ray
            w = (p[0] * u[1] - p[1] * u[0]) / den      # fraction along the segment
            if t > 0 and 0 <= w <= 1:
                out.append(t)
        return out
    ms, os_ = ring_segs('mid'), ring_segs('out')
    gaps = []
    for a in range(0, 360, 3):
        rm, ro = ray_hits(ms, a), ray_hits(os_, a)
        if rm and ro:
            gaps.append(min(ro) - max(rm))
    # a pinch is a place where the gap drops below 110; it ends once the gap opens past 130 again, so a dip that
    # wobbles round 110 for a few degrees counts as one place
    pinch, inside = 0, False
    start = max(range(len(gaps)), key=lambda k: gaps[k])
    for k in range(len(gaps)):
        g = gaps[(start + k) % len(gaps)]
        if not inside and g < 110:
            pinch, inside = pinch + 1, True
        elif inside and g > 130:
            inside = False
    return dict(jog=round(min(jogs)), min=round(min(gaps)), max=round(max(gaps)), pinch=pinch)


def islands_check():
    worst_r, worst_gap, roots = 0, 1e9, 1e9
    for name in G.ISLANDS:
        pts, _, _ = G.island_geometry(name)
        worst_r = max(worst_r, max(G.rad(p) for p in pts))
        worst_gap = min(worst_gap, min(math.dist(p, q) for k, p in enumerate(pts) for q in pts[k + 1:]))
    for c in ORDER:
        rs = [G.island_geometry(nm)[0][0] for nm in G.ISLANDS if G.ISLANDS[nm][0] == c]
        roots = min(roots, math.dist(*rs))
    ok = worst_r <= 0.83 * G.SANCTUM and worst_gap >= 38 and roots >= 38
    return ok, '%.0f / %.0f / %.0f' % (worst_r, worst_gap, roots)


def determinism_check(tsv_path, quick, seeds=1):
    with tempfile.TemporaryDirectory() as tmp:
        env = dict(os.environ, PYTHONHASHSEED='12345' if os.environ.get('PYTHONHASHSEED') != '12345' else '777')
        cmd = [sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'poe2_passive_tree.py'), tmp,
               '--no-determinism', '--no-pictures', '--fill-seeds', str(seeds)] + (['--quick'] if quick else [])
        r = subprocess.run(cmd, env=env, capture_output=True, text=True)
        if r.returncode != 0:
            return False, 'second run failed: ' + r.stderr[-200:]
        a = open(tsv_path, 'rb').read()
        b = open(os.path.join(tmp, 'passive_tree.tsv'), 'rb').read()
        return a == b, 'PYTHONHASHSEED=%s' % env['PYTHONHASHSEED']


def names_check(T, poe2_names):
    out = []
    names = [T.name[i] for i in range(len(T.pos)) if T.kind[i] in 'NK' and T.name[i] and not T.name[i].startswith('<')]
    asc = [nm for c in ORDER for a, _, nts in C.ASCENDANCIES[c] for nm in [a] + [x[0] for x in nts]]
    allnames = names + asc
    isl = []   # island names are today's ascendancy content, kept as they are: listed for the content pass
    theirs = set()
    if poe2_names and os.path.exists(poe2_names):
        theirs = {x.strip().lower() for x in open(poe2_names, encoding='utf-8') if x.strip()}
        hit = sorted({nm for nm in names if nm.lower() in theirs})
        out.append(('no name equals a POE2 node name (main tree)', not hit, len(hit), '0', ', '.join(hit[:8])))
    else:
        out.append(('no name equals a POE2 node name', 'INFO', 'not run', '0', 'pass --poe2-names FILE'))
    isl += ['%s (POE2 name)' % nm for nm in asc if nm.lower() in theirs]
    long_ = [nm for nm in names if len(nm) > C.NAME_MAX]
    out.append(('names <= 24 characters (main tree)', not long_, len(long_), '0', ', '.join(long_[:5])))
    isl += ['%s (%d chars)' % (nm, len(nm)) for nm in asc if len(nm) > C.NAME_MAX]
    figs = {f: sum(1 for nm in names if f.lower() in nm.lower()) for f in C.CANON_FIGURES}
    over = sorted(f for f, v in figs.items() if v > 2)
    out.append(('<= 2 notables per canon figure', not over, ', '.join('%s %d' % (f, figs[f]) for f in over) or 'none', 'none over 2', ''))
    hero = sorted({nm for nm in allnames for h in C.HERO_NAMES if h.lower() in nm.lower()})
    out.append(('no hero names', not hero, ', '.join(hero) or 'none', 'none', ''))
    mech = sorted({nm for nm in names for w in C.MECHANIC_WORDS if w.lower() in nm.lower().split()})
    out.append(('no mechanic words (main tree)', not mech, ', '.join(mech) or 'none', 'none', ''))
    isl += sorted({'%s (mechanic word)' % nm for nm in asc for w in C.MECHANIC_WORDS if w.lower() in nm.lower().split()})
    out.append(('island names (unchanged ascendancy content)', 'CONTENT' if isl else True, len(isl), '0',
                '; '.join(isl)))
    lore = [T.lore[i] for i in range(len(T.pos)) if T.lore[i]]
    cw = sorted({w for ln in lore + allnames for w in C.CLASS_WORDS if (' %s ' % w) in (' %s ' % ln.lower().replace('.', ' ').replace(',', ' ').replace("'", ' '))})
    out.append(('no class words in names or lore', not cw, ', '.join(cw) or 'none', 'none', ''))
    dup = sorted({nm for nm in names if names.count(nm) > 1})
    out.append(('no duplicate names', not dup, ', '.join(dup[:5]) or 'none', 'none', ''))
    return out


def content_check(T):
    out = []
    pairs = {}
    clash = []
    for i in range(len(T.pos)):
        if T.kind[i] != 'N' or not T.stats[i]:
            continue
        keys_ = G.stat_pair(T.stats[i])
        if keys_:
            k = (T.area[i], keys_)
            if k in pairs:
                clash.append('%s/%s' % (T.name[pairs[k]], T.name[i]))
            pairs[k] = i
    out.append(('no two notables in an area share two stat keys', not clash, len(clash), '0', '; '.join(clash[:3])))
    speed = []
    for c in ORDER:
        n1 = next(n for n in T.key[c]['N'] if any(s.endswith('Damage') for s, _ in T.stats[n]))
        speed.append(any(s in ('AttackSpeed', 'CastSpeed') for s, _ in T.stats[n1]))
    out.append(("offence openers' speed lines: all or none", True if len(set(speed)) == 1 else 'CONTENT', ''.join('y' if s else 'n' for s in speed), 'all or none',
                '' if len(set(speed)) == 1 else "today's offence openers: Rogue and Monk carry attack speed, Warrior and Sorcerer an on-kill line"))
    # a border notable is usable by a neighbour when at least half of its positive lines are stats that class's
    # usual builds want (its weapons, its defence, its damage types) or stats every build wants
    share = []
    for b, info in C.BORDERS.items():
        ns = [i for i in range(len(T.pos)) if T.kind[i] == 'N' and T.area[i] == b and T.stats[i]]
        for c in info['between']:
            want = USEFUL_STATS[c] | USEFUL_STATS['all']
            ok_ = sum(1 for i in ns if 2 * sum(1 for s, v in T.stats[i] if v > 0 and s in want) >= sum(1 for _, v in T.stats[i] if v > 0))
            share.append(('%s/%s' % (b.split()[0][:4], c[:4]), ok_ / max(1, len(ns))))
    out.append(('border notables usable by each neighbour', all(v >= 0.4 for _, v in share),
                ' '.join('%s %.0f%%' % (k, 100 * v) for k, v in share), '>= 40%', 'half the lines useful to that class'))
    return out


# What each class's usual builds want, for the border check: its weapons, defence and damage types.
USEFUL_STATS = {
    'all': {'Strength', 'Dexterity', 'Magic', 'Vitality', 'LifePercent', 'AllResist', 'FireResist', 'LightningResist', 'MagicResist',
            'LifeRegenRate', 'LifeRegen', 'LifeOnKill', 'FlaskRecovery', 'FlaskCharges', 'StunThreshold', 'MaxElementalResist',
            'MaxMagicResist', 'Spirit', 'DamageVsRareUnique', 'LightRadius'},
    'Warrior': {'Damage', 'MeleeDamage', 'TwoHandDamage', 'OneHandDamage', 'BowDamage', 'ArmorPercent', 'BlockChance', 'StunBuildup',
                'AttackSpeed', 'ToHit', 'FireDamage', 'ArmourVsElements', 'MinionDamage', 'MinionLife'},
    'Sorcerer': {'SpellDamage', 'FireDamage', 'LightningDamage', 'ColdDamage', 'CastSpeed', 'EnergyShieldPercent', 'EnergyShieldRecharge',
                 'ManaPercent', 'ManaRegen', 'ManaOnKill', 'ManaCost', 'MinionDamage', 'MinionLife', 'IgniteChance', 'ShockChance',
                 'FreezeBuildup', 'StaffDamage', 'HitDamageFromMana', 'EvasionVsSpells'},
    'Rogue': {'Damage', 'BowDamage', 'MeleeDamage', 'OneHandDamage', 'EvasionPercent', 'AttackSpeed', 'ToHit', 'FireDamage',
              'LightningDamage', 'ColdDamage', 'IgniteChance', 'ShockChance', 'FreezeBuildup', 'ManaOnKill', 'EvasionVsSpells',
              'ArmorPercent', 'BlockChance'},
    'Monk': {'Damage', 'StaffDamage', 'MeleeDamage', 'EvasionPercent', 'EnergyShieldPercent', 'EnergyShieldRecharge', 'AttackSpeed',
             'CastSpeed', 'ToHit', 'LightningDamage', 'ColdDamage', 'FireDamage', 'ShockChance', 'FreezeBuildup', 'StunBuildup',
             'SpellDamage', 'ManaRegen', 'ManaOnKill', 'ManaPercent', 'EvasionVsSpells', 'BowDamage'},
}


def arc_check(T, rows, ids):
    """Check 28 on the rows as written: token count per row, token syntax, and that the loader's arc (as
    Source/poe2/passive_tree.cpp builds it from the rounded file values) lands on the generator's curve."""
    out = []
    bad_count = bad_tok = 0
    worst = 0.0
    by_id = {r['id']: r for r in rows}
    inv = {v: k for k, v in ids.items()}
    for r in rows:
        links = [int(x) for x in r['links'].split(',') if x]
        higher = [x for x in links if x > r['id']]
        toks = r['arcs'].split(',') if r['arcs'] else []
        if toks and len(toks) != len(higher):
            bad_count += 1
            continue
        for h, tok in zip(higher, toks):
            if not (tok in ('-', 'o') or (tok[0] in '+-' and tok[1:].isdigit() and int(tok[1:]) > 0)):
                bad_tok += 1
                continue
            if r['id'] not in inv or h not in inv:
                continue
            a, b = inv[r['id']], inv[h]
            o = by_id[h]
            pts = loader_arc((r['x'], r['y']), (o['x'], o['y']), tok, (r['gx'], r['gy']), r['cluster_id'] == o['cluster_id']
                             and r['orbit'] == o['orbit'] and r['orbit'] >= 0)
            e = next(e for e in T.edges if {e[0], e[1]} == {a, b})
            pa, pb = T.pos[e[0]], T.pos[e[1]]
            mid_gen = G.arc_point(pa, pb, e[2] * math.dist(pa, pb), 0.5)
            mid_gen = (mid_gen[0], -mid_gen[1])
            m = pts[len(pts) // 2]
            worst = max(worst, math.dist(m, mid_gen))
    out.append(('arc tokens: one per higher link, valid syntax', bad_count == 0 and bad_tok == 0, '%d rows, %d tokens bad' % (bad_count, bad_tok), '0', ''))
    out.append(("loader's arc midpoint vs generator's curve", worst <= 3.0, '%.1f units' % worst, '<= 3 (rounding)', 'dedupe/reverse lookup: Stage 4 tests'))
    return out


def loader_arc(a, b, tok, centre, same_orbit, n=16):
    """Source/poe2/passive_tree.cpp BuildLinkPoints, in Python (screen coordinates)."""
    ax, ay = a
    bx, by = b
    dx, dy = bx - ax, by - ay
    chord = math.sqrt(dx * dx + dy * dy)

    def short(s, e):
        d = e - s
        while d > math.pi:
            d -= 2 * math.pi
        while d < -math.pi:
            d += 2 * math.pi
        return d
    if tok == 'o' and same_orbit:
        cx, cy = centre
        s = math.atan2(ay - cy, ax - cx)
        e = math.atan2(by - cy, bx - cx)
        r = (math.dist(a, centre) + math.dist(b, centre)) / 2
        sw = short(s, e)
        return [(cx + r * math.cos(s + sw * k / n), cy + r * math.sin(s + sw * k / n)) for k in range(n + 1)]
    if tok not in ('-', 'o', '') and chord > 0:
        rr = float(tok[1:]) * (1 if tok[0] == '+' else -1)
        if abs(rr) * 2 >= chord - 0.5:
            radius = max(abs(rr), chord / 2)
            side = 1 if rr > 0 else -1
            h = math.sqrt(max(radius * radius - chord * chord / 4, 0))
            cx, cy = (ax + bx) / 2 - dy / chord * h * side, (ay + by) / 2 + dx / chord * h * side
            s = math.atan2(ay - cy, ax - cx)
            e = math.atan2(by - cy, bx - cx)
            sw = short(s, e)
            if sw * side < 0:
                sw += 2 * math.pi * side
            return [(cx + radius * math.cos(s + sw * k / n), cy + radius * math.sin(s + sw * k / n)) for k in range(n + 1)]
    return [a, ((ax + bx) / 2, (ay + by) / 2), b]


def raster_iou(T):
    import numpy as np
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt

    def raster(rot, n=400):
        fig = plt.figure(figsize=(4, 4), dpi=n / 4)
        ax = fig.add_axes([0, 0, 1, 1])
        lw = 0.02 * R / (2 * R + 100) * n * 72 / (n / 4)
        for e in T.edges:
            if not (G.is_lattice(T, e[0]) and G.is_lattice(T, e[1])):
                continue
            pts = [G.pol(G.rad(p), G.ang_of(p) + rot) for p in T.poly(e, 2)]
            xs, ys = zip(*pts)
            ax.plot(xs, ys, color='k', lw=lw, solid_capstyle='round')
        ax.set_xlim(-R - 50, R + 50)
        ax.set_ylim(-R - 50, R + 50)
        ax.axis('off')
        fig.canvas.draw()
        img = np.asarray(fig.canvas.buffer_rgba())[:, :, 0] < 128
        plt.close(fig)
        return img
    base = raster(0)
    iou = {}
    for rot in (90, 180, 270, 37):
        img = raster(rot)
        iou[rot] = (base & img).sum() / max(1, (base | img).sum())
    m = max(iou[r] for r in (90, 180, 270))
    return m, iou[37], m / iou[37]


def summary(T, K):
    n = len(T.pos)
    main = [i for i in range(n) if T.kind[i] != 'S']
    fails = K.failures()
    lines = ['POE2 Mashup passive tree, stage 3 check report',
             'nodes %d main (%d notables), %d clusters, %d links; problems: %s' % (
                 len(main), sum(1 for i in main if T.kind[i] == 'N'), len(T.clusters), len(T.edges),
                 '; '.join(T.problems) or 'none'),
             'failing: ' + (', '.join('%s %s' % (r[0], r[1]) for r in fails) or 'none')]
    return '\n'.join(lines)


def details(T, RT, bund):
    out = ['Route table (plain tree), per class:']
    keys_ = list(RT['Warrior'].keys())
    for k in keys_:
        out.append('  %-12s ' % k + ' | '.join('%s %s' % (c[:4], str(RT[c][k]).replace(' ', '')) for c in ORDER))
    out.append('Bundles (exact Steiner):')
    for c in ORDER:
        out.append('  %-8s %s' % (c, {k: (round(v, 2) if isinstance(v, float) else v) for k, v in bund[c].items()}))
    out.append('Profane roads (savings per owner; hybrid = points saved between the road\'s two ends):')
    for a, v in T.notes.get('profane', {}).items():
        out.append('  %-17s saving %s, hybrid %s' % (a, '/'.join(map(str, v['saving'])), v.get('hybrid')))
    out.append('Clusters by motif: ' + ', '.join('%s %d' % (nm, sum(1 for cl in T.clusters if cl['name'] == nm))
                                                  for nm in sorted({cl['name'] for cl in T.clusters})))
    out.append('Clusters by mode: ' + ', '.join('%s %d' % (m, sum(1 for cl in T.clusters if cl['mode'] == m))
                                                 for m in sorted({cl['mode'] for cl in T.clusters})))
    if getattr(T, 'fill_scores', None):
        out.append('Fill seeds tried (seed: in range, reach targets met, notables, -|main-1350|); kept seed %s: %s' % (
            getattr(T, 'fill_seed', '?'), '; '.join('%d: %s' % (k, v) for k, v in T.fill_scores)))
    roles = {}
    for i, r in T.role.items():
        roles[r[0]] = roles.get(r[0], 0) + 1
    out.append('Notable roles: ' + ', '.join('%s %d' % kv for kv in sorted(roles.items())))
    return '\n'.join(out)
