#!/usr/bin/env python3
"""Generates the POE2 Mashup passive tree, stage 3 of the tree rework (research/tree-rework/design-spec.md).

What the tree holds (clusters, notables, shrines, sockets) and the hand-authored layout anchors are in
tools/poe2_tree_content.py; this file lays the tree out, links it, checks it (tools/poe2_tree_checks.py, every
design-spec §10 check plus the round-2 review gates) and writes it (TSV, previews, check report).

How the tree is built (design-spec §2-§7, §5.4):
  1. The lattice ("attribute web"): per class a kite (exits, rail heads, rails, corners, frame tops, apex socket),
     the two openings, laterals that end in ladders at the border sockets, spokes to the deep sockets, the mid and
     outer rings with braids, border spines, then the dead-end shrine spurs and the two capstone rim spurs. The
     topology is identical for every class, so every route cost is identical; only the drawing differs.
  2. Clusters, always in matched groups of four (one per quarter): every class meets the same notable costs.
     Order: big orbit sweeps round the 12 sockets, scenic border arches, style and defence clusters at 9,
     crossovers (25 from the neighbour), the Profane roads, set pieces, then the role fill (bare attribute nodes
     first, orbit and pass-through shapes before stalks, off-cluster last), the partner fill, the socket top-up
     (3-5 notables in each jewel radius) and promotions in fours up to 240 notables. The fill runs from
     FILL_SEEDS fixed seeds (in parallel) and the one with the most notables is kept.
  3. Travel stats (strict Life, Res, Mana on the landmark spine), notable roles and content, arcs, ids.

Coordinates inside the generator are maths style (y up, angles anticlockwise), as in the Stage 2 sketch; the
TSV and the pictures use screen coordinates (y down). Deterministic: one fixed seed, no iteration over hashed sets.

Usage: tools/poe2_passive_tree.py OUTDIR [--poe2-names FILE] [--no-determinism] [--quick] [--no-pictures]
                                  [--fill-seeds N]
  Writes OUTDIR/passive_tree.tsv, OUTDIR/notable_roles.tsv, OUTDIR/check-report.txt and the preview pictures.
  It never writes mods/poe2/txtdata/poe2/passive_tree.tsv: the game's loader can't read the new columns yet.
"""

import math
import os
import random
import sys
from collections import deque

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import poe2_tree_content as C  # noqa: E402
import poe2_road_finder as RF  # noqa: E402

SEED = 20261017
R = 1200            # tree radius
SANCTUM = 180       # ascendancy sanctum radius
R_START = 228
ORB = [0, 26, 40, 54, 79, 106, 135]       # orbit radii (POE2's scaled by 0.16)
SLOTS = [1, 24, 24, 24, 72, 72, 72]
ORDER = C.ORDER
CLASSES = C.CLASS_AXIS
BORDERS = {b: (C.BRD[b]['B'][1],) + C.BORDER_SIDES[b] for b in C.BORDER_SIDES}

# Spacing (check 17 limits, plus a margin so rounding to whole units in the TSV breaks nothing).
GAP = 24.0
N_GAP = 38.0
CLEAR = 16.0
MARGIN = 0.4
NODE_GAP = GAP + MARGIN
NOTABLE_GAP = N_GAP + MARGIN
LINK_CLEAR = CLEAR + MARGIN
MOTIF_GAP = 26.0
MAX_LINKS = 5

# Lattice shape rules (handmade-look must-fix 6, Curtis's ruling on straight runs).
RUN_BAND = 12.0          # degrees: a "straight run" keeps every link heading inside this band
RUN_MAX_SPINE = 7        # nodes: straight runs only on spokes, rails and spines, at most this long
NEAR_STRAIGHT_MAX = 260  # units: consecutive lattice links each turning under 12 degrees
LINK_MAX = 300           # units: no single lattice link longer
SPINE_TAGS = {'start', 'exit', 'rail', 'corner', 'frame', 'socket', 'spoke', 'junction', 'deep', 'bsocket', 'spine'}


# ---------------------------------------------------------------- geometry
def pol(r, deg):
    a = math.radians(deg)
    return (r * math.cos(a), r * math.sin(a))


def P(o, deg):
    return pol(ORB[o], deg)


def ang_of(p):
    return math.degrees(math.atan2(p[1], p[0])) % 360


def adiff(a, b):
    return (a - b + 180) % 360 - 180


def rad(p):
    return math.hypot(p[0], p[1])


def unit(p, q):
    dx, dy = q[0] - p[0], q[1] - p[1]
    L = math.hypot(dx, dy) or 1
    return dx / L, dy / L


def heading(p, q):
    return math.degrees(math.atan2(q[1] - p[1], q[0] - p[0]))


def turn_at(a, b, c):
    """Heading change in degrees at b along a-b-c."""
    return abs(adiff(heading(b, c), heading(a, b)))


def seg_dist(p, a, b):
    ax, ay = a
    dx, dy = b[0] - ax, b[1] - ay
    L = dx * dx + dy * dy
    t = 0 if L == 0 else max(0, min(1, ((p[0] - ax) * dx + (p[1] - ay) * dy) / L))
    return math.hypot(p[0] - ax - t * dx, p[1] - ay - t * dy)


def segs_cross(p1, p2, p3, p4):
    def o(a, b, c):
        return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
    return o(p1, p2, p3) * o(p1, p2, p4) < 0 and o(p3, p4, p1) * o(p3, p4, p2) < 0


def bez(ctrl, t):
    pts = list(ctrl)
    while len(pts) > 1:
        pts = [((1 - t) * p[0] + t * q[0], (1 - t) * p[1] + t * q[1]) for p, q in zip(pts, pts[1:])]
    return pts[0]


def left_normal(p, q):
    L = math.dist(p, q) or 1.0
    return (-(q[1] - p[1]) / L, (q[0] - p[0]) / L)


def arc_poly(p, q, sag, n=8):
    """The circular arc from p to q whose midpoint sits `sag` units off the chord, on its left (maths frame) when
    positive. |sag| <= chord / 2, so it is always the minor arc. Straight when sag is 0."""
    L = math.dist(p, q)
    if abs(sag) < 1e-6 or L < 1e-9:
        return [p, q]
    s = max(-L / 2, min(L / 2, sag))
    ln = left_normal(p, q)
    m = ((p[0] + q[0]) / 2, (p[1] + q[1]) / 2)
    Rr = (L * L / 4 + s * s) / (2 * abs(s))
    sg = 1 if s > 0 else -1
    c = (m[0] - sg * (Rr - abs(s)) * ln[0], m[1] - sg * (Rr - abs(s)) * ln[1])
    a0 = math.atan2(p[1] - c[1], p[0] - c[0])
    a1 = math.atan2(q[1] - c[1], q[0] - c[0])
    d = (a1 - a0 + math.pi) % (2 * math.pi) - math.pi
    # the minor arc through the bulge: with the bulge on the left of p->q the arc turns clockwise (d < 0)
    if (s > 0 and d > 0) or (s < 0 and d < 0):
        d = d - math.copysign(2 * math.pi, d)
    out = [(c[0] + Rr * math.cos(a0 + d * t / n), c[1] + Rr * math.sin(a0 + d * t / n)) for t in range(n + 1)]
    out[0], out[-1] = p, q
    return out


def sag_of_point(p, q, m):
    """Signed offset of point m from chord p-q along the left normal."""
    ln = left_normal(p, q)
    mid = ((p[0] + q[0]) / 2, (p[1] + q[1]) / 2)
    return (m[0] - mid[0]) * ln[0] + (m[1] - mid[1]) * ln[1]


def arc_point(p, q, sag, t):
    """Point at fraction t (by angle) along the arc of arc_poly."""
    if abs(sag) < 1e-6:
        return (p[0] + (q[0] - p[0]) * t, p[1] + (q[1] - p[1]) * t)
    pts = arc_poly(p, q, sag, 200)
    return pts[int(round(t * 200))]


# ---------------------------------------------------------------- graph
class Tree:
    """Nodes (pos, kind, tag, label, area, stat) and links [a, b, sag_rel, style]. Kinds: S start, s small, t travel
    small (lattice bead), A attribute, N notable, J jewel socket, K shrine. Link styles: lat (lattice, straight),
    clu (cluster), prof (Profane chord). sag_rel is the arc's sagitta over its chord, so links keep their shape
    when nodes move."""

    def __init__(self):
        self.pos, self.kind, self.tag, self.label, self.area, self.stat = [], [], [], [], [], []
        self.edges = []
        self.adj = []
        self.clusters = []
        self.cl_of = []       # cluster index of each node (-1: lattice or a path that is no motif)
        self.path_id = []     # small paths that are not motifs (openings, spurs, chords) get their own group ids
        self.problems = []    # anything the generator could not place (reported by check())

    def add(self, p, kind, tag='', label='', area=None):
        self.pos.append(p)
        self.kind.append(kind)
        self.tag.append(tag)
        self.label.append(label)
        self.area.append(area)
        self.stat.append(None)
        self.adj.append(set())
        self.cl_of.append(-1)
        self.path_id.append(-1)
        return len(self.pos) - 1

    def link(self, a, b, sag_rel=0.0, style='clu'):
        if b in self.adj[a] or a == b:
            return False
        self.adj[a].add(b)
        self.adj[b].add(a)
        self.edges.append([a, b, sag_rel, style])
        return True

    def mark(self):
        return len(self.pos), len(self.edges), len(self.clusters)

    def undo(self, m):
        n, e, c = m
        for a, b, _, _ in self.edges[e:]:
            self.adj[a].discard(b)
            self.adj[b].discard(a)
        del self.edges[e:]
        for lst in (self.pos, self.kind, self.tag, self.label, self.area, self.stat, self.adj, self.cl_of, self.path_id):
            del lst[n:]
        del self.clusters[c:]

    def poly(self, e, n=8):
        a, b, s, _ = e
        p, q = self.pos[a], self.pos[b]
        return arc_poly(p, q, s * math.dist(p, q), n)


def sag_rel_from_points(p, q, mid):
    L = math.dist(p, q) or 1.0
    return max(-0.5, min(0.5, sag_of_point(p, q, mid) / L))


# ---------------------------------------------------------------- motif library (design-spec §5, design-motifs.png)
# Local frame: the entrance is at the bottom (-y), the cluster grows away from the lattice (+y). Node: (kind, x, y),
# kind s small or N notable. Link: (i, j, style, param): 'l' straight, 'a' arc round param=(cx, cy), 'c' curve with
# bend param (+ = left). Every curved link is fitted as a circular arc when placed.
MOTIFS = {}
PICTOGRAMS = {'Coffin', 'Anvil', 'Crown', 'Ribcage', 'Skull', 'Soulstone', 'Pentagram', 'Horadric Staff', 'Pentagon', 'Triangle'}


def motif(name, diablo, nodes, links, entr, use, note, orbit_ok=False):
    MOTIFS[name] = dict(nodes=nodes, links=links, entr=entr, diablo=diablo, use=use, note=note, orbit_ok=orbit_ok)


def ring_links(idx, centre=(0, 0), closed=True):
    out = [(idx[k], idx[k + 1], 'a', centre) for k in range(len(idx) - 1)]
    if closed:
        out.append((idx[-1], idx[0], 'a', centre))
    return out


motif('Stub', False, [('s',) + P(2, -90), ('s',) + P(2, -22), ('N',) + P(2, 52)],
      ring_links([0, 1, 2], closed=False), [0], 'all', 'tiny: 2 smalls and a notable on one orbit', orbit_ok=True)
motif('Spur Arc', False, [('s',) + P(2, -90), ('s',) + P(2, -34), ('s',) + P(2, 28), ('N',) + P(2, 88)],
      ring_links([0, 1, 2, 3], closed=False), [0], 'all', '3 smalls on one orbit, notable at the dead end', orbit_ok=True)
motif('Hook', False, [('s',) + P(3, -95), ('s',) + P(3, -50), ('s',) + P(3, -4), ('s',) + P(3, 44), ('N',) + P(3, 92)],
      ring_links([0, 1, 2, 3, 4], closed=False), [0], 'all', '4 smalls on one orbit-3 arc, notable at the dead end', orbit_ok=True)
motif('Fork', False, [('s', 0, -40), ('s', 2, -4), ('N', -30, 22), ('s', 30, 18), ('N', 40, 52)],
      [(0, 1, 'c', 0.05), (1, 2, 'c', 0.15), (1, 3, 'c', -0.15), (3, 4, 'c', -0.15)], [0], 'all',
      'stem that forks: a notable on the short tine, another at the end of the long one')
motif('Horns', True, [('s', 0, -44), ('s', 0, -8), ('s', -34, 10), ('N', -46, 50), ('s', 34, 10), ('N', 46, 50)],
      [(0, 1, 'c', 0.05), (1, 2, 'c', 0.2), (2, 3, 'c', -0.3), (1, 4, 'c', -0.2), (4, 5, 'c', 0.3)], [0], 'all areas',
      "the Lord of Terror's horns: a stem that sweeps out into two horns, a notable at each tip")
motif('Crook', False, [('s',) + P(3, -90), ('s',) + P(3, -44), ('s',) + P(3, 8), ('s',) + P(3, 62), ('N', 0, 0)],
      ring_links([0, 1, 2, 3], closed=False) + [(3, 4, 'c', 0.25)], [0], 'all', 'arc that hooks into a centre notable')
motif('Lens', False, [('s', 0, -36), ('s', -24, 0), ('s', 24, 0), ('N', 0, 38)],
      [(0, 1, 'c', 0.25), (1, 3, 'c', 0.25), (0, 2, 'c', -0.25), (2, 3, 'c', -0.25)], [0],
      'all', 'two 1-node arcs meet at the notable: pick your lead-in')
motif('Long Lens', False,
      [('s', 0, -50), ('s', -30, -18), ('s', -30, 20), ('s', 30, -18), ('s', 30, 20), ('N', 0, 52)],
      [(0, 1, 'c', 0.2), (1, 2, 'c', 0.15), (2, 5, 'c', 0.2), (0, 3, 'c', -0.2), (3, 4, 'c', -0.15), (4, 5, 'c', -0.2)],
      [0], 'all', 'two 2-node arcs, different stats on each side')
motif('Teardrop', False,
      [('s', 0, -78), ('s', 0, -42), ('s', -30, -12), ('s', -20, 22), ('s', 30, -12), ('s', 20, 22), ('N', 0, 48)],
      [(0, 1, 'c', 0.12), (1, 2, 'c', 0.25), (2, 3, 'c', 0.15), (3, 6, 'c', 0.15),
       (1, 4, 'c', -0.25), (4, 5, 'c', -0.15), (5, 6, 'c', -0.15)], [0], 'all', 'tail, then a round lens closing on the notable')
motif('Bridge Arc', False,
      [('s',) + P(3, 200), ('s',) + P(3, 148), ('N',) + P(3, 92), ('s',) + P(3, 34), ('s',) + P(3, -18)],
      ring_links([0, 1, 2, 3, 4], closed=False), [0, 4], 'all', 'pass-through: two entrances, notable mid-way', orbit_ok=True)
motif('Ring of Six', False,
      [('s',) + P(2, a) for a in (-90, -34, 22)] + [('N',) + P(2, 92)] + [('s',) + P(2, a) for a in (158, 212)],
      ring_links([0, 1, 2, 3, 4, 5]), [0], 'all', 'one orbit, walk either way round to the far notable')
motif('Great Ring', False,
      [(('N' if k in (4, 7) else 's'),) + P(4, a) for k, a in enumerate((-90, -58, -20, 14, 50, 88, 122, 160, 196, 232))],
      ring_links(list(range(10))), [0], 'rare', 'big 10-ring with two notables; one per area at most')
motif('Long Sweep', False,
      [('s',) + P(5, a) for a in (-100, -72, -41, -12, 19)] + [('N',) + P(5, 52)],
      ring_links([0, 1, 2, 3, 4, 5], closed=False), [0], 'all, mainly round sockets',
      'big: one wide orbit-5 arc round its lattice node or socket, notable at the far end', orbit_ok=True)
motif('Wheel', False,
      [('N', 0, 0)] + [('s',) + P(2, a) for a in (-90, -16, 64, 138, 206)],
      ring_links([1, 2, 3, 4, 5]) + [(0, 3, 'l', None), (0, 4, 'l', None)], [1], 'all', 'notable hub, ring of smalls, two spokes')
motif('Fan', False,
      [('N', 0, 0), ('s',) + P(1, -90), ('s',) + P(3, -90), ('s',) + P(1, 30), ('s',) + P(3, 30), ('s',) + P(1, 150), ('s',) + P(3, 150)],
      [(0, 1, 'l', None), (1, 2, 'l', None), (0, 3, 'l', None), (3, 4, 'l', None), (0, 5, 'l', None), (5, 6, 'l', None)],
      [2], 'rare', 'retired from the fill (it is a tree)')
motif('Spiral', False,
      [('s',) + P(4, -90), ('s',) + P(3, -10), ('s',) + P(2, 70), ('s',) + P(1, 150), ('N', 0, 0)],
      [(0, 1, 'c', -0.2), (1, 2, 'c', -0.2), (2, 3, 'c', -0.2), (3, 4, 'c', -0.2)], [0], 'all', 'steps inward across four orbits')
motif('Split Arcs', False,
      [('s',) + P(3, 222), ('s',) + P(3, 176), ('N',) + P(3, 128), ('s',) + P(3, -40), ('s',) + P(3, 4), ('N',) + P(3, 52)],
      ring_links([0, 1, 2], closed=False) + ring_links([3, 4, 5], closed=False), [0, 3], 'all',
      'two separate spurs on one centre, each its own way in', orbit_ok=True)
motif('Figure Eight', False,
      [('s', 0, -54), ('s', -24, -28), ('s', 24, -28), ('s', 0, -2), ('s', -24, 24), ('s', 24, 24), ('N', 0, 50)],
      ring_links([0, 1, 3, 2], centre=(0, -28)) + ring_links([3, 4, 6, 5], centre=(0, 24)), [0], 'all',
      'two loops sharing a node, the notable at the top of the far loop')
motif('Triangle', False, [('s', 0, -52), ('s', 0, -20), ('s', -22, 16), ('N', 22, 16)],
      [(0, 1, 'l', None), (1, 2, 'l', None), (2, 3, 'l', None), (3, 1, 'l', None)], [0], 'all', 'tail and a tiny 3-loop')
motif('Pentagon', False, [(('N' if k == 3 else 's'),) + P(3, a) for k, a in enumerate((-90, -14, 58, 124, 196))],
      [(k, (k + 1) % 5, 'l', None) for k in range(5)], [0], 'all', 'straight-edged 5-loop, notable off the far side')
motif('Rune Chain', False,
      [('s', 0, -70), ('s', -18, -42), ('s', 18, -42), ('s', 0, -12), ('s', -18, 18), ('s', 18, 18), ('N', 0, 46)],
      [(0, 1, 'c', 0.2), (1, 3, 'c', 0.2), (0, 2, 'c', -0.2), (2, 3, 'c', -0.2),
       (3, 4, 'c', 0.2), (4, 6, 'c', 0.2), (3, 5, 'c', -0.2), (5, 6, 'c', -0.2)], [0], 'all', 'two lenses linked like chain links')
_star = []
for _k in range(5):
    _star.append(('N' if _k in (2, 3) else 's',) + pol(62, 270 + 72 * _k))
    _star.append(('s',) + pol(25, 270 + 36 + 72 * _k))
motif('Pentagram', True, _star, [(k, (k + 1) % 10, 'l', None) for k in range(10)], [0], 'Sorcerer, Zakarum (corrupt)',
      'inverted star outline (no crossing links), entered at the lower point; notables on the two upper points')
motif('Pentacle', True, [('s',) + P(3, a) for a in (-90, -20, 50, 122, 194)] + [('N', 0, 0)],
      ring_links([0, 1, 2, 3, 4]) + [(2, 5, 'l', None)], [0], 'Sorcerer, Horadric',
      'five smalls on a circle, the notable in the middle on a spoke from the far side')
motif('Horadric Staff', True,
      [('s', 2, -52), ('s', 0, -20), ('s', -30, -14), ('N', 32, -12), ('s', 0, 8), ('s', 24, 32), ('N', 0, 56), ('s', -24, 32)],
      [(0, 1, 'c', 0.06), (1, 2, 'l', None), (1, 3, 'l', None), (1, 4, 'l', None)] + ring_links([4, 5, 6, 7], centre=(0, 32)),
      [0], 'Horadric, Zakarum', 'Staff of Kings: shaft, crossbar (notable on one arm), ring head with the amulet notable on top')
motif('Crypt Arch', True, [('s', -40, -40), ('s', -40, 4), ('N', 0, 56), ('s', 40, 4), ('s', 40, -40)],
      [(0, 1, 'c', 0.06), (1, 2, 'c', 0.22), (2, 3, 'c', 0.22), (3, 4, 'c', -0.06)], [0, 4], 'all, mainly borders',
      'pointed arch on two pillars: a pass-through, the classic scenic route')
motif('Twin Vault', True,
      [('s', -70, -36), ('s', -70, 6), ('N', -35, 52), ('s', 0, 6), ('s', 0, -36), ('N', 35, 52), ('s', 70, 6), ('s', 70, -36)],
      [(0, 1, 'c', 0.05), (1, 2, 'c', 0.22), (2, 3, 'c', 0.22), (3, 4, 'c', 0.05), (3, 5, 'c', 0.22), (5, 6, 'c', 0.22), (6, 7, 'c', -0.05)],
      [0, 7], 'Khanduras, Zakarum, Warrior', 'two arches on three pillars (a crypt vault)')
motif('Barrow Gate', True, [('s', -62, -14), ('s', -34, 40), ('N', 0, 12), ('s', 34, 40), ('s', 62, -14), ('s', 0, -30)],
      [(0, 1, 'c', 0.2), (1, 2, 'c', 0.2), (2, 3, 'c', 0.2), (3, 4, 'c', 0.2), (2, 5, 'c', 0.08)], [0, 4, 5], 'Khanduras',
      'two low barrow arches; the notable is the shared pillar')
motif('Sigil Loop', True,
      [('s', 0, -66), ('s', 2, -30)] + [('s',) + (34 * math.cos(math.radians(a)), 6 + 34 * math.sin(math.radians(a))) for a in (-20, 50, 130, 200)] + [('N', 0, 6)],
      [(0, 1, 'c', 0.08)] + ring_links([1, 2, 3, 4, 5], centre=(0, 6)) + [(3, 6, 'l', None)], [0], 'Sorcerer, Monk, Horadric',
      'stem into a loop, the notable inside the loop like the eye of a rune')
motif('Coffin', True, [('s', 0, -84), ('s', -16, -52), ('s', 16, -52), ('s', -30, 14), ('s', 30, 14), ('s', -16, 54), ('N', 16, 54)],
      [(0, 1, 'l', None), (0, 2, 'l', None), (1, 3, 'l', None), (2, 4, 'l', None), (3, 5, 'l', None), (4, 6, 'l', None), (5, 6, 'l', None), (3, 4, 'l', None)],
      [0], 'Warrior, Khanduras', 'sarcophagus outline with a lid line across the shoulders')
motif('Ribcage', True,
      [('s', 0, -76), ('s', 5, -40), ('s', -3, -4), ('s', -34, -28), ('s', 36, -28), ('s', -40, 14), ('N', 38, 14), ('N', 5, 44)],
      [(0, 1, 'c', 0.05), (1, 2, 'c', -0.05), (2, 7, 'l', None), (1, 3, 'c', -0.3), (1, 4, 'c', 0.3), (3, 5, 'c', 0.2), (4, 6, 'c', -0.2),
       (2, 5, 'c', -0.2), (2, 6, 'c', 0.2)], [0], 'Warrior, Khanduras (the risen dead)', 'spine with curved ribs closing into loops, skull notable on top')
_skull = [('s', 0, -70)] + [('s',) + (50 * math.cos(math.radians(a)), 14 + 50 * math.sin(math.radians(a))) for a in (-90, -30, 30, 90, 150, 210)] + [('N', -20, 18), ('N', 20, 18)]
motif('Skull', True, _skull,
      [(0, 1, 'l', None)] + ring_links([1, 2, 3, 4, 5, 6], centre=(0, 14)) + [(6, 7, 'l', None), (3, 8, 'l', None)], [0],
      'once per area at most', 'cranium ring with two notables as the eyes, jaw as the way in')
motif('Sightless Eye', True,
      [('s', -64, 0), ('s', -24, 26), ('s', 24, 26), ('s', 64, 0), ('s', 24, -26), ('s', -24, -26), ('N', 0, 0)],
      [(0, 1, 'c', 0.15), (1, 2, 'c', 0.1), (2, 3, 'c', 0.15), (3, 4, 'c', 0.15), (4, 5, 'c', 0.1), (5, 0, 'c', 0.15),
       (1, 6, 'l', None), (4, 6, 'l', None)], [0, 3], 'Rogue, Sightless Eye',
      'vesica with the notable as the pupil, wired corner to corner so it is equally far from both ways in')
motif('Crown', True,
      [('s', 0, -22), ('s', -34, -17), ('s', -64, -5), ('s', 34, -17), ('s', 64, -5), ('s', -68, 36), ('N', 0, 56), ('s', 68, 36), ('s', 0, 18)],
      [(0, 1, 'c', -0.1), (1, 2, 'c', -0.1), (0, 3, 'c', 0.1), (3, 4, 'c', 0.1), (2, 5, 'l', None), (0, 8, 'l', None), (8, 6, 'l', None), (4, 7, 'l', None)],
      [0], 'Warrior, Khanduras', "Leoric's crown: a curved band with three spikes, notable on the middle spike")
motif('Serpent', True, [('s', -10, -74), ('s', 20, -46), ('s', 0, -14), ('s', -24, 14), ('s', -2, 44), ('N', 26, 64)],
      [(0, 1, 'c', 0.3), (1, 2, 'c', -0.3), (2, 3, 'c', 0.3), (3, 4, 'c', -0.3), (4, 5, 'c', 0.3)], [0], 'Monk',
      'S-curve of five smalls, notable at the head')
motif('Bough', True,
      [('s', 0, -56), ('s', 2, -22), ('s', -36, -4), ('s', -12, 18), ('s', -46, 34), ('s', 36, -4), ('s', 12, 18), ('N', 46, 34)],
      [(0, 1, 'c', 0.06), (1, 2, 'c', 0.2), (2, 4, 'c', 0.2), (1, 3, 'c', 0.08), (3, 4, 'c', -0.2),
       (1, 5, 'c', -0.2), (5, 7, 'c', -0.2), (1, 6, 'c', -0.08), (6, 7, 'c', 0.2)], [0], 'Monk, Sightless Eye',
      'a twig with two leaves (the Brotherhood of the Bough); one leaf ends in the notable')
motif('Anvil', True, [('s', -26, -38), ('s', 26, -38), ('s', -12, -8), ('s', 12, -8), ('s', -34, 20), ('s', 34, 20), ('N', -66, 12)],
      [(0, 1, 'l', None), (0, 2, 'l', None), (1, 3, 'l', None), (2, 4, 'l', None), (3, 5, 'l', None), (4, 5, 'l', None), (4, 6, 'c', 0.25)],
      [0], 'Warrior', "Griswold's anvil: base, waist and face as one loop, the notable on the horn")
motif('Drawn Bow', True, [('s', 0, -24), ('s', -54, -18), ('s', -38, 24), ('N', 0, 40), ('s', 38, 24), ('s', 54, -18)],
      [(0, 1, 'l', None), (1, 2, 'c', 0.2), (2, 3, 'c', 0.15), (3, 4, 'c', 0.15), (4, 5, 'c', 0.2), (5, 0, 'l', None)],
      [0], 'Rogue, Sightless Eye', 'bow limb as an arc, the string as straight links through the nock')
motif('Censer', True,
      [('s', 0, -86), ('s', 2, -56)] + [(('N' if a == 58 else 's'),) + (34 * math.cos(math.radians(a)), 6 + 34 * math.sin(math.radians(a))) for a in (-90, -12, 58, 132, 204)],
      [(0, 1, 'c', 0.08), (1, 2, 'l', None)] + ring_links([2, 3, 4, 5, 6], centre=(0, 6)), [0], 'Zakarum',
      'a short chain into a ring, like a swinging censer')
motif('Soulstone', True, [('s', 0, -62), ('s', -22, -28), ('s', -22, 28), ('N', 0, 64), ('s', 22, 28), ('s', 22, -28), ('s', 0, 0)],
      [(0, 1, 'l', None), (1, 2, 'l', None), (2, 3, 'l', None), (3, 4, 'l', None), (4, 5, 'l', None), (5, 0, 'l', None), (1, 6, 'l', None), (6, 4, 'l', None)],
      [0], 'Horadric', 'long hexagonal crystal with one facet line across it')
motif('Crescent', True,
      [('s',) + P(3, -105), ('s',) + P(3, -48), ('s',) + P(3, 10), ('s',) + P(3, 66), ('N',) + P(3, 115), ('s', 20, 6)],
      ring_links([0, 1, 2, 3, 4], closed=False) + [(0, 5, 'c', 0.2), (5, 4, 'c', 0.2)], [0], 'Rogue, Monk, Sightless Eye',
      'horned moon: long outer arc, short inner arc, notable on the upper horn')

PALETTE = {
    'Warrior': ['Horns', 'Anvil', 'Crown', 'Coffin', 'Ribcage', 'Twin Vault', 'Crypt Arch'],
    'Sorcerer': ['Horns', 'Pentagram', 'Pentacle', 'Sigil Loop', 'Crescent', 'Crypt Arch'],
    'Rogue': ['Horns', 'Drawn Bow', 'Crescent', 'Sightless Eye', 'Crypt Arch'],
    'Monk': ['Horns', 'Bough', 'Serpent', 'Sigil Loop', 'Crescent', 'Crypt Arch'],
    'Zakarum Cloister': ['Horns', 'Censer', 'Crypt Arch', 'Twin Vault', 'Pentagram', 'Horadric Staff'],
    'Khanduras Muster': ['Horns', 'Crown', 'Coffin', 'Ribcage', 'Twin Vault', 'Crypt Arch', 'Barrow Gate'],
    'Sightless Eye': ['Horns', 'Sightless Eye', 'Crescent', 'Drawn Bow', 'Bough', 'Crypt Arch'],
    'Horadric Archive': ['Horns', 'Horadric Staff', 'Soulstone', 'Pentacle', 'Sigil Loop', 'Crypt Arch'],
}
GENERIC = [n for n, m in MOTIFS.items() if not m['diablo']]
RARE = {'Great Ring', 'Fan', 'Skull'}
SPUR_FAMILY = {'Stub', 'Spur Arc'}
CURVED_DIABLO = ['Horns', 'Crypt Arch', 'Serpent', 'Crescent', 'Bough', 'Sigil Loop', 'Censer', 'Sightless Eye', 'Drawn Bow']


# ---------------------------------------------------------------- areas
def neighbours(c):
    i = ORDER.index(c)
    return ORDER[(i + 1) % 4], ORDER[(i - 1) % 4]


def opposite(c):
    return ORDER[(ORDER.index(c) + 2) % 4]


def border_of(c, d):
    for b, (_, c1, c2) in BORDERS.items():
        if {c1, c2} == {c, d}:
            return b
    return None


def P2(c, uv):
    """Local (u, v) of class c to tree coordinates."""
    u, v = uv
    a = math.radians(CLASSES[c])
    return (u * math.cos(a) - v * math.sin(a), u * math.sin(a) + v * math.cos(a))


def P2r(c, rd):
    r, d = rd
    return pol(r, CLASSES[c] + d)


# ---------------------------------------------------------------- lattice chains
def lat_chain(T, a, b, pattern, ctrl=(), zig=28.0, signs=None, tag='', min_bend=18.0, beadjit=0.18):
    """Zigzag lattice chain from node a to node b (sketch rules). pattern: interior kinds, 'A' attribute vertex or
    't' travel bead; beads never adjacent. Vertices sit on a guide Bezier pushed sideways by +-zig, with a visible
    bend at every vertex; beads sit on the straight segment between their neighbours. Every link is straight."""
    assert 'tt' not in pattern
    p0, p1 = T.pos[a], T.pos[b]
    JIT = random.Random(SEED + int(round(p0[0] * 7 + p0[1] * 13 + p1[0] * 17 + p1[1] * 19)) + len(pattern))
    cps = [p0] + list(ctrl) + [p1]
    samples = [bez(cps, i / 400) for i in range(401)]
    acc = [0.0]
    for p, q in zip(samples, samples[1:]):
        acc.append(acc[-1] + math.dist(p, q))
    n = len(pattern) + 1
    vpos = {}
    k = 0
    vi = 0
    for i, kind in enumerate(pattern):
        if kind != 'A':
            continue
        target = acc[-1] * (i + 1 + JIT.uniform(-0.12, 0.12)) / n
        while acc[k] < target:
            k += 1
        k = min(k, 399)
        p, q = samples[k], samples[min(k + 1, 400)]
        dx, dy = q[0] - p[0], q[1] - p[1]
        L = math.hypot(dx, dy) or 1
        s = (signs[vi] if signs else (1 if vi % 2 == 0 else -1)) * zig * JIT.uniform(0.7, 1.25)
        vpos[i] = (p[0] - dy / L * s, p[1] + dx / L * s)
        vi += 1
    verts = [p0] + [vpos[i] for i, k2 in enumerate(pattern) if k2 == 'A'] + [p1]
    for _ in range(20):
        ok = True
        for j in range(1, len(verts) - 1):
            if turn_at(verts[j - 1], verts[j], verts[j + 1]) < min_bend:
                ok = False
                m = ((verts[j - 1][0] + verts[j + 1][0]) / 2, (verts[j - 1][1] + verts[j + 1][1]) / 2)
                dx, dy = verts[j][0] - m[0], verts[j][1] - m[1]
                if math.hypot(dx, dy) < 1:
                    e = (verts[j + 1][0] - verts[j - 1][0], verts[j + 1][1] - verts[j - 1][1])
                    dx, dy = -e[1] * 0.05, e[0] * 0.05
                verts[j] = (verts[j][0] + dx * 0.35, verts[j][1] + dy * 0.35)
        if ok:
            break
    vit = iter(verts[1:-1])
    for i, kind in enumerate(pattern):
        if kind == 'A':
            vpos[i] = next(vit)
    full = [p0] + [vpos.get(i) for i in range(len(pattern))] + [p1]
    for i, kind in enumerate(pattern):
        if kind == 't':
            u, v = full[i], full[i + 2]
            L_ = math.dist(u, v)
            bj = min(beadjit, max(0.0, 0.5 - 30.0 / max(L_, 1.0)))
            t = 0.5 + JIT.uniform(-bj, bj)
            full[i + 1] = (u[0] + (v[0] - u[0]) * t, u[1] + (v[1] - u[1]) * t)
    ids = [a]
    for i, kind in enumerate(pattern):
        ids.append(T.add(full[i + 1], kind, tag))
    ids.append(b)
    for u, v in zip(ids, ids[1:]):
        T.link(u, v, 0.0, 'lat')
    return ids


def outline_chain(T, a, b, pattern, sag, tag, wobble=3.0):
    """Kite outline chain (rails): nodes on one gentle circular arc from a to b (sag: signed sagitta, + left of a->b)
    with at most `wobble` units of jitter, so the outline survives as a clean line in game. Beads sit on the straight
    segment between their neighbours, so every link is straight."""
    p0, p1 = T.pos[a], T.pos[b]
    JIT = random.Random(SEED + 31 + int(round(p0[0] * 3 + p1[1] * 5)))
    n = len(pattern) + 1
    full = [p0] + [None] * len(pattern) + [p1]
    ln = left_normal(p0, p1)
    for i, kind in enumerate(pattern):
        if kind == 'A':
            q = arc_point(p0, p1, sag, (i + 1 + JIT.uniform(-0.08, 0.08)) / n)
            w = JIT.uniform(-wobble, wobble)
            full[i + 1] = (q[0] + ln[0] * w, q[1] + ln[1] * w)
    for i, kind in enumerate(pattern):
        if kind == 't':
            u, v = full[i], full[i + 2]
            full[i + 1] = ((u[0] + v[0]) / 2, (u[1] + v[1]) / 2)
    ids = [a] + [T.add(full[i + 1], kind, tag) for i, kind in enumerate(pattern)] + [b]
    for u, v in zip(ids, ids[1:]):
        T.link(u, v, 0.0, 'lat')
    return ids


def curved_path(T, a, b, kinds, ctrl=(), tag='', style='clu'):
    """Cluster-style path of smalls from a to b along a Bezier; each link is the circular arc through the curve's
    own midpoint."""
    cps = [T.pos[a]] + list(ctrl) + [T.pos[b]]
    samples = [bez(cps, i / 400) for i in range(401)]
    acc = [0.0]
    for p, q in zip(samples, samples[1:]):
        acc.append(acc[-1] + math.dist(p, q))
    n = len(kinds) + 1
    ids = [a]
    k = 0
    cuts = [0]
    for i, kind in enumerate(kinds):
        target = acc[-1] * (i + 1) / n
        while acc[k] < target:
            k += 1
        ids.append(T.add(samples[k], kind, tag))
        cuts.append(k)
    ids.append(b)
    cuts.append(400)
    for (u, v), (s0, s1) in zip(zip(ids, ids[1:]), zip(cuts, cuts[1:])):
        T.link(u, v, sag_rel_from_points(T.pos[u], T.pos[v], samples[(s0 + s1) // 2]), style)
    return ids


def bowed(T, a, b, kinds, off, tag, style='clu'):
    """Smalls from a to b along a bowed curve (off: sideways bow as a fraction of the length)."""
    p, q = T.pos[a], T.pos[b]
    m = ((p[0] + q[0]) / 2, (p[1] + q[1]) / 2)
    L = math.dist(p, q)
    ux, uy = unit(p, q)
    return curved_path(T, a, b, kinds, [(m[0] - uy * off * L, m[1] + ux * off * L)], tag=tag, style=style)


def ring_guide(p0, p1, bumps):
    r0, r1 = rad(p0), rad(p1)
    a0, a1 = ang_of(p0), ang_of(p1)
    d = adiff(a1, a0)
    return [pol(r0 + (r1 - r0) * f + dr, a0 + d * f) for f, dr in bumps]


def rebead(T, ids):
    for x, i, y in zip(ids, ids[1:], ids[2:]):
        if T.kind[i] == 't':
            p, q = T.pos[x], T.pos[y]
            T.pos[i] = ((p[0] + q[0]) / 2, (p[1] + q[1]) / 2)


def jog(T, ids, j0, j1):
    """Shift the first and last attribute vertex of a chain radially (beads follow)."""
    verts = [i for i in ids[1:-1] if T.kind[i] == 'A']
    for i, dj in ((verts[0], j0), (verts[-1], j1)):
        p = T.pos[i]
        T.pos[i] = pol(rad(p) + dj, ang_of(p))
    rebead(T, ids)


def wheel_geom(c):
    """Monk wheel: centre and radius (the circle passes through the start) and the phi of the start."""
    r0, off = C.CLS[c]['wheel']
    a = CLASSES[c]
    cen = pol(r0, a + off)
    S = pol(R_START, a)
    rr = math.dist(cen, S)
    toward = a + off + 180
    phiS = adiff(math.degrees(math.atan2(S[1] - cen[1], S[0] - cen[0])), toward)
    return cen, rr, toward, phiS


def wheel_point(c, phi):
    cen, rr, toward, _ = wheel_geom(c)
    return (cen[0] + rr * math.cos(math.radians(toward + phi)), cen[1] + rr * math.sin(math.radians(toward + phi)))


def build_lattice():
    T = Tree()
    key = {}
    for c, a in CLASSES.items():
        k = key[c] = {}
        p = C.CLS[c]
        S = k['S'] = T.add(pol(R_START, a), 'S', 'start', c)
        if 'wheel' in p:
            ph = p['phi']
            _, _, _, phiS = wheel_geom(c)
            E = [T.add(wheel_point(c, ph['E'][i]), 's', 'exit') for i in range(2)]
            H = [T.add(wheel_point(c, ph['H'][i]), 'A', 'rail') for i in range(2)]
            Cn = [T.add(wheel_point(c, ph['C'][i]), 'A', 'corner') for i in range(2)]
            Tt = [T.add(wheel_point(c, ph['T'][i]), 'A', 'frame') for i in range(2)]
            J = T.add(wheel_point(c, ph['J']), 'J', 'socket', 'own socket')
            for i in range(2):
                T.link(S, E[i], 0.0, 'lat')
                T.link(E[i], H[i], 0.0, 'lat')
                phis = [ph['H'][i] + (ph['C'][i] - ph['H'][i]) * f / 6 for f in range(1, 6)]
                ids = [H[i]] + [T.add(wheel_point(c, f), 'A' if j % 2 == 0 else 't', 'rail') for j, f in enumerate(phis)] + [Cn[i]]
                for u, v in zip(ids, ids[1:]):
                    T.link(u, v, 0.0, 'lat')
                k.setdefault('rails', []).append(ids)
                T.link(Cn[i], Tt[i], 0.0, 'lat')
                T.link(Tt[i], J, 0.0, 'lat')
        elif 'side_arc' in p:
            sa = p['side_arc']
            Cn = [T.add(P2(c, sa['C'][i]), 'A', 'corner') for i in range(2)]
            Tt = [T.add(P2(c, p['T'][i]), 'A', 'frame') for i in range(2)]
            J = T.add(P2(c, p['J']), 'J', 'socket', 'own socket')
            E, H = [], []
            for i in range(2):
                pS, pC = T.pos[S], T.pos[Cn[i]]
                sag = sa['sag'][i] * (1 if i == 0 else -1)     # outward: left of S->C on the anticlockwise side
                fe, fh = sa['frac']
                E.append(T.add(arc_point(pS, pC, sag, fe), 's', 'exit'))
                H.append(T.add(arc_point(pS, pC, sag, fh), 'A', 'rail'))
                T.link(S, E[i], 0.0, 'lat')
                T.link(E[i], H[i], 0.0, 'lat')
                # the rail nodes on the same arc
                fr = [fh + (1 - fh) * f / 6 for f in range(1, 6)]
                pts = [arc_point(pS, pC, sag, f) for f in fr]
                for j in (1, 3):     # beads on the chord midpoint of their neighbours
                    pts[j] = ((pts[j - 1][0] + pts[j + 1][0]) / 2, (pts[j - 1][1] + pts[j + 1][1]) / 2)
                ids = [H[i]] + [T.add(q, 'A' if j % 2 == 0 else 't', 'rail') for j, q in enumerate(pts)] + [Cn[i]]
                for u, v in zip(ids, ids[1:]):
                    T.link(u, v, 0.0, 'lat')
                k.setdefault('rails', []).append(ids)
                T.link(Cn[i], Tt[i], 0.0, 'lat')
                T.link(Tt[i], J, 0.0, 'lat')
        else:
            E = [T.add(P2(c, p['E'][i]), 's', 'exit') for i in range(2)]
            H = [T.add(P2(c, p['H'][i]), 'A', 'rail') for i in range(2)]
            Cn = [T.add(P2(c, p['C'][i]), 'A', 'corner') for i in range(2)]
            Tt = [T.add(P2(c, p['T'][i]), 'A', 'frame') for i in range(2)]
            J = T.add(P2(c, p['J']), 'J', 'socket', 'own socket')
            for i in range(2):
                T.link(S, E[i], 0.0, 'lat')
                T.link(E[i], H[i], 0.0, 'lat')
                sag = p['rail_sag'][i] * (1 if i == 0 else -1)
                ids = outline_chain(T, H[i], Cn[i], 'AtAtA', sag, 'rail')
                k.setdefault('rails', []).append(ids)
                T.link(Cn[i], Tt[i], 0.0, 'lat')
                T.link(Tt[i], J, 0.0, 'lat')
        k.update(E=E, H=H, C=Cn, T=Tt, J=J)
        # the opening: exit, three lead-in smalls, notable at 5, three smalls on to the kite corner (a second way in;
        # three, so a neighbour reaches the opener at 26, after its crossover at 25: route-checker must-fix 2)
        k['N'] = []
        k['nc'] = []
        for i in range(2):
            Nn = T.add(P2(c, p['N'][i]), 'N', 'open', 'opening notable', c)
            ids = curved_path(T, E[i], Nn, 'sss', [P2(c, q) for q in p['lead'][i]], tag='open')
            k.setdefault('lead', []).append(ids)
            k['nc'].append(curved_path(T, Nn, Cn[i], 'sss', [P2(c, q) for q in p['nctl'][i]], tag='open'))
            k['N'].append(Nn)
        X = k['X'] = T.add(P2(c, p['X']), 'A', 'junction')
        k['sp1'] = lat_chain(T, J, X, 'At', [P2(c, q) for q in p['sp1']], zig=p['sp1zig'], tag='spoke')
        D = k['D'] = T.add(P2(c, p['D']), 'J', 'deep', 'deep socket')
        k['sp2'] = lat_chain(T, X, D, 'AtAtA', [P2(c, q) for q in p['sp2']], zig=p['sp2zig'], tag='spoke',
                             signs=p.get('sp2signs'))
    for b, (ab, c1, c2) in BORDERS.items():
        q = C.BRD[b]
        k = key[b] = {}
        B = k['B'] = T.add(pol(*q['B']), 'J', 'bsocket', 'border socket')
        XB = k['XB'] = T.add(pol(*q['XB']), 'A', 'junction')
        OB = k['OB'] = T.add(pol(*q['OB']), 'A', 'junction')
        s16 = T.add(pol(*q['s16']), 'A', 'spine')
        T.link(B, s16, 0.0, 'lat')
        T.link(s16, XB, 0.0, 'lat')
        k['sp'] = [B, s16, XB]
        k['sp2'] = lat_chain(T, XB, OB, 'tAt', [pol(*x) for x in q['spine']], zig=24, tag='spine')
        k['Q'] = {}
        k['lat'] = {}
        for c, corner in ((c1, 0), (c2, 1)):
            lt = q['lat'][c]
            Cn = key[c]['C'][corner]
            p9n = T.add(pol(*lt['p9']), 'A', 'lateral')
            L = T.add(pol(*lt['L']), 'A', 'lateral')
            Q = T.add(pol(*lt['Q']), 'A', 'lateral')
            T.link(Cn, p9n, 0.0, 'lat')
            T.link(p9n, L, 0.0, 'lat')
            pl, pq = T.pos[L], T.pos[Q]
            ln = left_normal(pl, pq)

            def guide(f, w):
                return (pl[0] + (pq[0] - pl[0]) * f + ln[0] * w * 2, pl[1] + (pq[1] - pl[1]) * f + ln[1] * w * 2)
            sa = lat_chain(T, L, Q, 'AtA', [guide(lt['pa'], lt['wa'])], zig=8, tag='lateral',
                           signs=[1, 1] if lt['wa'] > 0 else [-1, -1])
            sb = lat_chain(T, L, Q, 'tAt', [guide(lt['pb'], lt['wb'])], zig=8, tag='lateral',
                           signs=[1] if lt['wb'] > 0 else [-1])
            T.link(sa[3], sb[2], 0.0, 'lat')   # rung between equal-cost nodes: it changes no point cost
            T.link(Q, B, 0.0, 'lat')
            k['Q'][c] = Q
            k['lat'][c] = dict(L=L, a=sa, b=sb, p9=p9n, pocket=lt.get('pocket', False))
        k['s16'] = s16
    for (c, b), spec in C.MID.items():
        A0, B0 = key[c]['X'], key[b]['XB']
        pat = 'tAAAAtA' if spec.get('braid') else 'tAtAtAt'
        s0 = 1 if adiff(ang_of(T.pos[B0]), ang_of(T.pos[A0])) > 0 else -1
        sg = [s0 * (1 if k_ % 2 == 0 else -1) for k_ in range(8)]
        ids = lat_chain(T, A0, B0, pat, ring_guide(T.pos[A0], T.pos[B0], spec['b']), zig=spec['zig'], tag='mid', signs=sg)
        jog(T, ids, *spec['j'])
        key[c].setdefault('mid', {})[b] = ids
        if spec.get('braid'):
            key[c].setdefault('braid', []).append(add_braid(T, ids[2], ids[5], 'mid', ids))
    for (c, b), spec in C.OUT.items():
        A0, B0 = key[c]['D'], key[b]['OB']
        pat = 'AtAAAAt' if spec.get('braid') else 'AtAtAtA'
        s0 = -1 if adiff(ang_of(T.pos[B0]), ang_of(T.pos[A0])) > 0 else 1
        sg = [s0 * (1 if k_ % 2 == 0 else -1) for k_ in range(8)]
        ids = lat_chain(T, A0, B0, pat, ring_guide(T.pos[A0], T.pos[B0], spec['b']), zig=spec['zig'], tag='outer', signs=sg)
        jog(T, ids, *spec['j'])
        key[c].setdefault('out', {})[b] = ids
        if spec.get('braid'):
            key[c].setdefault('braid', []).append(add_braid(T, ids[3], ids[6], 'outer', ids))
    for c in CLASSES:
        k = key[c]
        k['acw'], k['cw'] = border_of(c, neighbours(c)[0]), border_of(c, neighbours(c)[1])
    T.key = key
    T.starts = {key[c]['S'] for c in ORDER}
    relax_lattice(T)
    spread_lattice(T)
    relax_lattice(T)
    clear_lattice(T)
    add_spurs(T)
    set_rim_floor(T)
    return T


def clear_lattice(T, rounds=20):
    """Check 17: a lattice node that sits closer than LINK_CLEAR to a link it is not on (a few units at sharp turns)
    is pushed off that link, or the link's ends are pushed off the node, keeping the run rules, the node gaps and no
    crossings."""
    def worst_near(j):
        out = None
        for a, b, _, _ in T.edges:
            if j in (a, b):
                continue
            d = seg_dist(T.pos[j], T.pos[a], T.pos[b])
            if d < LINK_CLEAR + 0.5 and (out is None or d < out[0]):
                out = (d, a, b)
        return out

    def fine(x, before):
        if len(run_violations(T)) > before:
            return False
        if any(math.dist(T.pos[x], T.pos[k]) < GAP + 0.5 for k in range(len(T.pos)) if k != x):
            return False
        for m in T.adj[x]:
            for k in range(len(T.pos)):
                if k not in (x, m) and math.dist(T.pos[k], T.pos[x]) < 400 and seg_dist(T.pos[k], T.pos[x], T.pos[m]) < CLEAR + 0.5:
                    return False
            for a2, b2, _, _ in T.edges:
                if len({a2, b2, x, m}) == 4 and segs_cross(T.pos[x], T.pos[m], T.pos[a2], T.pos[b2]):
                    return False
        return True

    for _ in range(rounds):
        moved = False
        for j in range(len(T.pos)):
            w = worst_near(j)
            if w is None:
                continue
            d, a, b = w
            pa, pb, pj = T.pos[a], T.pos[b], T.pos[j]
            L2 = (pb[0] - pa[0]) ** 2 + (pb[1] - pa[1]) ** 2 or 1
            f = max(0, min(1, ((pj[0] - pa[0]) * (pb[0] - pa[0]) + (pj[1] - pa[1]) * (pb[1] - pa[1])) / L2))
            cp = (pa[0] + f * (pb[0] - pa[0]), pa[1] + f * (pb[1] - pa[1]))
            before = len(run_violations(T))
            # the smallest nudge (up to 18 units) of the node or of either end of the link that clears it
            steps = sorted(((dx, dy) for dx in range(-18, 19, 2) for dy in range(-18, 19, 2) if 0 < dx * dx + dy * dy <= 324),
                           key=lambda v: (v[0] * v[0] + v[1] * v[1], v))
            done = False
            for dx, dy in steps:
                for x in (j, a, b):
                    if T.tag[x] in HARD_FIXED:
                        continue
                    old = T.pos[x]
                    T.pos[x] = (old[0] + dx, old[1] + dy)
                    if seg_dist(T.pos[j], T.pos[a], T.pos[b]) >= LINK_CLEAR and worst_near(x) is None and fine(x, before):
                        done = True
                        break
                    T.pos[x] = old
                if done:
                    moved = True
                    break
        if not moved:
            break


def add_braid(T, w1, w2, tag, half):
    """Long strand of a braid: travel, attribute, travel; it loops out on the side away from the short strand and
    rejoins at a different angle (one point dearer)."""
    p, q = T.pos[w1], T.pos[w2]
    m = ((p[0] + q[0]) / 2, (p[1] + q[1]) / 2)
    dx, dy = q[0] - p[0], q[1] - p[1]
    L = math.hypot(dx, dy) or 1
    nx, ny = -dy / L, dx / L
    want = pol(rad(m) + (-1 if tag == 'mid' else 1) * 100, ang_of(m))
    side = 1 if (want[0] - m[0]) * nx + (want[1] - m[1]) * ny > 0 else -1
    ctrl = (m[0] + nx * side * 250, m[1] + ny * side * 250)
    ids = lat_chain(T, w1, w2, 'tAt', [ctrl], zig=10, tag='braid')
    return ids


def lattice_node(T, i):
    return T.kind[i] in 'AtJS' and T.tag[i] not in ('rimtip',) or T.tag[i] == 'exit'


def is_lattice_edge(T, e):
    return e[3] == 'lat'


HARD_FIXED = {'start', 'exit', 'corner', 'socket', 'junction', 'deep', 'bsocket', 'frame'}


def lattice_vertex_nbrs(T, adj, i):
    """The two lattice vertices next to node i, skipping travel beads."""
    out = []
    for v in adj[i]:
        prev = i
        while T.kind[v] == 't' and len(adj[v]) == 2:
            nxt = [w for w in adj[v] if w != prev][0]
            prev, v = v, nxt
        out.append(v)
    return out


def relax_lattice(T, rounds=400):
    """Nudge movable lattice nodes until the run rules hold: each step pushes the straightest node of a violating
    run 5 units off the line through its neighbours (a bead may bend too: the lattice bends only at nodes). No node
    moves more than 36 units from its authored spot (rails, spokes and spines 16); beads that never move stay on
    their segment."""
    beads = {}
    for i in range(len(T.pos)):
        if T.kind[i] == 't':
            nb = sorted(T.adj[i])
            if len(nb) == 2:
                p, q = T.pos[nb[0]], T.pos[nb[1]]
                L2 = (q[0] - p[0]) ** 2 + (q[1] - p[1]) ** 2 or 1
                f = ((T.pos[i][0] - p[0]) * (q[0] - p[0]) + (T.pos[i][1] - p[1]) * (q[1] - p[1])) / L2
                beads[i] = (nb[0], nb[1], f)
    adj = lattice_adj(T)
    home = {}
    T.relax_left = []
    for it in range(rounds):
        bad = run_violations(T)
        if not bad:
            break
        moved = set()
        for path in bad:
            best = None
            for pref, tags_ok in ((0, lambda t: t not in SPINE_TAGS and t not in HARD_FIXED),
                                  (1, lambda t: t in ('rail', 'spoke', 'spine'))):
                for i in path:
                    if T.kind[i] not in 'At' or not tags_ok(T.tag[i]) or i in moved:
                        continue
                    lim = 16 if T.tag[i] in SPINE_TAGS else 36
                    if math.dist(home.get(i, T.pos[i]), T.pos[i]) > lim - 5:
                        continue
                    k = path.index(i)
                    if 0 < k < len(path) - 1:
                        a, b = path[k - 1], path[k + 1]
                    elif len(adj[i]) == 2:
                        a, b = adj[i]
                    else:
                        continue
                    tn = turn_at(T.pos[a], T.pos[i], T.pos[b]) + (3 if len(adj[i]) != 2 else 0)
                    if best is None or (pref, tn) < best[0]:
                        best = ((pref, tn), i, a, b)
                if best:
                    break
            if not best:
                continue
            _, i, a, b = best
            home.setdefault(i, T.pos[i])
            pa, pb, pi = T.pos[a], T.pos[b], T.pos[i]
            ln = left_normal(pa, pb)
            side = sag_of_point(pa, pb, pi)
            sg = 1 if side > 0.5 else (-1 if side < -0.5 else (1 if i % 2 else -1))
            T.pos[i] = (pi[0] + ln[0] * 5 * sg, pi[1] + ln[1] * 5 * sg)
            beads.pop(i, None)
            moved.add(i)
        for i, (x, y, f) in sorted(beads.items()):
            p, q = T.pos[x], T.pos[y]
            T.pos[i] = (p[0] + (q[0] - p[0]) * f, p[1] + (q[1] - p[1]) * f)
        if not moved:
            break
    T.relax_left = run_violations(T)


def spread_lattice(T, rounds=120):
    """Push lattice nodes apart until node spacing and link clearance hold with margin (the moved node is the
    non-landmark one; each step is 3 units; beads that move leave their segment, which bends only at nodes)."""
    home = {}
    for it in range(rounds):
        moves = {}
        g = grid_for(T)
        for i, p in enumerate(T.pos):
            for j in g.near_nodes(p, NODE_GAP + 3):
                if j <= i:
                    continue
                d = math.dist(p, T.pos[j])
                if d < NODE_GAP + 2:
                    for a, b in ((i, j), (j, i)):
                        if T.kind[a] in 'At' and T.tag[a] not in HARD_FIXED:
                            u = unit(T.pos[b], T.pos[a])
                            moves.setdefault(a, [0.0, 0.0])
                            moves[a][0] += u[0] * 3
                            moves[a][1] += u[1] * 3
                            break
            for s_ in g.near_segs(p, p, LINK_CLEAR + 3):
                if i in (s_[2], s_[3]):
                    continue
                d = seg_dist(p, s_[0], s_[1])
                if d < LINK_CLEAR + 2.5:
                    cand = [x for x in (i, s_[2], s_[3]) if T.kind[x] in 'At' and T.tag[x] not in HARD_FIXED]
                    if not cand:
                        continue
                    a = cand[0]
                    ax, ay = s_[0][0] - s_[1][0], s_[0][1] - s_[1][1]
                    L = math.hypot(ax, ay) or 1
                    t = max(0, min(1, ((p[0] - s_[1][0]) * ax + (p[1] - s_[1][1]) * ay) / (L * L)))
                    foot = (s_[1][0] + ax * t, s_[1][1] + ay * t)
                    u = unit(foot, p) if a == i else unit(p, foot)
                    moves.setdefault(a, [0.0, 0.0])
                    moves[a][0] += u[0] * 3
                    moves[a][1] += u[1] * 3
        if not moves:
            break
        for a, (dx, dy) in sorted(moves.items()):
            h = home.setdefault(a, T.pos[a])
            q = (T.pos[a][0] + dx, T.pos[a][1] + dy)
            if math.dist(h, q) <= 40:
                T.pos[a] = q


def lattice_adj(T):
    out = [[] for _ in T.pos]
    for e in T.edges:
        if e[3] == 'lat' and abs(e[2]) < 1e-9:
            out[e[0]].append(e[1])
            out[e[1]].append(e[0])
    return out


def straight_runs(T, through=None):
    """Maximal straight runs on the lattice: paths whose link headings all sit inside a 12-degree band.
    Returns a list of node paths with 4 or more nodes."""
    adj = lattice_adj(T)
    out = []
    seen = set()

    def ext(path, hs):
        grown = False
        u = path[-1]
        for v in adj[u]:
            if v in path:
                continue
            h = heading(T.pos[u], T.pos[v])
            hs2 = hs + [h]
            ref = hs2[0]
            dev = [adiff(x, ref) for x in hs2]
            if max(dev) - min(dev) <= RUN_BAND and len(path) < 12:
                ext(path + [v], hs2)
                grown = True
        if not grown and len(path) >= 4:
            key_ = tuple(path) if path[0] < path[-1] else tuple(path[::-1])
            if key_ not in seen:
                seen.add(key_)
                out.append(list(key_))
    starts = range(len(T.pos)) if through is None else sorted({x for e in through for x in e})
    for a in starts:
        for b in adj[a]:
            ext([a, b], [heading(T.pos[a], T.pos[b])])
    return out


def near_straight_runs(T, through=None):
    """Maximal chains of 2+ consecutive straight lattice links, each turning under 12 degrees at the joint:
    (length in units, node path). through: only runs that use one of these links."""
    adj = lattice_adj(T)
    out = []
    seen = set()
    want = None if through is None else {frozenset(e) for e in through}
    for a in range(len(T.pos)):
        for b in adj[a]:
            # only start a run where it cannot extend backwards
            if any(v != b and turn_at(T.pos[v], T.pos[a], T.pos[b]) < RUN_BAND for v in adj[a]):
                continue
            stack = [[a, b]]
            while stack:
                path = stack.pop()
                u, w = path[-2], path[-1]
                nxt = [v for v in adj[w] if v not in path and turn_at(T.pos[u], T.pos[w], T.pos[v]) < RUN_BAND]
                if not nxt:
                    if len(path) >= 3:
                        key_ = tuple(path) if path[0] < path[-1] else tuple(path[::-1])
                        if key_ not in seen and (want is None or any(frozenset(x) in want for x in zip(path, path[1:]))):
                            seen.add(key_)
                            out.append((sum(math.dist(T.pos[x], T.pos[y]) for x, y in zip(path, path[1:])), list(key_)))
                    continue
                for v in nxt:
                    if len(path) < 14:
                        stack.append(path + [v])
    return out


def run_violations(T, through=None):
    bad = []
    for path in straight_runs(T, through):
        if len(path) > RUN_MAX_SPINE or not all(T.tag[i] in SPINE_TAGS for i in path):
            bad.append(path)
    if through is not None and not through:
        return bad
    for L, path in near_straight_runs(T, through):
        if L > NEAR_STRAIGHT_MAX:
            bad.append(path)
    return bad


# ---------------------------------------------------------------- spatial grid and geometry tests on arc polylines
def edge_segments(T, edges=None, n=8):
    out = []
    for e in (T.edges if edges is None else edges):
        pts = T.poly(e, n)
        out += [(p, q, e[0], e[1]) for p, q in zip(pts, pts[1:]) if p != q]
    return out


class Grid:
    """Spatial hash of node positions and link segments, with an undo log."""
    def __init__(self, cell=60):
        self.cell = cell
        self.nodes = {}
        self.segs = {}
        self.log = []

    def cells(self, p, q=None, pad=0):
        q = q or p
        x0, x1 = sorted((p[0], q[0]))
        y0, y1 = sorted((p[1], q[1]))
        c = self.cell
        for gx in range(int((x0 - pad) // c), int((x1 + pad) // c) + 1):
            for gy in range(int((y0 - pad) // c), int((y1 + pad) // c) + 1):
                yield (gx, gy)

    def add_node(self, i, p):
        k = (int(p[0] // self.cell), int(p[1] // self.cell))
        self.nodes.setdefault(k, []).append(i)
        self.log.append((0, k))

    def add_seg(self, s):
        for k in self.cells(s[0], s[1]):
            self.segs.setdefault(k, []).append(s)
            self.log.append((1, k))

    def undo_to(self, n):
        while len(self.log) > n:
            w, k = self.log.pop()
            (self.segs if w else self.nodes)[k].pop()

    def near_nodes(self, p, pad):
        for k in self.cells(p, None, pad):
            yield from self.nodes.get(k, [])

    def near_segs(self, p, q, pad):
        seen = set()
        for k in self.cells(p, q, pad):
            for s in self.segs.get(k, []):
                if id(s) not in seen:
                    seen.add(id(s))
                    yield s


def grid_for(T):
    g = Grid()
    for i, p in enumerate(T.pos):
        g.add_node(i, p)
    for s in edge_segments(T):
        g.add_seg(s)
    return g


def crossings(T, bridges=False):
    """Pairs of links that cross. The class roads pass over other links like bridges (Curtis 2026-10-17): those
    crossings are left out, or with bridges=True are the only ones listed."""
    g = grid_for(T)
    out = set()
    road = road_nodes(T)
    for s in edge_segments(T):
        for t in g.near_segs(s[0], s[1], 2):
            if {s[2], s[3]} & {t[2], t[3]}:
                continue
            if bool({s[2], s[3], t[2], t[3]} & road) != bridges:
                continue
            if segs_cross(s[0], s[1], t[0], t[1]):
                out.add(tuple(sorted(((s[2], s[3]), (t[2], t[3])))))
    return sorted(out)


class LocalCheck:
    """Spacing, clearance, crossing and run tests for a few new lattice nodes and links (spurs)."""
    def __init__(self, T, sp=NODE_GAP + 2, cl=LINK_CLEAR + 1):
        self.T, self.sp, self.cl = T, sp, cl
        self.g = grid_for(T)

    def ok(self, mk):
        T = self.T
        n0, e0, _ = mk
        new_nodes = list(range(n0, len(T.pos)))
        new_edges = T.edges[e0:]
        for i in new_nodes:
            p = T.pos[i]
            if rad(p) > R - 40:
                return False
            for j in list(self.g.near_nodes(p, 45)) + [x for x in new_nodes if x != i]:
                lim = NOTABLE_GAP + 2 if (T.kind[i] in 'NKJ' and T.kind[j] in 'NKJS') else self.sp
                if math.dist(p, T.pos[j]) < lim:
                    return False
            for s_ in self.g.near_segs(p, p, self.cl):
                if i not in (s_[2], s_[3]) and seg_dist(p, s_[0], s_[1]) < self.cl:
                    return False
        for s_ in edge_segments(T, new_edges):
            for t_ in self.g.near_segs(s_[0], s_[1], 4):
                if {s_[2], s_[3]} & {t_[2], t_[3]}:
                    continue
                if segs_cross(s_[0], s_[1], t_[0], t_[1]):
                    return False
            for j in self.g.near_nodes(((s_[0][0] + s_[1][0]) / 2, (s_[0][1] + s_[1][1]) / 2), 70):
                if j not in (s_[2], s_[3]) and seg_dist(T.pos[j], s_[0], s_[1]) < self.cl:
                    return False
        if run_violations(T, through=[(e[0], e[1]) for e in new_edges if e[3] == 'lat']):
            return False
        return True

    def commit(self, mk):
        T = self.T
        n0, e0, _ = mk
        for i in range(n0, len(T.pos)):
            self.g.add_node(i, T.pos[i])
        for s_ in edge_segments(T, T.edges[e0:]):
            self.g.add_seg(s_)


def spur_candidates(base, rs=(0, 25, -25, 50, -50, 80, -80, 110, -110), angs=(0, 3, -3, 6, -6, 9, -9, 13, -13, 17, -17)):
    r0, a0 = rad(base), ang_of(base)
    out = [(abs(dr) / 25 + abs(da) / 3, dr, da) for dr in rs for da in angs]
    return [pol(r0 + dr, a0 + da) for _, dr, da in sorted(out)]


def add_spur(T, chk, att, base, kinds, tag, end_kind, end_tag, label, bends=(0.3, -0.3, 0.2, -0.2, 0.42, -0.42)):
    for p in spur_candidates(base):
        for bend in bends:
            mk = T.mark()
            e = T.add(p, end_kind, end_tag, label)
            bowed(T, att, e, kinds, bend, tag)
            if chk.ok(mk):
                chk.commit(mk)
                return e
            T.undo(mk)
    T.problems.append('no room for spur %s at %s' % (label, tuple(round(x) for x in base)))
    return None


def add_spurs(T):
    """Dead-end spurs, added last so they fit round the finished lattice: own shrine 1 off the clockwise corner
    (12 points), the border shrines off the spine (18), own shrine 2 off the anticlockwise outer half (25), and the
    two rim spurs to the capstones (27 and 29). Each starts from its authored spot and moves only as far as it must."""
    chk = LocalCheck(T)
    for c in ORDER:
        k = T.key[c]
        k['K1'] = add_spur(T, chk, k['C'][1], P2r(c, C.SHR1[c]), 'sss', 'spur', 'K', 'shrine', 'own shrine 1')
    for b in BORDERS:
        k = T.key[b]
        k['K'] = add_spur(T, chk, k['s16'], pol(*C.BRD[b]['shrine']), 's', 'spur', 'K', 'shrine', 'border shrine')
    for c in ORDER:
        k = T.key[c]
        att = k['out'][k['acw']][3]
        dr, da = C.SHR2[c]
        k['K2'] = add_spur(T, chk, att, pol(dr, ang_of(T.pos[att]) + da), 'ss', 'spur', 'K', 'shrine', 'own shrine 2')
        k['cap'], k['rim'] = [], []
        for side, vi, reach, drift, pat in C.RIM[c]:
            att = k['out'][k['cw'] if side == 'cw' else k['acw']][1 + vi]
            pa = T.pos[att]
            done = False
            for rch in (reach, reach + 25, reach - 15, reach + 50, reach + 75, 0, -40):
                for dft in (drift, drift * 0.6, drift * 1.4, drift * 1.8, -drift * 0.6, -drift, -drift * 1.4, -drift * 1.8):
                    for cap_d in (0.75, 1.1, 0.4):
                        for bend in (0.22, -0.22, 0.12, -0.12):
                            mk = T.mark()
                            tip = T.add(pol(min(rad(pa) + rch, R - 70), ang_of(pa) + dft), 'A', 'rim')
                            ids = lat_chain(T, att, tip, pat, [], zig=16, tag='rim')
                            capn = T.add(pol(min(rad(T.pos[tip]) + max(rch, 20) * 0.3 + 30, R - 45),
                                             ang_of(T.pos[tip]) + dft * cap_d), 'N', 'capstone', 'capstone', c)
                            bowed(T, tip, capn, 'ss', bend * 1.5, 'capstone')
                            if chk.ok(mk):
                                chk.commit(mk)
                                k['cap'].append(capn)
                                k['rim'].append(ids)
                                done = True
                                break
                            T.undo(mk)
                        if done:
                            break
                    if done:
                        break
                if done:
                    break
            if not done:
                T.problems.append('no room for rim spur %s %s' % (c, side))


# ---------------------------------------------------------------- rim, pockets
RIM_FLOOR = [0.0] * 360


def rim_at(deg):
    """The ragged outline: R minus 16 bays of different width and depth, but never inside the lattice (a bay stops
    28 units outside the outermost lattice node or link near that angle, so no capstone or rim spur is cut)."""
    r = R - 10
    for a, w, d in C.BAYS:
        r -= 0.75 * d * math.exp(-(adiff(deg, a) / w) ** 2)
    return min(R - 10, max(r, RIM_FLOOR[int(round(deg)) % 360]))


def set_rim_floor(T):
    fl = [0.0] * 360
    pts = list(T.pos) + [p for e in T.edges for p in T.poly(e, 4)]
    for p in pts:
        a, r = ang_of(p), rad(p)
        for d in range(-3, 4):
            k = int(round(a + d)) % 360
            fl[k] = max(fl[k], r + 28)
    RIM_FLOOR[:] = fl


def in_pocket(p):
    return any(math.dist(p, pol(r, a)) < rr for r, a, rr in C.POCKETS)


# ---------------------------------------------------------------- costs and areas
def bfs(T, start, extra_block=()):
    """Point costs from one node: every node taken costs 1; other starts are reached but never walked through."""
    dist = {start: 0}
    q = deque([start])
    starts = T.starts
    back = getattr(T, 'road_back', ())
    while q:
        u = q.popleft()
        for v in sorted(T.adj[u]):
            if v in dist or v in extra_block or (u, v) in back:
                continue
            dist[v] = dist[u] + 1
            if v in starts:
                continue
            q.append(v)
    return dist


def prof_nodes(T):
    return {i for i, t in enumerate(T.tag) if t == 'profane'}


def road_nodes(T):
    """The nodes of the four class roads (design v8), each one-way from its door."""
    return set(getattr(T, 'road', {}))


def all_costs(T, profane=False, roads=True):
    """Point costs from every start. By default the Profane roads are left out: areas, floors, the role fill and
    the §1.3 rows are measured on the plain tree (the route marker never walks an untaken Profane notable either);
    profane=True measures with the roads, for the savings and the floors they must keep (roads=False leaves the four
    class roads out, for the border roads' own savings)."""
    blk = (() if roads else road_nodes(T)) if profane else prof_nodes(T)
    return {c: bfs(T, T.key[c]['S'], blk) for c in ORDER}


def area_by_cost(D, i):
    """Nearest start's class; a node within 2 points of equal between two neighbouring classes belongs to their border."""
    ds = sorted((D[c].get(i, 999), c) for c in ORDER)
    (d1, c1), (d2, c2) = ds[0], ds[1]
    if d2 - d1 <= 2 and border_of(c1, c2):
        return border_of(c1, c2)
    return c1


def assign_areas(T, D=None):
    D = D or all_costs(T)
    for i in range(len(T.pos)):
        T.area[i] = T.label[i] if T.kind[i] == 'S' else area_by_cost(D, i)
    return D


# ---------------------------------------------------------------- motif geometry
def mtransform(pt, centre, rot, mirror, scale=1.0, sx=1.0, sy=1.0, shear=0.0, orbit=False):
    x, y = pt
    if mirror:
        x = -x
    x, y = (x * sx + shear * y) * scale, y * sy * scale
    a = math.radians(rot)
    return (centre[0] + x * math.cos(a) - y * math.sin(a), centre[1] + x * math.sin(a) + y * math.cos(a))


def arc_sag_round(p, q, c):
    """Signed sagitta (left of p->q) of the circular arc from p to q round centre c (minor arc, bulging away from
    c). Uses the mean radius, so it is exact when both ends sit on one orbit."""
    L = math.dist(p, q)
    r = (math.dist(p, c) + math.dist(q, c)) / 2
    if r <= L / 2 + 1e-6:
        s = L / 2
    else:
        s = r - math.sqrt(r * r - L * L / 4)
    side = sag_of_point(p, q, c)
    return -s if side > 0 else s


def motif_geometry(name, centre, rot, mirror, var):
    """Node points and links (i, j, sag_rel) of a motif. Every curved link is a circular arc."""
    m = MOTIFS[name]
    v = {k: x for k, x in var.items() if k in ('scale', 'sx', 'sy', 'shear')}
    pts = [mtransform((n[1], n[2]), centre, rot, mirror, **v) for n in m['nodes']]
    links = []
    for i, j, style, param in m['links']:
        p, q = pts[i], pts[j]
        L = math.dist(p, q) or 1.0
        if style == 'l':
            sr = 0.0
        elif style == 'a':
            sr = arc_sag_round(p, q, mtransform(param, centre, rot, mirror, **v)) / L
        else:
            sr = 0.5 * param * (-1 if mirror else 1)
        links.append((i, j, max(-0.5, min(0.5, sr))))
    return pts, links


def motif_size(name, var):
    m = MOTIFS[name]
    if var.get('orbit'):
        return 'big' if var.get('orb', 3) >= 4 else ('tiny' if len(m['nodes']) <= 4 else 'normal')
    ext = max(math.hypot(n[1], n[2]) for n in m['nodes']) * var.get('scale', 1.0) * max(var.get('sx', 1), var.get('sy', 1))
    if len(m['nodes']) <= 4:
        return 'tiny'
    if ext >= 75 or len(m['nodes']) >= 9:
        return 'big'
    return 'normal'


def point_in_poly(p, poly):
    x, y = p
    inside = False
    for (x1, y1), (x2, y2) in zip(poly, poly[1:] + poly[:1]):
        if (y1 > y) != (y2 > y) and x < x1 + (y - y1) * (x2 - x1) / (y2 - y1):
            inside = not inside
    return inside


def kite_ring(T, c):
    k = T.key[c]
    return [k['S'], k['E'][1]] + k['rails'][1] + [k['T'][1], k['J'], k['T'][0]] + k['rails'][0][::-1] + [k['E'][0]]


def kite_polys(T):
    return {c: [T.pos[i] for i in kite_ring(T, c)] for c in ORDER}


def is_asym(var):
    if var.get('orbit'):
        return True          # orbit placements always use uneven steps and an entrance at one end
    return abs(var.get('sx', 1) - 1) > 0.05 or abs(var.get('sy', 1) - 1) > 0.05 or abs(var.get('shear', 0)) > 0.05


def mpos(name, idx, rot, mirror, var):
    v = {k: x for k, x in var.items() if k in ('scale', 'sx', 'sy', 'shear')}
    return mtransform(MOTIFS[name]['nodes'][idx][1:], (0, 0), rot, mirror, **v)


def fit_two(name, ia, ib, ta, tb, mirror, var, grow=None, smin=0.65, smax=1.6):
    """Similarity fit: motif nodes ia and ib land on ta and tb. grow: a direction the motif's local +y must face."""
    v1 = dict(var, scale=1.0)
    la, lb = mpos(name, ia, 0, mirror, v1), mpos(name, ib, 0, mirror, v1)
    dl = (lb[0] - la[0], lb[1] - la[1])
    dt = (tb[0] - ta[0], tb[1] - ta[1])
    if math.hypot(*dl) < 1:
        return None
    s = math.hypot(*dt) / math.hypot(*dl)
    if not smin <= s <= smax:
        return None
    rot = math.degrees(math.atan2(dt[1], dt[0]) - math.atan2(dl[1], dl[0]))
    v2 = dict(var, scale=s)
    pa = mpos(name, ia, rot, mirror, v2)
    centre = (ta[0] - pa[0], ta[1] - pa[1])
    if grow is not None:
        o = mpos_xy((0, 0), rot, mirror, v2)
        y = mpos_xy((0, 10), rot, mirror, v2)
        if (y[0] - o[0]) * grow[0] + (y[1] - o[1]) * grow[1] <= 0:
            return None
    return centre, rot, v2


def mpos_xy(pt, rot, mirror, var):
    v = {k: x for k, x in var.items() if k in ('scale', 'sx', 'sy', 'shear')}
    return mtransform(pt, (0, 0), rot, mirror, **v)


def stalk_geom(name, ap, d_ang, gap, mirror, var, tilt=0.0):
    """Entrance `gap` away from the attach point along d_ang; the motif's axis is turned `tilt` degrees off that
    line so the attach link never continues a straight line inside the motif."""
    m = MOTIFS[name]
    rot = d_ang - 90 + tilt
    ent = (ap[0] + gap * math.cos(math.radians(d_ang)), ap[1] + gap * math.sin(math.radians(d_ang)))
    pe = mpos(name, m['entr'][0], rot, mirror, var)
    return (ent[0] - pe[0], ent[1] - pe[1]), rot


def rand_var(rnd, asym, scale=None):
    v = dict(scale=scale if scale is not None else rnd.choice([0.84, 0.9, 0.96, 1.04, 1.1]))
    if asym:
        k = rnd.random()
        if k < 0.4:
            v['sx'] = rnd.choice([0.8, 0.86, 1.15, 1.25])
        elif k < 0.7:
            v['sy'] = rnd.choice([0.8, 0.86, 1.15, 1.25])
        else:
            v['shear'] = rnd.choice([-0.27, -0.18, 0.18, 0.27])
    return v


def motif_dist(name):
    """Steps inside the motif from its first entrance to each node."""
    m = MOTIFS[name]
    adj = {}
    for i, j, _, _ in m['links']:
        adj.setdefault(i, set()).add(j)
        adj.setdefault(j, set()).add(i)
    d = {m['entr'][0]: 0}
    q = deque([m['entr'][0]])
    while q:
        u = q.popleft()
        for v in sorted(adj.get(u, ())):
            if v not in d:
                d[v] = d[u] + 1
                q.append(v)
    return d, adj


def n_depths(name, kinds=None):
    """Steps from the attach node to each notable (the attach link counts)."""
    d, _ = motif_dist(name)
    kinds = kinds or [n[0] for n in MOTIFS[name]['nodes']]
    return tuple(sorted(d.get(i, 99) + 1 for i, k in enumerate(kinds) if k == 'N'))


def no_cut_notable(name, kinds=None, entr=None):
    """With only these entrances joined, every node is reachable and no notable is a cut vertex."""
    m = MOTIFS[name]
    kinds = kinds or [n[0] for n in m['nodes']]
    entr = entr if entr is not None else m['entr'][:1]
    adj = {i: set() for i in range(len(m['nodes']))}
    for i, j, _, _ in m['links']:
        adj[i].add(j)
        adj[j].add(i)

    def reach(block):
        seen = {e for e in entr if e != block}
        q = list(seen)
        while q:
            u = q.pop()
            for v in adj[u]:
                if v != block and v not in seen:
                    seen.add(v)
                    q.append(v)
        return seen
    n = len(m['nodes'])
    if len(reach(None)) < n:
        return False
    return all(len(reach(i)) >= n - 1 for i, k in enumerate(kinds) if k == 'N')


def loop_shape(name, kinds=None, entr_used=1):
    """Every notable of the shape sits on a loop (two ways to reach it) once placed."""
    m = MOTIFS[name]
    kinds = kinds or [n[0] for n in m['nodes']]
    adj = {i: set() for i in range(len(m['nodes']))}
    for i, j, _, _ in m['links']:
        adj[i].add(j)
        adj[j].add(i)
    root = -1
    for e in m['entr'][:entr_used]:
        adj.setdefault(root, set()).add(e)
        adj[e].add(root)
    for i, k in enumerate(kinds):
        if k != 'N':
            continue
        nb = sorted(adj[i])
        if len(nb) < 2:
            return False
        seen = {nb[0]}
        q = [nb[0]]
        while q:
            u = q.pop()
            for v in adj[u]:
                if v != i and v not in seen:
                    seen.add(v)
                    q.append(v)
        if not any(x in seen for x in nb[1:]):
            return False
    return True


def min_gap(name):
    pts = [(n[1], n[2]) for n in MOTIFS[name]['nodes']]
    return min(math.dist(p, q) for a, p in enumerate(pts) for q in pts[a + 1:])


MIN_GAP = {n: min_gap(n) for n in MOTIFS}


def fix_var(name, var):
    """Raise the scale so no two nodes of the motif sit closer than 27 units."""
    if var.get('orbit'):
        return var
    squeeze = min(var.get('sx', 1), var.get('sy', 1), 1.0) * (1 - abs(var.get('shear', 0)) / 2)
    need = 27.5 / (MIN_GAP[name] * squeeze)
    if var.get('scale', 1.0) < need:
        var = dict(var, scale=round(need, 2))
    return var


def palette_forms(area):
    """(motif, kinds, notable depths) for every stalk-safe Diablo form in the area's palette, with single-notable
    variants of the two-notable shapes and other notable positions on the Censer ring and the Pentagram points."""
    out = []
    for name in PALETTE[area]:
        base = [n[0] for n in MOTIFS[name]['nodes']]
        variants = [base]
        nots = [i for i, k in enumerate(base) if k == 'N']
        extra = list(range(2, 7)) if name == 'Censer' else (list(range(2, 10, 2)) if name == 'Pentagram' else [])
        for j in (nots if len(nots) >= 2 else []) + extra:
            kv = ['N' if i == j else ('s' if k == 'N' else k) for i, k in enumerate(base)]
            if kv not in variants:
                variants.append(kv)
        for kv in variants:
            if no_cut_notable(name, kv) and min(n_depths(name, kv)) >= 3:
                out.append((name, tuple(kv), n_depths(name, kv)))
    return out


FORMS = {a: palette_forms(a) for a in PALETTE}
STALK_GENERIC = [n for n in GENERIC if no_cut_notable(n) and n != 'Fan']


def image_form(name, kinds, area_img, rnd):
    """The motif quarter r uses: the same generic motif, or a palette shape of the target area with the same
    notable depths."""
    if not MOTIFS[name]['diablo'] or name in PALETTE[area_img]:
        return name, kinds
    prof = n_depths(name, kinds)
    opts = [(n, list(k)) for n, k, p in FORMS[area_img] if p == prof]
    return rnd.choice(opts) if opts else (None, None)


def lattice_ok(T, j, maxdeg):
    return j is not None and T.kind[j] in 'AJ' and T.tag[j] not in ('start', 'exit') and len(T.adj[j]) <= maxdeg


def free_path(T, j):
    """j reaches the lattice without stepping on a notable."""
    seen = {j}
    q = [j]
    while q:
        u = q.pop()
        if u < T.n_lat and T.kind[u] in 'AJt':
            return True
        for v in T.adj[u]:
            if v not in seen and T.kind[v] != 'N':
                seen.add(v)
                q.append(v)
    return False


def is_cut(T, i):
    nb = sorted(T.adj[i])
    if len(nb) < 2:
        return False
    want = set(nb[1:])
    seen = {nb[0]}
    q = [nb[0]]
    while q and want:
        u = q.pop()
        for v in T.adj[u]:
            if v != i and v not in seen:
                seen.add(v)
                want.discard(v)
                q.append(v)
    return bool(want)


def rand_bend(rnd):
    return rnd.choice((-1, 1)) * rnd.uniform(0.12, 0.2)


def fan(a0, steps=(0, 20, -20, 40, -40, 65, -65, 90, -90)):
    return [a0 + d for d in steps]


def link_dirs(T, i):
    out = []
    for e in T.edges:
        if i in (e[0], e[1]):
            poly = T.poly(e, 8)
            if e[0] != i:
                poly = poly[::-1]
            q = poly[1]
            out.append(math.degrees(math.atan2(q[1] - T.pos[i][1], q[0] - T.pos[i][0])) % 360)
    return sorted(out)


# Orbit placement: nodes along one orbit round the attach node (POE2's threaded look); index of notables; entrances
ORBIT_FORM = {
    'Stub': (3, [2], [0]), 'Spur Arc': (4, [3], [0]), 'Bridge Arc': (5, [2], [0, 4]), 'Split Arcs': (6, [2, 3], [0, 5]),
    'Hook': (5, [4], [0]),
}


def orbit_geom(T, at, name, rnd, orbit=None, exact=True):
    """The cluster's nodes sit on one orbit round the lattice node, inside a gap between its links, with uneven
    steps. Returns (points, links, entrances, kinds, orbit) or None."""
    n, nots, entr = ORBIT_FORM[name]
    dirs = link_dirs(T, at)
    if not dirs:
        return None
    gaps = [(dirs[k], (dirs[(k + 1) % len(dirs)] - dirs[k]) % 360 or 360) for k in range(len(dirs))]
    g0, gw = max(gaps, key=lambda g: g[1]) if rnd.random() < 0.7 else rnd.choice(gaps)
    o = orbit or rnd.choice([3, 4, 4, 5])
    r = ORB[o]
    clear = math.degrees(math.asin(min(1.0, 26 / r)))
    span = gw - 2 * clear
    step0 = 2 * math.degrees(math.asin(14 / r))
    steps = [step0 * rnd.uniform(1.0, 1.45) for _ in range(n - 1)]
    if name == 'Split Arcs':
        steps[2] = step0 * rnd.uniform(1.7, 2.3)          # the gap between the two spurs
    if sum(steps) > span or max(steps) >= 170:
        return None
    a0 = g0 + clear + rnd.uniform(0, span - sum(steps))
    angs = [a0]
    for st in steps:
        angs.append(angs[-1] + st)
    if rnd.random() < 0.5:
        angs = angs[::-1]                                  # entrance at either end
    c = T.pos[at]
    pts = [(c[0] + r * math.cos(math.radians(a)), c[1] + r * math.sin(math.radians(a))) for a in angs]
    pairs = [(k, k + 1) for k in range(n - 1) if not (name == 'Split Arcs' and k == 2)]
    links = [(i, j, arc_sag_round(pts[i], pts[j], c) / max(1e-9, math.dist(pts[i], pts[j]))) for i, j in pairs]
    kinds = ['N' if k in nots else 's' for k in range(n)]
    return pts, links, entr, kinds, o


# ---------------------------------------------------------------- the placer
class Placer:
    def __init__(self, T):
        self.T = T
        self.g = grid_for(T)
        self.gid = 0
        self.gfail = {}
        self.rm = None
        self.why = ''
        self.whys = {}
        self.refresh_costs()

    def refresh_costs(self):
        self.D = all_costs(self.T)

    def _f(self, why):
        self.whys[why] = self.whys.get(why, 0) + 1
        return why

    def snapshot(self):
        return self.T.mark(), self.D, len(self.g.log), list(self.T.kind)

    def restore(self, snap):
        mk, D, glen, kinds = snap
        self.T.undo(mk)
        self.D = D
        self.g.undo_to(glen)
        self.T.kind[:] = list(kinds[:len(self.T.kind)])
        if self.rm:
            self.rm.prune(len(self.T.pos))

    def node_ok(self, p, notable=False, own=()):
        T = self.T
        r = rad(p)
        if r > rim_at(ang_of(p)) or r < SANCTUM + 60:
            self._f('n:rim')
            return False
        if in_pocket(p):
            self._f('n:pocket')
            return False
        for j in self.g.near_nodes(p, 45):
            if j in own:
                continue
            lim = NOTABLE_GAP if ((notable and T.kind[j] in 'NKJS') or (T.kind[j] == 'N' and notable)) else NODE_GAP
            if math.dist(p, T.pos[j]) < lim:
                self._f('n:near-' + T.tag[j])
                return False
        for s in self.g.near_segs(p, p, 20):
            if s[2] in own or s[3] in own:
                continue
            if seg_dist(p, s[0], s[1]) < LINK_CLEAR:
                self._f('n:seg-' + T.tag[s[2]] + '/' + T.tag[s[3]])
                return False
        return True

    def path_ok(self, pts, ends):
        """A new link polyline must not cross existing links or pass too close to other nodes."""
        T = self.T
        for p, q in zip(pts, pts[1:]):
            for s in self.g.near_segs(p, q, 4):
                if s[2] in ends or s[3] in ends:
                    continue
                if segs_cross(p, q, s[0], s[1]):
                    return False
            for j in self.g.near_nodes(((p[0] + q[0]) / 2, (p[1] + q[1]) / 2), 45):
                if j in ends:
                    continue
                if seg_dist(T.pos[j], p, q) < LINK_CLEAR:
                    return False
        return True

    def moat(self, p):
        """Inside 40% of R, the ground between the kites stays empty."""
        if rad(p) >= 0.4 * R:
            return False
        return not any(point_in_poly(p, poly) for poly in self.T.kites.values())

    def place(self, name, centre, rot, mirror, var, attach, mode, area, tag='cluster', second=None, extra_links=(),
              kinds=None, check_floor=True, geom=None, att_bend=0.0, role=None):
        """Try to add one motif. attach: node the first entrance joins (for 'orbit' the centre sits on it).
        second: node a second entrance joins ('through'). geom: (points, links, entrances, kinds) fitted by the
        caller (orbit placement). Returns the new ids or None (nothing changed)."""
        T = self.T
        m = MOTIFS[name]
        if geom:
            pts, links, entr, gk = geom[:4]
            kinds = kinds or gk
        else:
            pts, links = motif_geometry(name, centre, rot, mirror, var)
            entr = m['entr']
        kinds = kinds or [n[0] for n in m['nodes']]
        for a in range(len(pts)):
            for b in range(a + 1, len(pts)):
                if math.dist(pts[a], pts[b]) < (NOTABLE_GAP if kinds[a] == 'N' and kinds[b] == 'N' else NODE_GAP + 1):
                    self.why = self._f('tight')
                    return None
        for p, kd in zip(pts, kinds):
            if not self.node_ok(p, kd == 'N') or self.moat(p):
                self.why = self._f('node')
                return None
        new_paths = [arc_poly(pts[i], pts[j], sr * math.dist(pts[i], pts[j])) for i, j, sr in links]
        att_pairs = [(entr[0], attach)]
        if second is not None:
            att_pairs.append((entr[1], second))
        for e, n in extra_links:
            att_pairs.append((e, n))
        if mode == 'orbit':
            att_pairs = [(e, attach) for e in entr]
        for e, n in att_pairs:
            if math.dist(pts[e], T.pos[n]) > 140:
                self.why = self._f('far')
                return None
        for path in new_paths:
            if not self.path_ok(path, ()):
                self.why = self._f('path')
                return None
        att_sr = []
        for k, (e, n) in enumerate(att_pairs):
            sr = 0.0 if mode == 'orbit' or not att_bend else 0.5 * att_bend * (-1) ** k
            att_sr.append(sr)
        att_paths = [arc_poly(pts[e], T.pos[n], sr * math.dist(pts[e], T.pos[n])) for (e, n), sr in zip(att_pairs, att_sr)]
        for (e, n), ap in zip(att_pairs, att_paths):
            if not self.path_ok(ap, (n,)):
                self.why = self._f('attach')
                return None
        segs = [(p, q) for path in new_paths + att_paths for p, q in zip(path, path[1:])]
        for a in range(len(segs)):
            for b in range(a + 1, len(segs)):
                if len({segs[a][0], segs[a][1], segs[b][0], segs[b][1]}) == 4 and segs_cross(*segs[a], *segs[b]):
                    self.why = self._f('cross')
                    return None
        if any(len(T.adj[n]) + sum(1 for _, nn in att_pairs if nn == n) > MAX_LINKS for _, n in att_pairs):
            self.why = self._f('degree')
            return None
        mk = T.mark()
        ids = [T.add(p, kd, tag, '', area) for p, kd in zip(pts, kinds)]
        for i, j, sr in links:
            T.link(ids[i], ids[j], sr, 'clu')
        e0 = mk[1]
        for (e, n), sr in zip(att_pairs, att_sr):
            T.link(ids[e], n, sr, 'clu')
        D_old = self.D
        D = all_costs(T)
        if mode == 'through' or extra_links:
            for c in ORDER:
                for i, v in D_old[c].items():
                    if D[c].get(i, 999) != v:
                        T.undo(mk)
                        self.why = self._f('cost')
                        return None
        if check_floor and not self.floors_ok(D, ids, area):
            T.undo(mk)
            self.why = self._f('floor')
            return None
        if any(T.kind[i] == 'N' and is_cut(T, i) for i in ids):
            T.undo(mk)
            self.why = self._f('cut')
            return None
        self.D = D
        for i in ids:
            self.g.add_node(i, T.pos[i])
        for s in edge_segments(T, T.edges[e0:]):
            self.g.add_seg(s)
        ci = len(T.clusters)
        for i in ids:
            T.cl_of[i] = ci
        T.clusters.append(dict(name=name, centre=centre, ids=ids, area=area, rot=rot, mirror=mirror, var=var, mode=mode,
                               attach=attach, second=second, tag=tag, size=motif_size(name, var), role=role,
                               orbit=geom[4] if geom and len(geom) > 4 else None))
        return ids

    def floors_ok(self, D, ids, area):
        T = self.T
        for i in ids:
            if T.kind[i] != 'N':
                continue
            if area in CLASSES:
                if T.tag[i] != 'open' and D[area].get(i, 999) < 6:
                    return False          # nothing but the opening pair below 6 points
                for x in ORDER:
                    if x == area:
                        continue
                    if x == opposite(area):
                        need = 40
                    else:
                        need = 25 if T.tag[i] == 'crossover' else 26
                    if D[x].get(i, 999) < need:
                        return False
                if area_by_cost(D, i) != area:
                    return False
            else:
                _, c1, c2 = BORDERS[area]
                if abs(D[c1].get(i, 999) - D[c2].get(i, 999)) > 1 or area_by_cost(D, i) != area:
                    return False
                if min(D[c1].get(i, 999), D[c2].get(i, 999)) < 16:
                    return False          # a border notable costs at least what the border socket does, plus one
        return True


# ---------------------------------------------------------------- lockstep placement (design-spec §5.4)
# Every cluster is placed as a group of four: once in each quarter of the tree, at nodes whose point costs are the
# same up to a quarter turn of the classes (the lattice topology is identical for every class). The four copies use
# the same generic motif, or for Diablo shapes the target area's own palette shape with the same notable depths, but
# each has its own size, mirror, skew and direction. If any copy does not fit, the whole group is rolled back.
def kgroup(k):
    return 'L' if k in 'At' else k


def cost_vec(D, i):
    return tuple(D[c].get(i, 999) for c in ORDER)


def rot_vec(v, r):
    return tuple(v[(k - r) % 4] for k in range(4))


def rot_area(a, r):
    if a in CLASSES:
        return ORDER[(ORDER.index(a) + r) % 4]
    _, c1, c2 = BORDERS[a]
    return border_of(rot_area(c1, r), rot_area(c2, r))


class RoleMap:
    """img[i][r]: the node that plays i's role a quarter turn r later (same costs, classes rotated by r)."""
    def __init__(self, T, D):
        self.img = {}
        self.map_nodes(T, D, range(len(T.pos)))

    def map_nodes(self, T, D, nodes):
        idx = {}
        for i in nodes:
            idx.setdefault((cost_vec(D, i), kgroup(T.kind[i])), []).append(i)
        for (v, g), xs in sorted(idx.items()):
            for r in (1, 2, 3):
                ys = idx.get((rot_vec(v, r), g), [])
                if not ys or (len(ys) != len(xs) and min(xs) < T.n_lat):
                    continue          # lattice roles must match one to one; cluster smalls may pair up partly
                pairs = sorted((math.dist(pol(rad(T.pos[x]), ang_of(T.pos[x]) + 90 * r), T.pos[y]), x, y)
                               for x in xs for y in ys)
                ux, uy = set(), set()
                for _, x, y in pairs:
                    if x in ux or y in uy:
                        continue
                    ux.add(x)
                    uy.add(y)
                    self.img.setdefault(x, [x, None, None, None])[r] = y

    def prune(self, n):
        for i in [i for i in self.img if i >= n]:
            del self.img[i]
        for lst in self.img.values():
            for r in (1, 2, 3):
                if lst[r] is not None and lst[r] >= n:
                    lst[r] = None

    def get(self, i, r):
        if r == 0:
            return i
        lst = self.img.get(i)
        return lst[r] if lst else None

    def full(self, i):
        lst = self.img.get(i)
        return lst is not None and None not in lst


def group_place(Pl, attempt):
    """attempt(r) places the copy for quarter r and returns its ids (or None). All four or nothing."""
    snap = Pl.snapshot()
    n0 = len(Pl.T.clusters)
    out = []
    for r in range(4):
        ids = attempt(r)
        if not ids:
            Pl.restore(snap)
            Pl.gfail[r] = Pl.gfail.get(r, 0) + 1
            return None
        out.append(ids)
    Pl.gid += 1
    for q, cl in enumerate(Pl.T.clusters[n0:]):
        cl['group'] = Pl.gid
        cl['q'] = q
    Pl.rm.map_nodes(Pl.T, Pl.D, [i for ids in out for i in ids])
    return out


def try_stalk(Pl, names, at, area, dirs, gaps=(34, 42, 50, 58), tag='cluster', mirrors=(False, True), vars_=None,
              kinds=None, mode='stalk', att_bend=0.0, tilts=(15, -15), role=None):
    T = Pl.T
    for name in names:
        for d_ang in dirs:
            for gap in gaps:
                for mirror in mirrors:
                    for var in (vars_ or [dict()]):
                        var = fix_var(name, var)
                        for tilt in tilts:
                            centre, rot = stalk_geom(name, T.pos[at], d_ang, gap, mirror, var, tilt)
                            ids = Pl.place(name, centre, rot, mirror, var, at, mode, area, tag=tag, kinds=kinds,
                                           att_bend=att_bend, role=role)
                            if ids:
                                return ids
    return None


def try_orbit(Pl, at, name, area, rnd, n=10, orbits=None, tag='cluster', role=None, kinds=None):
    T = Pl.T
    for k in range(n):
        o = orbits[k % len(orbits)] if orbits else None
        geom = orbit_geom(T, at, name, rnd, orbit=o)
        if not geom:
            continue
        var = dict(scale=round(ORB[geom[4]] / ORB[2], 2), orbit=True, orb=geom[4])
        ids = Pl.place(name, T.pos[at], 0, False, var, at, 'orbit', area, geom=geom, tag=tag, role=role, kinds=kinds)
        if ids:
            return ids
    return None


def open_dirs(Pl, i, rings=(55, 85, 115, 145)):
    """Directions (every 20 degrees) from node i ordered by how much free ground lies along them, with that count."""
    p = Pl.T.pos[i]
    out = []
    for a in range(0, 360, 20):
        n = 0
        for g in rings:
            q = (p[0] + g * math.cos(math.radians(a)), p[1] + g * math.sin(math.radians(a)))
            if Pl.node_ok(q) and not Pl.moat(q):
                n += 1
            else:
                break
        out.append((n, a))
    out.sort(key=lambda x: (-x[0], x[1]))
    return out


def notable_room(Pl, i, ignore=()):
    """Node i may be a notable: no other notable, shrine, socket or start within the notable gap."""
    T = Pl.T
    return all(j == i or j in ignore or T.kind[j] not in 'NKJS' or math.dist(T.pos[i], T.pos[j]) >= NOTABLE_GAP
               for j in Pl.g.near_nodes(T.pos[i], 45))


def steps_from(T, sources, block=()):
    """Multi-source BFS steps (starts are not walked through)."""
    d = {s: 0 for s in sources}
    q = deque(sorted(sources))
    while q:
        u = q.popleft()
        for v in sorted(T.adj[u]):
            if v in d or v in block:
                continue
            d[v] = d[u] + 1
            if T.kind[v] == 'S':
                continue
            q.append(v)
    return d


# ---------------------------------------------------------------- placement passes
def socket_roles(T):
    """The three socket role groups, each given by its Warrior-quarter (or Zakarum) representative."""
    return [T.key['Warrior']['J'], T.key['Zakarum Cloister']['B'], T.key['Warrior']['D']]


def place_socket_sweeps(Pl):
    """First, a big orbit sweep round every jewel socket (POE2's look: the socket is the hub of a wide arc), in
    groups of four by socket role. Bridge Arc on orbit 5 or 4 (both ends join the socket, so its notable sits on a
    loop 3 points past the socket), else Split Arcs, else a Hook."""
    T, rm = Pl.T, Pl.rm
    rnd = random.Random(SEED + 11)
    T.notes['sweep'] = {}
    for base in socket_roles(T):
        g = None
        for name, orbits in (('Bridge Arc', (5, 4)), ('Split Arcs', (5, 4)), ('Bridge Arc', (3,)), ('Hook', (5, 4)),
                             ('Spur Arc', (4, 3)), ('Stub', (4, 3))):
            def attempt(r, name=name, orbits=orbits):
                a = rm.get(base, r)
                if a is None:
                    return None
                return try_orbit(Pl, a, name, area_by_cost(Pl.D, a), rnd, n=60, orbits=orbits, tag='sweep', role='socket')
            g = group_place(Pl, attempt)
            if g:
                break
        T.notes['sweep'][T.tag[base]] = g
        if not g:
            T.problems.append('socket sweep failed: ' + T.tag[base])


def lateral_nodes(T, b, c):
    lt = T.key[b]['lat'][c]
    return [lt['p9'], lt['L']] + lt['a'][1:-1] + lt['b'][1:-1] + [T.key[b]['Q'][c]]


def place_scenic(Pl):
    """One scenic pass-through per border. It joins two lateral nodes that mirror each other in cost (x from one
    side costs what y costs from the other), so its notable is equal from both sides, and it bows away from the
    lattice. Detour +2 to +4."""
    T = Pl.T
    T.notes['scenic'] = {}
    for b, (ab, c1, c2) in BORDERS.items():
        k = T.key[b]
        D = Pl.D
        Bp = T.pos[k['B']]
        pairs = []
        for x in lateral_nodes(T, b, c1):
            for y in lateral_nodes(T, b, c2):
                if D[c1][x] == D[c2][y] and D[c2][x] == D[c1][y] and 90 < math.dist(T.pos[x], T.pos[y]) < 280 \
                        and len(T.adj[x]) < MAX_LINKS and len(T.adj[y]) < MAX_LINKS:
                    gd = bfs(T, x).get(y, 99)
                    pairs.append((math.dist(T.pos[x], Bp) + math.dist(T.pos[y], Bp), x, y, gd))
        pairs.sort()
        names = [C.BRD[b]['scenic']] + [n for n in ('Crypt Arch', 'Twin Vault', 'Bridge Arc') if n != C.BRD[b]['scenic']]
        done = None
        for name in names:
            m = MOTIFS[name]
            for _, x, y, gd in pairs:
                d_in = motif_dist(name)[0].get(m['entr'][1], 99) + 2
                if not 2 <= d_in - gd <= 4:
                    continue
                p1, p2 = T.pos[x], T.pos[y]
                u = unit(p1, p2)
                for side in (1, -1):
                    nrm = (-u[1] * side, u[0] * side)
                    for g in (34, 42, 28, 50, 58):
                        for h in (20, 35, 50, 8, 65):
                            for mirror in (False, True):
                                for var in (dict(), dict(sx=1.15), dict(shear=0.18), dict(shear=-0.18)):
                                    ta = (p1[0] + u[0] * g + nrm[0] * h, p1[1] + u[1] * g + nrm[1] * h)
                                    tb = (p2[0] - u[0] * g + nrm[0] * h, p2[1] - u[1] * g + nrm[1] * h)
                                    f = fit_two(name, m['entr'][0], m['entr'][1], ta, tb, mirror, var, grow=nrm)
                                    if not f:
                                        continue
                                    centre, rot, v2 = f
                                    done = Pl.place(name, centre, rot, mirror, v2, x, 'through', b, tag='scenic',
                                                    second=y, att_bend=0.14, role='scenic')
                                    if done:
                                        T.notes['scenic'][b] = (name, x, y, d_in - gd)
                                        break
                                if done:
                                    break
                            if done:
                                break
                        if done:
                            break
                    if done:
                        break
                if done:
                    break
            if done:
                break
        if not done:
            T.problems.append('scenic failed: ' + b)


def place_openings(Pl):
    """First 10 points, in lockstep: on each kite rail a two-notable style cluster (Horns) at the rail's middle
    attribute node (5), both notables at 9, one weapon or skill family per rail; and a defence cluster at 9."""
    T, rm = Pl.T, Pl.rm
    T.notes['style'] = {}
    T.notes['defence'] = {}
    rnd = random.Random(SEED + 5)
    vs = [dict(scale=0.8), dict(scale=0.8, sx=0.86), dict(scale=0.84, sy=0.86), dict(scale=0.8, shear=0.18),
          dict(scale=0.8, shear=-0.18), dict(scale=0.9), dict(scale=0.78, sx=1.15)]

    def lobe_dir(c, a):
        """Point into the kite: from the rail node toward the kite's middle."""
        k = T.key[c]
        mid = ((T.pos[k['S']][0] + T.pos[k['J']][0]) / 2, (T.pos[k['S']][1] + T.pos[k['J']][1]) / 2)
        return math.degrees(math.atan2(mid[1] - T.pos[a][1], mid[0] - T.pos[a][0]))

    def run(at, names, target, tag, role, inward=True):
        def attempt(r):
            a = rm.get(at, r)
            c = ORDER[r]
            if a is None or len(T.adj[a]) >= MAX_LINKS:
                return None
            v = vs[r:] + vs[:r]
            a0 = lobe_dir(c, a) if inward else ang_of(T.pos[a])
            for nm in names:
                ids = try_stalk(Pl, [nm], a, c, fan(a0, (0, 18, -18, 36, -36, 55, -55, 75, -75, 100, -100, 130, -130, 160, -160, 180)),
                                gaps=(30, 36, 44), tag=tag, vars_=v, att_bend=rand_bend(rnd), role=role)
                if ids and target in [Pl.D[c][x] for x in ids if T.kind[x] == 'N']:
                    return ids
                if ids:
                    return ids
            return None
        return group_place(Pl, attempt)

    k0 = T.key['Warrior']
    for i in (0, 1):
        at = k0['rails'][i][3]
        g = run(at, ['Horns', 'Fork'], 9, 'style', 'style') or run(at, ['Horns', 'Fork'], 9, 'style', 'style', inward=False)
        for r, c in enumerate(ORDER):
            T.notes['style'][(c, i)] = g[r] if g else None
        if not g:
            T.problems.append('style cluster failed on rail %d' % i)
    # defence: a one-notable shape of depth 4 at a rail's middle attribute node (9), else depth 3 off an opening small
    g = None
    for i in (1, 0):
        at = k0['rails'][i][3]
        names = [n for n in ('Long Lens', 'Ring of Six', 'Wheel', 'Spur Arc') if n_depths(n) == (4,)]
        g = run(at, names, 9, 'defence', 'defence') or run(at, names, 9, 'defence', 'defence', inward=False)
        if g:
            break
    if not g:
        for i in (0, 1):
            at = k0['nc'][i][1]
            g = run(at, ['Lens', 'Stub', 'Triangle'], 9, 'defence', 'defence')
            if g:
                break
    for r, c in enumerate(ORDER):
        T.notes['defence'][c] = g[r] if g else None
    if not g:
        T.problems.append('defence cluster failed')


def place_crossovers(Pl):
    """On each lateral, a cluster whose notable the neighbour reaches at exactly 25 (and the owner at about 15): the
    neighbour's first notable in this area. Placed in lockstep from the two laterals of the Zakarum Cloister; on a
    pocket ladder it grows into the pocket between the strands."""
    T, rm = Pl.T, Pl.rm
    T.notes['cross'] = {}
    rnd = random.Random(SEED + 6)
    b0 = 'Zakarum Cloister'
    vs = [dict(), dict(sx=1.15), dict(shear=0.18), dict(shear=-0.18), dict(scale=0.9), dict(sy=0.86)]
    for c in BORDERS[b0][1:]:
        lat = T.key[b0]['lat'][c]
        nb = [x for x in BORDERS[b0][1:] if x != c][0]
        done = None
        cands = [lat['L'], lat['p9']] + [x for x in lat['a'][1:-1] + lat['b'][1:-1] if T.kind[x] == 'A']
        for at in cands:
            need = 25 - Pl.D[nb][at]
            names = [n for n in ('Crook', 'Teardrop', 'Spiral', 'Figure Eight', 'Rune Chain', 'Hook', 'Long Lens', 'Ring of Six',
                                 'Spur Arc', 'Lens', 'Stub', 'Long Sweep') if n_depths(n) == (need,)]
            if not names or not all(lattice_ok(T, rm.get(at, r), 4) for r in range(4)):
                continue

            def attempt(r):
                a = rm.get(at, r)
                b_r = rot_area(b0, r)
                c_r = rot_area(c, r)
                lt = T.key[b_r]['lat'][c_r]
                strands = lt['a'][1:-1] + lt['b'][1:-1]
                cen = (sum(T.pos[x][0] for x in strands) / len(strands), sum(T.pos[x][1] for x in strands) / len(strands))
                d_pocket = math.degrees(math.atan2(cen[1] - T.pos[a][1], cen[0] - T.pos[a][0]))
                dirs = ([d_pocket + d for d in (0, 15, -15, 30, -30)] if lt['pocket'] else []) + \
                    fan(ang_of(T.pos[a]), (0, 25, -25, 50, -50, 75, -75, 100, -100, 130, -130))
                v = vs[r:] + vs[:r]
                ids = try_stalk(Pl, names, a, c_r, dirs, tag='crossover', vars_=v, att_bend=rand_bend(rnd),
                                role='crossover')
                if not ids and 'Hook' in names:
                    ids = try_orbit(Pl, a, 'Hook', c_r, rnd, n=12, tag='crossover', role='crossover')
                return ids
            done = group_place(Pl, attempt)
            if done:
                break
        for r in range(4):
            T.notes['cross'][(rot_area(b0, r), rot_area(c, r))] = done[r] if done else None
        if not done:
            T.problems.append('crossover failed: %s side of %s' % (c, b0))


# Set pieces: per area a big shape and a normal one round one hub, each from the area's own palette, placed in
# lockstep (hubs mapped by role). (motif, scale, notable indices or None for the default)
SET_PIECES = {
    'Warrior': (('Coffin', 1.15, None), ('Crown', 0.85, None)),                   # Leoric's tomb
    'Sorcerer': (('Pentagram', 1.1, [3]), ('Crescent', 1.0, None)),              # the summoning floor
    'Monk': (('Bough', 1.2, None), ('Crescent', 1.0, None)),                     # wreath of boughs
    'Rogue': (('Drawn Bow', 1.2, None), ('Sightless Eye', 0.8, None)),           # the bow and the eye
    'Zakarum Cloister': (('Censer', 1.0, [3]), [('Pentagram', 0.95, [2]), ('Horadric Staff', 0.9, [3]), ('Pentagram', 0.8, [2]), ('Twin Vault', 0.9, None), ('Horns', 0.85, None), ('Crypt Arch', 0.85, None)]),
    'Horadric Archive': (('Soulstone', 1.0, None), ('Horadric Staff', 1.0, [3])),
    'Sightless Eye': (('Bough', 1.0, None), ('Crescent', 1.0, None)),
    'Khanduras Muster': (('Ribcage', 1.0, [7]), ('Crown', 0.85, None)),
}
SET_TARGET = {'Warrior': (690, -14), 'Zakarum Cloister': (960, 40)}
SET_TURN = [0, -80, 100, 40]   # degrees, per quarter


def open_ground(Pl, i):
    p = Pl.T.pos[i]
    return sum(1 for g in (60, 90, 120, 150) for a in range(0, 360, 20)
               if Pl.node_ok((p[0] + g * math.cos(math.radians(a)), p[1] + g * math.sin(math.radians(a))))
               and not Pl.moat((p[0] + g * math.cos(math.radians(a)), p[1] + g * math.sin(math.radians(a)))))


def place_set_pieces(Pl, bases=None):
    """The set pieces of SET_TARGET's bases (default all): two per area, matched in fours."""
    T, rm = Pl.T, Pl.rm
    T.notes.setdefault('set', {})
    rnd = random.Random(SEED + 7 + (0 if bases is None else sorted(SET_TARGET).index(bases[0])))

    def attempt_for(base, k, at, mode, turn=1.0):
        def attempt(r):
            area = rot_area(base, r)
            a = rm.get(at, r)
            if a is None or len(T.adj[a]) >= MAX_LINKS:
                return None
            opts = SET_PIECES[area][k]
            for name, sc, nots in (opts if isinstance(opts, list) else [opts]):
                kinds = None
                if nots:
                    kinds = ['N' if i in nots else ('s' if n[0] == 'N' else n[0]) for i, n in enumerate(MOTIFS[name]['nodes'])]
                vs = [dict(scale=sc), dict(scale=sc, sx=1.15), dict(scale=sc, shear=0.18), dict(scale=sc, shear=-0.18),
                      dict(scale=sc * 0.9), dict(scale=sc * 0.85, sy=1.1)]
                # each quarter turns its piece its own way off the hub, so the four copies do not sit on rotated
                # spots (check 8b)
                ids = try_stalk(Pl, [name], a, area, fan(ang_of(T.pos[a]) + SET_TURN[r] * turn, [s * d for d in range(0, 181, 15) for s in (1, -1)][1:]),
                                gaps=(36, 46, 56, 70)[r % 2:] + (36, 46, 56, 70)[:r % 2], tag='set', vars_=vs[r:] + vs[:r], kinds=kinds, mode=mode,
                                att_bend=rand_bend(rnd), role='set')
                if ids:
                    return ids
            return None
        return attempt

    for base, (r0, a0) in SET_TARGET.items():
        if bases is not None and base not in bases:
            continue
        target = pol(r0, a0)
        got = []
        for k in (0, 1):
            mx = 3 if base in CLASSES else 4
            hubs = [i for i in range(T.n_lat) if lattice_ok(T, i, mx) and area_by_cost(Pl.D, i) == base and rm.full(i)
                    and all(lattice_ok(T, rm.get(i, r), mx) for r in range(4))
                    and (math.dist(T.pos[i], target) < (330 if base in CLASSES else 600) if not got
                         else 60 < math.dist(T.pos[i], T.pos[got[0][0]]) < (420 if base in CLASSES else 560))]
            hubs.sort(key=lambda i: (-min(open_ground(Pl, rm.get(i, r)) for r in range(4)), math.dist(T.pos[i], target), i))
            hubs = hubs[:14 if base in CLASSES else 24]
            if got:     # the second element may also hang off a small of the first
                hubs += [x for x in got[0][1][0] if T.kind[x] == 's' and rm.full(x) and len(T.adj[x]) <= 2]
            for turn in (1.0, 0.5, 0.0):     # turned copies first (check 8b); straight copies if nothing fits
                for hub_ in hubs:
                    g = group_place(Pl, attempt_for(base, k, hub_, 'stalk' if hub_ < T.n_lat else 'off', turn))
                    if g:
                        got.append((hub_, g))
                        break
                if len(got) > k:
                    break
        for r in range(4):
            T.notes['set'][rot_area(base, r)] = (rm.get(got[0][0], r) if got else None, len(got))
        if len(got) < 2:
            T.problems.append('set piece short: %s %d' % (base, len(got)))


# ---------------------------------------------------------------- Profane Paths (Curtis 04:19, replaces spec §7)
# Each class's Profane Path is a long cracked road clear across its own area, from one side's family ground over the
# kite's axis to the other side's: two arcs that meet on the axis spoke (a road can only cross the spoke at one of its
# nodes). The outward arc carries the Profane notable after 2+ lead-in smalls and is the one that saves points: the
# far side's notables, deep ground and capstone cheaper (the search tries 10 points down; the largest saving all
# four classes reach is kept, 4 with today's lattice: a road may cross no link, so it stays inside one lattice
# cell plus the axis node, and the rings and spokes join a cell's two sides every few points). The inward arc
# saves nothing on its own; it
# joins the near side's family road to the spoke, so a hybrid build reaches the far side's family cheaply. Each border's
# road crosses its border area inside the border socket, from one neighbour's ladder to the other's, the notable in
# the middle, with similar savings for both neighbours. All matched in fours (same savings); when no saving fits all
# four borders, the better opposite pair is kept so every class neighbours exactly one border road. Costs with the roads
# must keep: no key target of another class changes, every notable floor holds (neighbour notables >= 25/26,
# opposite >= 40), neighbour starts >= 28, opposite start 48.
PROFANE_SAVE_MAX = 10
PROFANE_LINK = (40, 140)   # a Profane road is a long cracked road: its links may be longer than a cluster's


def key_targets(T):
    """Every node whose cost §1.3, check 6 or a bundle reads."""
    keyt = ('open', 'capstone', 'corner', 'junction', 'deep', 'frame', 'crossover', 'style', 'defence', 'exit',
            'socket', 'bsocket', 'start')
    return [i for i in range(len(T.pos)) if T.kind[i] in 'SJK' or (T.tag[i] in keyt and T.kind[i] in 'NA')]


def profane_arc(Pl, a, b, kinds, off):
    """Geometry of one arc of a road from node a to node b with the given new node kinds: (points, sags) or None."""
    T = Pl.T
    pa, pb = T.pos[a], T.pos[b]
    L = len(kinds) + 1
    chord = math.dist(pa, pb)
    poly = arc_poly(pa, pb, off, 240)
    pts = []
    t_prev = 0
    for k in range(1, L):
        got = None
        for d in (0, 0.03, -0.03, 0.06, -0.06, 0.1, -0.1, 0.14, -0.14):
            t = k / L + d / L * 2
            if t <= t_prev + 0.02 or t >= 1 - 0.02:
                continue
            q = poly[int(round(t * 240))]
            prev = pts[-1] if pts else pa
            if not PROFANE_LINK[0] <= math.dist(prev, q) <= PROFANE_LINK[1]:
                continue
            if Pl.node_ok(q, kinds[k - 1] == 'N') and not Pl.moat(q):
                got = (t, q)
                break
        if not got:
            Pl._f('prof:node')
            return None
        t_prev = got[0]
        pts.append(got[1])
    seq = [pa] + pts + [pb]
    if any(not PROFANE_LINK[0] <= math.dist(x, y) <= PROFANE_LINK[1] for x, y in zip(seq, seq[1:])):
        Pl._f('prof:len')
        return None
    cen = None
    if abs(off) > 1e-6:
        Rr = (chord * chord / 4 + off * off) / (2 * abs(off))
        ln = left_normal(pa, pb)
        m = ((pa[0] + pb[0]) / 2, (pa[1] + pb[1]) / 2)
        sg = 1 if off > 0 else -1
        cen = (m[0] - sg * (Rr - abs(off)) * ln[0], m[1] - sg * (Rr - abs(off)) * ln[1])
    sags = [(arc_sag_round(x, y, cen) / max(1.0, math.dist(x, y))) if cen else 0.0 for x, y in zip(seq, seq[1:])]
    for k, (x, y) in enumerate(zip(seq, seq[1:])):
        ends = ((a,) if k == 0 else ()) + ((b,) if k == L - 1 else ())
        if not Pl.path_ok(arc_poly(x, y, sags[k] * math.dist(x, y)), ends):
            Pl._f('prof:path')
            return None
    return pts, sags


def profane_ok(Pl, D0, Dp, owners, P):
    """The road keeps every other class's key costs and every floor; returns True when it may stay."""
    T = Pl.T
    keys = Pl.prof_keys
    for c in ORDER:
        for x in ORDER:
            if x == c:
                continue
            opp = x == opposite(c)
            kx = T.key[x]
            for t, need in ((kx['S'], 48 if opp else 28), (kx['J'], 42 if opp else 24), (kx['D'], 42 if opp else 24),
                            (kx['K1'], 41 if opp else 25), (kx['K2'], 41 if opp else 25)):
                if Dp[c].get(t, 999) < need:
                    Pl._f('pf:landmark')
                    return False
    for i in range(len(T.pos)):
        if T.kind[i] != 'N' or i == P or T.tag[i] in ('open', 'capstone'):
            continue
        area = area_by_cost(D0, i)
        if area in CLASSES:
            for x in ORDER:
                if x == area:
                    continue
                need = 40 if x == opposite(area) else (25 if T.tag[i] == 'crossover' else 26)
                if Dp[x].get(i, 999) < need:
                    Pl._f('pf:floor-' + T.tag[i])
                    return False
        else:
            _, c1, c2 = BORDERS[area]
            if min(Dp[c1].get(i, 999), Dp[c2].get(i, 999)) < 16:
                Pl._f('pf:border16')
                return False
    return True


def self_cross(T, edges):
    segs = edge_segments(T, edges)
    return any(len({a[2], a[3], b[2], b[3]}) == 4 and segs_cross(a[0], a[1], b[0], b[1])
               for k, a in enumerate(segs) for b in segs[k + 1:])


def road_clear(T, ids, mk):
    """Check 17 inside one road: where its two arcs meet at the axis node, no road node may sit closer than
    LINK_CLEAR to the other arc's links."""
    nodes = set(ids) | {x for e in T.edges[mk[1]:] for x in e[:2]}
    for sg_ in edge_segments(T, T.edges[mk[1]:]):
        for j in nodes:
            if j not in (sg_[2], sg_[3]) and seg_dist(T.pos[j], sg_[0], sg_[1]) < LINK_CLEAR:
                return False
    return True


def add_road(Pl, arcs, area, role):
    """arcs: [(a, b, kinds, points, sags)]. Adds the nodes and links; returns (ids, P, mark)."""
    T = Pl.T
    mk = T.mark()
    ids, P = [], None
    for a, b, kinds, pts, sags in arcs:
        new = [T.add(q, kd, 'profane', '', area) for q, kd in zip(pts, kinds)]
        chain = [a] + new + [b]
        for k, (x, y) in enumerate(zip(chain, chain[1:])):
            T.link(x, y, sags[k], 'prof')
        ids += new
        P = P if P is not None else next((i for i, kd in zip(new, kinds) if kd == 'N'), None)
    return ids, P, mk


def commit_road(Pl, ids, mk, area, ends, info):
    T = Pl.T
    for i in ids:
        Pl.g.add_node(i, T.pos[i])
    for sg_ in edge_segments(T, T.edges[mk[1]:]):
        Pl.g.add_seg(sg_)
    ci = len(T.clusters)
    for i in ids:
        T.cl_of[i] = ci
    xs = [T.pos[i] for i in ids]
    T.clusters.append(dict(name='Profane Path', centre=(sum(p[0] for p in xs) / len(xs), sum(p[1] for p in xs) / len(xs)),
                           ids=ids, area=area, rot=0, mirror=False, var={}, mode='road', attach=ends[0], second=ends[-1],
                           tag='profane', size='big', role='profane', orbit=None, road=info))


def road_saving(T, D0, Dp, owners, among=None):
    """Per owner: the largest start-cost drop the road gives (over `among`, default every node)."""
    out = []
    for c in owners:
        xs = among if among is not None else D0[c].keys()
        out.append(max([D0[c][x] - Dp[c].get(x, 999) for x in xs if x in D0[c]] + [0]))
    return tuple(out)


def owners_of(area):
    return [area] if area in CLASSES else list(BORDERS[area][1:])


def axis_angle(T, area):
    return CLASSES[area] if area in CLASSES else ang_of(T.pos[T.key[area]['B']])


def side_of(T, area, i):
    """Signed distance of node i from the area's axis (the class axis, or the line through the border socket);
    + is the anticlockwise side."""
    a = math.radians(axis_angle(T, area))
    p = T.pos[i]
    return -p[0] * math.sin(a) + p[1] * math.cos(a)


def try_road(Pl, area, u, w, v, L1, L2, offs, lo):
    """Profane road u ~ w ~ v clear across the area. A road may not cross a link, so it crosses the area's axis (the
    class's spoke, or the border's spine) at one lattice node, w. The arc w ~ v carries the Profane notable after two
    lead-in smalls and gives the saving; the arc u ~ w is as long as the walk it doubles (an equal route), so it saves
    nothing without the notable. Returns (savings per owner, hybrid saving, P, ids) or None (nothing changed)."""
    T = Pl.T
    D0 = Pl.D
    owners = owners_of(area)
    if len(T.adj[w]) > MAX_LINKS - 2 or len(T.adj[u]) >= MAX_LINKS or len(T.adj[v]) >= MAX_LINKS:
        return None
    k2 = ['s', 's', 'N'] + ['s'] * (L2 - 4)
    k1 = ['s'] * (L1 - 1)
    old = bfs(T, u, prof_nodes(T)).get(v, 999)
    among = None if area in CLASSES else [x for x in D0[owners[0]] if area_by_cost(D0, x) == area]
    for o1, o2 in offs:
        g2 = profane_arc(Pl, w, v, k2, o2 * math.dist(T.pos[w], T.pos[v]))
        g1 = profane_arc(Pl, u, w, k1, o1 * math.dist(T.pos[u], T.pos[w])) if g2 else None
        if not g1:
            continue
        ids, P, mk = add_road(Pl, [(w, v, k2) + g2, (u, w, k1) + g1], area, 'profane')
        ok = all(math.dist(T.pos[i], T.pos[j]) >= NODE_GAP for i in ids for j in ids if i < j) and \
            not self_cross(T, T.edges[mk[1]:]) and road_clear(T, ids, mk)
        if ok:
            Dp = all_costs(T, profane=True, roads=False)
            ok = profane_ok(Pl, D0, Dp, owners, P)
        if ok:
            sv = road_saving(T, D0, Dp, owners, among)
            inner = set(ids[len(k2):])
            D1 = {c: bfs(T, T.key[c]['S'], prof_nodes(T) - inner) for c in ORDER}
            hyb = old - bfs(T, u, road_nodes(T)).get(v, 999)
            ok = all(lo <= x <= PROFANE_SAVE_MAX for x in sv) and max(sv) - min(sv) <= 1 and \
                max(road_saving(T, D0, D1, owners)) == 0
            if not ok:
                Pl._f('prof:save')
        if ok:
            commit_road(Pl, ids, mk, area, (u, w, v), dict(u=u, w=w, v=v, L1=L1, L2=L2, saving=sv, hybrid=hyb))
            return sv, hyb, P, ids
        T.undo(mk)
    return None


def try_arc_road(Pl, area, u, v, L, offs, lo):
    """Border road u ~ v as one arc from one neighbour's side to the other's, the notable in the middle with 2+
    smalls on each side, so each neighbour saves on the way to the other's side. Returns like try_road."""
    T = Pl.T
    D0 = Pl.D
    owners = owners_of(area)
    if len(T.adj[u]) >= MAX_LINKS or len(T.adj[v]) >= MAX_LINKS:
        return None
    h = (L - 1) // 2
    kinds = ['s'] * h + ['N'] + ['s'] * (L - 2 - h)
    old = bfs(T, u, prof_nodes(T)).get(v, 999)
    among = [x for x in D0[owners[0]] if area_by_cost(D0, x) == area]
    for off in offs:
        g = profane_arc(Pl, u, v, kinds, off * math.dist(T.pos[u], T.pos[v]))
        if not g:
            continue
        ids, P, mk = add_road(Pl, [(u, v, kinds) + g], area, 'profane')
        ok = road_clear(T, ids, mk)
        if ok:
            Dp = all_costs(T, profane=True, roads=False)
            ok = profane_ok(Pl, D0, Dp, owners, P)
        if ok:
            sv = road_saving(T, D0, Dp, owners, among)
            ok = all(lo <= x <= PROFANE_SAVE_MAX for x in sv) and max(sv) - min(sv) <= 1
        if ok:
            hyb = old - bfs(T, u, road_nodes(T)).get(v, 999)
            commit_road(Pl, ids, mk, area, (u, v), dict(u=u, v=v, L=L, saving=sv, hybrid=hyb))
            return sv, hyb, P, ids
        T.undo(mk)
    return None


def arc_plans(Pl, area, sv):
    T = Pl.T
    D0 = Pl.D
    c1, c2 = owners_of(area)
    nodes = [x for x in range(T.n_lat) if area_by_cost(D0, x) in (area, c1, c2) and len(T.adj[x]) < MAX_LINKS
             and T.kind[x] in 'AtJ' and T.tag[x] not in ('start', 'exit', 'open', 'spur', 'rail')
             and abs(side_of(T, area, x)) < 320]
    out = []
    for u in nodes:
        if side_of(T, area, u) > -30:
            continue
        for v in nodes:
            if side_of(T, area, v) < 30:
                continue
            d = math.dist(T.pos[u], T.pos[v])
            for L in range(5, 10):
                if not 0.8 * PROFANE_LINK[0] * L <= d <= 0.95 * PROFANE_LINK[1] * L:
                    continue
                g = sorted((D0[c1][v] - D0[c1][u] - L, D0[c2][u] - D0[c2][v] - L))
                if g[0] >= sv and g[1] - g[0] <= 1 and g[1] <= PROFANE_SAVE_MAX:
                    out.append((g[0] != sv, L, u, v, L))
    out.sort()
    return [x[2:] for x in out]


def road_plans(Pl, area, sv, feas):
    """Candidate roads (u, w, L1, v, L2) of `area` whose saving arc w ~ v should save sv for every owner."""
    T = Pl.T
    D0 = Pl.D
    owners = owners_of(area)
    blk = prof_nodes(T)

    def arc_feasible(a_, b_, L_, kinds_):
        key = (a_, b_, L_)
        if key not in feas:
            dd = math.dist(T.pos[a_], T.pos[b_])
            feas[key] = 0.8 * PROFANE_LINK[0] * L_ <= dd <= 0.95 * PROFANE_LINK[1] * L_ and any(
                profane_arc(Pl, a_, b_, kinds_, o * dd) for o in (0.1, -0.1, 0.18, -0.18, 0.26, -0.26, 0.04, -0.04))
        return feas[key]

    def usable(x):
        return len(T.adj[x]) < MAX_LINKS and (T.kind[x] in 'AtJ' and T.tag[x] not in ('start', 'exit', 'open', 'spur',
                                                                                         'capstone', 'rail')
                                              or T.tag[x] == 'capstone')
    nodes = [x for x in range(T.n_lat) if area_by_cost(D0, x) == area and usable(x)]
    # a border road's equal-route arc may start just over the line, in a neighbour's area
    unodes = nodes if area in CLASSES else [x for x in range(T.n_lat) if usable(x) and area_by_cost(D0, x) in
                                            [area] + owners and abs(side_of(T, area, x)) < 320]
    out = []
    for w in nodes:
        if len(T.adj[w]) > MAX_LINKS - 2 or T.tag[w] == 'capstone':
            continue
        dw = None
        for v in nodes:
            gains = [D0[c][v] - D0[c][w] for c in owners]
            L2 = min(gains) - sv
            sg = 1 if side_of(T, area, v) > 0 else -1
            if max(gains) - min(gains) > 1 or not (5 if T.kind[v] == 'N' else 4) <= L2 <= 8 or side_of(T, area, v) * sg < 40:
                continue
            if not arc_feasible(w, v, L2, ['s', 's', 'N'] + ['s'] * (L2 - 4)):
                continue
            dw = dw or bfs(T, w, blk)
            for u in unodes:
                L1 = dw.get(u, 99)
                if T.tag[u] != 'capstone' and 2 <= L1 <= 6 and side_of(T, area, u) * sg < -40 and \
                        arc_feasible(u, w, L1, ['s'] * (L1 - 1)):
                    out.append((-abs(side_of(T, area, u) - side_of(T, area, v)), L1 + L2, u, w, L1, v, L2))
    out.sort()
    return [x[2:] for x in out]


def place_roads(Pl, areas, offs, lo_min):
    """One road per area of `areas`, all with the same saving: the largest every area can reach."""
    feas = {}
    for sv in range(PROFANE_SAVE_MAX, lo_min - 1, -1):
        snap = Pl.snapshot()
        got = []
        for area in areas:
            res = None
            trials = [('arc',) + x for x in (arc_plans(Pl, area, sv)[:20] if area not in CLASSES else [])] + \
                [('join',) + x for x in road_plans(Pl, area, sv, feas)[:30]]
            for t in trials:
                sn = Pl.snapshot()
                if t[0] == 'arc':
                    res = try_arc_road(Pl, area, t[1], t[2], t[3], [o for _, o in offs[::4]], sv)
                else:
                    res = try_road(Pl, area, t[1], t[2], t[4], t[3], t[5], offs, sv)
                if res and min(res[0]) != sv:
                    Pl.restore(sn)
                    res = None
                if res:
                    break
            if not res:
                break
            got.append(res)
        if len(got) == len(areas):
            return got
        Pl.restore(snap)
        feas.clear()
    return None


# ---------------------------------------------------------------- Profane Roads, design v8 (Curtis approved 2026-10-18)
# Four one-way roads in a pinwheel (research/tree-rework/profane-road/design-v8.md): each runs from a door 15 points
# from its owner's start, through 8 nodes (small, small, toll, small, small, pact, greater small, greater small), to a
# far gate in the next class's ground. A hero enters only at the door; from the far gate the road can't be walked
# back. The lines are the clean 8-node lines the road measure found between the gates with only the nodes placed
# before the roads as obstacles (research/overnight-2026-10-18/roads-measure, lines_v7.pkl and lines_maiden314.pkl;
# 24 degree turns, 30 for Bartuc's and the Sickness). They may pass over lattice and cluster links like bridges
# (Curtis 2026-10-17); the set pieces and the fill grow around them. Content: C.ROADS.
PROFANE_ROADS = {
    # road: (door, far gate, the 8 nodes from the door), in internal ids and tree units
    'leoric': (331, 478, [(720.8, -276.2), (680.8, -356.2), (640.8, -436.2), (590.8, -536.2), (540.8, -636.2),
                          (490.8, -706.2), (430.8, -806.2), (390.8, -876.2)]),
    'bartuc': (280, 433, [(285.5, 745.6), (365.5, 725.6), (445.5, 705.6), (525.5, 685.6), (635.5, 655.6),
                          (725.5, 585.6), (795.5, 505.6), (855.5, 425.6)]),
    'sickness': (492, 448, [(-542.3, 582.9), (-542.3, 702.9), (-502.3, 792.9), (-472.3, 862.9), (-432.3, 952.9),
                            (-372.3, 1002.9), (-292.3, 1062.9), (-202.3, 1102.9)]),
    'maiden': (314, 463, [(-242.3, -856.2), (-362.3, -806.2), (-432.3, -776.2), (-542.3, -726.2), (-642.3, -666.2),
                          (-752.3, -606.2), (-862.3, -546.2), (-962.3, -486.2)]),
}
ROAD_KINDS = ['s', 's', 'N', 's', 's', 'N', 's', 's']


def place_class_roads(Pl):
    """The four roads of PROFANE_ROADS. T.road[i] = (road, position 1-8); T.road_back holds the steps the cost
    search may not take (back along a road, or from the far gate onto it). Spacing faults go to T.problems."""
    T = Pl.T
    T.road = {}
    T.road_back = set()
    T.notes['roads'] = {}
    for key, (door, far, pts) in PROFANE_ROADS.items():
        # the line: searched through the ground left free by everything placed so far; the drawn line is the
        # fallback (its spacing faults then go to T.problems)
        ob = RF.Obstacles(T.pos, T.kind, edge_segments(T), NODE_GAP, NOTABLE_GAP, LINK_CLEAR)
        got = RF.search(ob, T.pos[door], T.pos[far], door, far, len(ROAD_KINDS),
                        [k for k, kd in enumerate(ROAD_KINDS) if kd == 'N'])
        if got:
            pts = got[0]
            T.notes['roads'][key] = dict(tried=got[2], crossings=len(got[1]))
        else:
            T.notes['roads'][key] = dict(fallback=True)
        mk = T.mark()
        old = list(range(mk[0]))
        new = []
        for k, (q, kd) in enumerate(zip(pts, ROAD_KINDS)):
            near = min(old, key=lambda j: math.dist(T.pos[j], q))
            i = T.add(q, kd, 'profane', '', T.area[near])
            T.road[i] = (key, k + 1)
            new.append(i)
        chain = [door] + new + [far]
        for x, y in zip(chain, chain[1:]):
            T.link(x, y, 0.0, 'prof')
            T.road_back.add((y, x))
        segs = edge_segments(T, T.edges[mk[1]:])
        for i in new:
            for j in old:
                gap = NOTABLE_GAP if T.kind[i] == 'N' and T.kind[j] in 'NKJS' else NODE_GAP
                if math.dist(T.pos[i], T.pos[j]) < gap:
                    T.problems.append('%s road node %d is %.0f from node %d' % (key, i, math.dist(T.pos[i], T.pos[j]), j))
        for sg_ in segs:
            for j in old:
                if j not in (sg_[2], sg_[3]) and seg_dist(T.pos[j], sg_[0], sg_[1]) < LINK_CLEAR:
                    T.problems.append('%s road passes %.0f from node %d' % (key, seg_dist(T.pos[j], sg_[0], sg_[1]), j))
        commit_road(Pl, new, mk, C.ROADS[key]['owner'], (door, far), dict(road=key, u=door, v=far))
        T.problems = list(dict.fromkeys(T.problems))


def place_profane(Pl):
    """The border Profane roads (the four class roads of design v8 come later, see place_class_roads). The border
    notables stay as they were (design-v8 §1.9): the two live ones, the Horadric Archive and the Khanduras Muster,
    a matched pair placed by the old search (see the notes above try_road). If the pair finds no shared saving,
    each border keeps the best it reaches on its own."""
    T = Pl.T
    Pl.prof_keys = key_targets(T)
    T.notes['profane'] = {}
    offs = [(a_, b_) for b_ in (0.1, -0.1, 0.18, -0.18, 0.26, -0.26, 0.04, -0.04) for a_ in (0.1, -0.1, 0.2, -0.2)]
    pair = ['Horadric Archive', 'Khanduras Muster']
    best = place_roads(Pl, pair, offs, 1)
    if best:
        for area, g in zip(pair, best):
            T.notes['profane'][area] = dict(saving=g[0], hybrid=g[1], P=g[2])
    else:
        for area in pair:
            got = place_roads(Pl, [area], offs, 1)
            if got:
                g = got[0]
                T.notes['profane'][area] = dict(saving=g[0], hybrid=g[1], P=g[2])
            else:
                T.problems.append('no border Profane road in ' + area)
    Pl.rm = RoleMap(T, Pl.D)


# ---------------------------------------------------------------- the role fill (§5.4 step 5, Curtis's mix rulings)
THROUGH_MOTIFS = ['Bridge Arc', 'Crypt Arch', 'Twin Vault']
GEN_WEIGHTS = {'Stub': 8, 'Spur Arc': 6, 'Hook': 4, 'Fork': 16, 'Horns': 16, 'Split Arcs': 8, 'Bridge Arc': 12, 'Lens': 12,
               'Long Lens': 7, 'Teardrop': 9, 'Ring of Six': 7, 'Great Ring': 2, 'Wheel': 4, 'Crook': 4, 'Spiral': 3,
               'Figure Eight': 4, 'Rune Chain': 4, 'Triangle': 3, 'Pentagon': 2}
MODE_SHARE = {'orbit': 0.30, 'through': 0.10, 'stalk': 0.42, 'off': 0.18}
P_DIABLO = 0.45
SPUR_CAP = 0.28
STREAK_STOP = 200
DEPTH_W = [10, 5, 2]
SPOT_CLEAR = (44, 52, 60)
TWIN_SHARE = 0.8  # partner fill: share of tries that grow a two-notable shape (Curtis: two-notable and loop shapes)
OFF_CAP = 0.2     # off-cluster share (Curtis: at most 20-25%)
TOL = 1          # a partner notable may cost up to this much more than its role (never less): ranks stay within 1
TWIN = ('Fork', 'Horns', 'Split Arcs', 'Twin Vault')
TWIN_W = {'Fork': 2, 'Horns': 2, 'Split Arcs': 3}   # Split Arcs threads round its node (motif mix)
ORBIT_FIRST = 0.6   # partner fill: share of orbit-capable tries that thread round the node before trying a stalk
FG_DIABLO = 0.5     # partner fill: share of single-notable tries that use the area's Diablo shapes


def cluster_caps(T):
    n = max(len(T.clusters), 60)
    cnt = {}
    for cl in T.clusters:
        cnt[cl['name']] = cnt.get(cl['name'], 0) + 1
    spur = cnt.get('Stub', 0) + cnt.get('Spur Arc', 0)
    pic = sum(1 for cl in T.clusters if cl['name'] in PICTOGRAMS and cl['tag'] != 'set')

    def ok(name):
        if name in SPUR_FAMILY:
            return spur < SPUR_CAP * n
        if name in ('Fork', 'Horns'):
            return cnt.get(name, 0) < 0.13 * n
        if name in PICTOGRAMS and pic >= 0.13 * n:
            return False
        return cnt.get(name, 0) < 0.11 * n
    return ok


def attached_clusters(T):
    """For each lattice node, how many clusters hang from it."""
    out = {}
    for cl in T.clusters:
        for a in (cl['attach'], cl.get('second')):
            if a is not None and a < T.n_lat:
                out[a] = out.get(a, 0) + 1
    return out


def off_ok(T, x):
    """A cluster small another cluster may hang from: a free small of a cluster that hangs from the lattice itself
    (chains of clusters stay one deep)."""
    if T.kind[x] != 's' or x < T.n_lat or len(T.adj[x]) > 2 or not free_path(T, x):
        return False
    ci = T.cl_of[x]
    if ci < 0:
        return False
    cl = T.clusters[ci]
    return cl['mode'] != 'off' and cl['tag'] in ('cluster', 'set', 'crossover', 'style', 'defence', 'sweep')


def fill(Pl, rnd, node_target=1330, notable_target=236, max_tries=2600):
    """Grow clusters four at a time. Each try picks a role in the Warrior quarter (a lattice node of the Warrior
    area or the Zakarum Cloister, bare attribute nodes first, or a small of an earlier cluster there) whose four
    copies all still have open ground, a mode (orbit round the node, through between two nodes, stalk off a node,
    or off another cluster's small) by the share still missing, a motif by the mix rules and a variant. Each
    quarter then fits its own copy toward its own open side."""
    T, rm = Pl.T, Pl.rm
    by_area = {}
    for cl in T.clusters:
        by_area.setdefault((cl['area'], cl['name']), []).append(cl)
    T.fill_log = []
    base_areas = ('Warrior', 'Zakarum Cloister')
    score = {}
    streak = {m: 0 for m in MODE_SHARE}

    def n_main():
        return sum(1 for k in T.kind if k != 'S')

    def reuse_ok(area, name, centre, var):
        prev = by_area.get((area, name), [])
        lim = 1 if name in RARE else (7 if name in SPUR_FAMILY or name in TWIN else 4)
        if len(prev) >= lim:
            return False
        for cl in prev:
            if math.dist(cl['centre'], centre) < 120:
                return False
            if area in CLASSES and cl['mode'] != 'orbit':
                ax = CLASSES[area]
                s1, s2 = adiff(ang_of(cl['centre']), ax), adiff(ang_of(centre), ax)
                if s1 * s2 < 0 and abs(abs(s1) - abs(s2)) < 4 and abs(rad(cl['centre']) - rad(centre)) < 0.1 * R:
                    return False   # never the mirror image across the class axis
        return True

    def pick_motif(area, mode):
        cap_ok = cluster_caps(T)
        if mode == 'orbit':
            pool = {'Bridge Arc': 14, 'Split Arcs': 10, 'Hook': 4, 'Spur Arc': 4, 'Stub': 4}
        elif mode == 'through':
            pool = {n: 4 for n in THROUGH_MOTIFS}
        else:
            if rnd.random() < P_DIABLO:
                forms = [f for f in FORMS[area] if all(any(p == f[2] for _, _, p in FORMS[rot_area(area, r)])
                                                       for r in (1, 2, 3)) and cap_ok(f[0]) and len(f[2]) <= 2]
                if forms:
                    w = [3 if f[0] in CURVED_DIABLO else 1 for f in forms]
                    f = rnd.choices(forms, w)[0]
                    return f[0], list(f[1])
            pool = {n: w for n, w in GEN_WEIGHTS.items() if n in STALK_GENERIC or n in ('Horns',)}
        pool = {n: w for n, w in pool.items() if cap_ok(n)}
        if not pool:
            return None, None
        names = sorted(pool)
        return rnd.choices(names, [pool[n] for n in names])[0], None

    def pick_mode():
        n = len(T.clusters) + 1
        cnt = {m: sum(1 for cl in T.clusters if cl['mode'] == m) for m in MODE_SHARE}
        w = {m: MODE_SHARE[m] * min(3.0, max(0.3, 1 + (MODE_SHARE[m] * n - cnt[m]) / 4)) / (1 + streak[m] / 6)
             for m in MODE_SHARE}
        names = sorted(w)
        return rnd.choices(names, [w[m] for m in names])[0]

    def group_ok(j, test):
        return rm.full(j) and len({rm.get(j, r) for r in range(4)}) == 4 and all(test(rm.get(j, r)) for r in range(4))

    def role_score(j):
        if j not in score:
            score[j] = min(open_dirs(Pl, rm.get(j, r))[0][0] + 0.1 * sum(n for n, _ in open_dirs(Pl, rm.get(j, r))[:6])
                           for r in range(4))
        return score[j]

    def forget(ids):
        pts = [T.pos[i] for i in ids]
        for j in list(score):
            js = [T.pos[rm.get(j, r)] for r in range(4) if rm.get(j, r) is not None]
            if any(math.dist(p, q) < 330 for p in pts[::3] for q in js):
                del score[j]

    lat_pool = [i for i in range(T.n_lat) if area_by_cost(Pl.D, i) in base_areas and group_ok(i, lambda x: lattice_ok(T, x, 4))]
    tries = 0
    while tries < max_tries:
        tries += 1
        if T.kind.count('N') >= notable_target or n_main() >= node_target:
            break
        mode = pick_mode()
        att = attached_clusters(T)
        if mode == 'off':
            pool = [j for j in range(T.n_lat, len(T.pos)) if area_by_cost(Pl.D, j) in base_areas and group_ok(j, lambda x: off_ok(T, x))]
        elif mode == 'orbit':
            pool = [j for j in lat_pool if group_ok(j, lambda x: lattice_ok(T, x, 3))]
        else:
            pool = [j for j in lat_pool if group_ok(j, lambda x: lattice_ok(T, x, 4))]
        pool = [j for j in pool if role_score(j) > 0.05]
        if not pool:
            streak[mode] += 1
            if min(streak.values()) > STREAK_STOP:
                break
            continue
        # bare attribute nodes first (Curtis: clusters attach to bare attribute nodes first)
        pool.sort(key=lambda j: (min(att.get(rm.get(j, r), 0) for r in range(4)) if j < T.n_lat else 1, -role_score(j), j))
        top = pool[:24]
        at = rnd.choices(top, [role_score(j) + 0.2 + (2.0 if j < T.n_lat and att.get(j, 0) == 0 else 0) for j in top])[0]
        area = area_by_cost(Pl.D, at)
        res = None
        if mode == 'through':
            others = [y for y in lat_pool if y != at and y not in T.adj[at] and area_by_cost(Pl.D, y) == area
                      and 100 < math.dist(T.pos[at], T.pos[y]) < 230 and group_ok(y, lambda x: lattice_ok(T, x, 4))]
            if not others:
                streak[mode] += 1
                continue
            b = rnd.choice(others)
            name, _ = pick_motif(area, mode)
            if not name:
                continue
            m = MOTIFS[name]

            def attempt(r):
                ar, br = rm.get(at, r), rm.get(b, r)
                if br in T.adj[ar] or not 90 < math.dist(T.pos[ar], T.pos[br]) < 250:
                    return None
                area_r = rot_area(area, r)
                p1, p2 = T.pos[ar], T.pos[br]
                u = unit(p1, p2)
                nrm = (-u[1], u[0])
                for k in range(16):
                    side = nrm if k % 2 == 0 else (-nrm[0], -nrm[1])
                    g, h = rnd.uniform(28, 50), rnd.uniform(10, 55)
                    ta = (p1[0] + u[0] * g + side[0] * h, p1[1] + u[1] * g + side[1] * h)
                    tb = (p2[0] - u[0] * g + side[0] * h, p2[1] - u[1] * g + side[1] * h)
                    var = rand_var(rnd, rnd.random() < 0.62)
                    for mr in (False, True):
                        f = fit_two(name, m['entr'][0], m['entr'][1], ta, tb, mr, var, grow=side, smin=0.7, smax=1.45)
                        if not f:
                            continue
                        centre, rot, v2 = f
                        if v2['scale'] < fix_var(name, v2)['scale'] or not reuse_ok(area_r, name, centre, v2):
                            continue
                        ids = Pl.place(name, centre, rot, mr, v2, ar, 'through', area_r, second=br, att_bend=rand_bend(rnd))
                        if ids:
                            return ids
                return None
        else:
            name, kinds = pick_motif(area, 'stalk' if mode == 'off' else mode)
            if not name:
                continue
            var0 = rand_var(rnd, rnd.random() < 0.62)

            def try_orb(a, nm, area_r, n, kd=None):
                for k in range(n):
                    geom = orbit_geom(T, a, nm, rnd, orbit=rnd.choice((4, 4, 5, 3)))
                    if not geom:
                        continue
                    var = dict(scale=round(ORB[geom[4]] / ORB[2], 2), orbit=True, orb=geom[4])
                    if not reuse_ok(area_r, nm, T.pos[a], var):
                        return None
                    ids = Pl.place(nm, T.pos[a], 0, False, var, a, 'orbit', area_r, geom=geom)
                    if ids:
                        return ids
                return None

            def try_sweep(a, nm, kd, area_r, md):
                dirs = [d for n, d in open_dirs(Pl, a) if n >= 1][:7]
                for k, (d, dv, g) in enumerate((d, dv, g) for d in dirs for dv in (0, 12, -12) for g in (32, 44)):
                    mr = rnd.random() < 0.5
                    var = fix_var(nm, var0 if k % 3 == 0 else rand_var(rnd, rnd.random() < 0.62))
                    centre, rot = stalk_geom(nm, T.pos[a], d + dv, g, mr, var, rnd.choice((-1, 1)) * rnd.uniform(8, 22))
                    if not reuse_ok(area_r, nm, centre, var):
                        continue
                    ids = Pl.place(nm, centre, rot, mr, var, a, md, area_r, kinds=kd, att_bend=rand_bend(rnd))
                    if ids:
                        return ids
                return None

            def attempt(r):
                a = rm.get(at, r)
                area_r = rot_area(area, r)
                nm, kd = (name, kinds) if r == 0 else image_form(name, kinds, area_r, rnd)
                if not nm:
                    return None
                if mode == 'orbit':
                    if nm not in ORBIT_FORM:
                        return None
                    return try_orb(a, nm, area_r, 12)
                return try_sweep(a, nm, kd, area_r, mode)
        snap_j = Pl.snapshot()
        jc0 = jewel_counts(T)
        res = group_place(Pl, attempt)
        if res and jewel_over(T, jc0):
            Pl.restore(snap_j)
            res = None
        if res:
            for cl in T.clusters[-4:]:
                by_area.setdefault((cl['area'], cl['name']), []).append(cl)
            forget([i for ids in res for i in ids])
            T.fill_log.append((tries, n_main(), T.kind.count('N')))
            streak[mode] = 0
        else:
            streak[mode] += 1
            score[at] = role_score(at) * 0.6
    T.fill_tries = tries


def empty_spot(Pl, rnd, clear=52):
    T = Pl.T
    for _ in range(40):
        r = math.sqrt(rnd.uniform((0.4 * R) ** 2, (R - 40) ** 2))
        a = rnd.uniform(0, 360)
        if r > rim_at(a) - 25:
            continue
        p = pol(r, a)
        if in_pocket(p) or Pl.moat(p):
            continue
        if any(math.dist(p, T.pos[j]) < clear for j in Pl.g.near_nodes(p, clear)):
            continue
        if any(seg_dist(p, s_[0], s_[1]) < 0.7 * clear for s_ in Pl.g.near_segs(p, p, clear)):
            continue
        return p
    return None


SPOT_PICK = 3


def spot_cost(Pl, q):
    T = Pl.T
    near = [j for j in Pl.g.near_nodes(q, 160)]
    if not near:
        return 99
    j = min(near, key=lambda j: (math.dist(q, T.pos[j]), j))
    return min(Pl.D[c].get(j, 99) for c in ORDER)


def shape_key(v):
    return tuple(x - v[0] for x in v[1:])


RANK_MAX = 30     # the balance check compares notable ranks costing up to this many points


def vmatch(a, t, add=0):
    """Partner notable costs a (plus add) against role costs t: per class both above RANK_MAX (the rank check
    never sees them), or both at most RANK_MAX with a costing 0 to TOL more."""
    for x, y in zip(a, t):
        x += add
        if x > RANK_MAX and y > RANK_MAX:
            continue
        if x > RANK_MAX or y > RANK_MAX or not 0 <= x - y <= TOL:
            return False
    return True


def min_depth(D, a, area):
    """The fewest steps a notable must sit past node a to meet the area floors."""
    v = {c: D[c].get(a, 999) for c in ORDER}
    if area in CLASSES:
        need = [6 - v[area], 40 - v[opposite(area)]] + [26 - v[x] for x in neighbours(area)]
    else:
        _, c1, c2 = BORDERS[area]
        need = [16 - min(v[c1], v[c2])]
    return max([1] + need)


def fill_groups(Pl, rnd, node_target=1370, notable_target=240, max_tries=20000, max_fails=2500, near=None,
                spot_clear=None):
    """Spot-first partner fill: grow one cluster into open ground anywhere, then give it three partners, one per
    other quarter, whose notable has the same costs with the classes turned a quarter. A partner may hang from any
    node whose costs are the partner notable's minus the same number of points for every class. All four or
    nothing. near: optional (point list, radius) to grow only there (the socket top-up)."""
    T = Pl.T
    by_area = {}
    for cl in T.clusters:
        by_area.setdefault((cl['area'], cl['name']), []).append(cl)
    cache = {}

    def n_main():
        return sum(1 for k in T.kind if k != 'S')

    def index():
        if cache.get('n') != (len(T.pos), len(T.edges)):
            idx = {}
            D = Pl.D
            for y in range(len(T.pos)):
                if T.kind[y] in 'Ss':
                    if T.kind[y] == 'S' or not off_ok(T, y):
                        continue
                elif not lattice_ok(T, y, 4):
                    continue
                v = cost_vec(D, y)
                idx.setdefault(shape_key(v), []).append((v[0], y))
            cache['idx'], cache['n'] = idx, (len(T.pos), len(T.edges))
        return cache['idx']

    def reuse_ok(area, name, centre):
        prev = by_area.get((area, name), [])
        lim = 1 if name in RARE else (12 if name in SPUR_FAMILY or name in TWIN else 6)
        return len(prev) < lim and all(math.dist(cl['centre'], centre) >= 110 for cl in prev)

    def shapes_for(area, depth, diablo):
        cap_ok = cluster_caps(T)
        out = []
        if diablo:
            out += [(n, list(k)) for n, k, p in FORMS.get(area, []) if p == (depth,) and cap_ok(n)]
            rnd.shuffle(out)
        gen = [n for n in STALK_GENERIC if n_depths(n) == (depth,) and n in GEN_WEIGHTS and cap_ok(n)]
        rnd.shuffle(gen)
        # straight-edged pictograms last (curved links: clusters >= 70%; pictograms <= 15%)
        gen.sort(key=lambda n: (n in SPUR_FAMILY) * 2 + (n in PICTOGRAMS) * 2.5 + len(MOTIFS[n]['nodes']) * 0.3
                 + rnd.uniform(0, 2.5))
        return out + [(n, None) for n in gen]

    def try_orbit_at(a, name, area):
        for _ in range(6):
            geom = orbit_geom(T, a, name, rnd, orbit=rnd.choice((3, 4, 4, 5)))
            if not geom:
                continue
            var = dict(scale=round(ORB[geom[4]] / ORB[2], 2), orbit=True, orb=geom[4])
            ids = Pl.place(name, T.pos[a], 0, False, var, a, 'orbit', area, geom=geom)
            if ids:
                return ids
        return None

    def place_at(a, name, kinds, area, d_pref=None, tries=8, spot=None):
        md = 'off' if a >= T.n_lat else 'stalk'
        can_orbit = md == 'stalk' and name in ORBIT_FORM and lattice_ok(T, a, 3) and (kinds is None or kinds.count('N') == 1
                                                                                     or name == 'Split Arcs')
        # threaded first (motif mix: orbit or pass-through >= 40%), then the stalk toward the open spot
        if can_orbit and rnd.random() < ORBIT_FIRST:
            ids = try_orbit_at(a, name, area)
            if ids:
                return ids
        if spot is not None:
            # centre the motif on the open spot, entrance toward the attach node
            ap = T.pos[a]
            d = heading(ap, spot)
            for k in range(6):
                var = fix_var(name, rand_var(rnd, rnd.random() < 0.62, scale=rnd.choice((0.8, 0.86, 0.92, 1.0))))
                mr = rnd.random() < 0.5
                tilt = rnd.choice((-1, 1)) * rnd.uniform(6, 20)
                c0, rot = stalk_geom(name, ap, d, 0, mr, var, tilt)
                proj = (c0[0] - ap[0]) * math.cos(math.radians(d)) + (c0[1] - ap[1]) * math.sin(math.radians(d))
                g = math.dist(ap, spot) - proj + rnd.uniform(-8, 8)
                if not 26 <= g <= 100:
                    continue
                centre, rot = stalk_geom(name, ap, d + rnd.uniform(-6, 6), g, mr, var, tilt)
                if not reuse_ok(area, name, centre):
                    continue
                ids = Pl.place(name, centre, rot, mr, var, a, md, area, kinds=kinds, att_bend=rand_bend(rnd))
                if ids:
                    return ids
        can_orbit = md == 'stalk' and name in ORBIT_FORM and lattice_ok(T, a, 3) and (kinds is None or kinds.count('N') == 1
                                                                                     or name == 'Split Arcs')
        if can_orbit and (rnd.random() < 0.8 or name == 'Split Arcs'):
            ids = try_orbit_at(a, name, area)
            if ids:
                return ids
            if name == 'Split Arcs':
                return None
        dirs = [d for n, d in open_dirs(Pl, a) if n >= 1][:5]
        if d_pref is not None:
            dirs = [d_pref] + dirs
        k = 0
        for d in dirs:
            for g in (32, 44):
                k += 1
                if k > tries * 2:
                    break
                var = fix_var(name, rand_var(rnd, rnd.random() < 0.62))
                mr = rnd.random() < 0.5
                centre, rot = stalk_geom(name, T.pos[a], d + rnd.uniform(-12, 12), g, mr, var,
                                         rnd.choice((-1, 1)) * rnd.uniform(8, 22))
                if not reuse_ok(area, name, centre):
                    continue
                ids = Pl.place(name, centre, rot, mr, var, a, md, area, kinds=kinds, att_bend=rand_bend(rnd))
                if ids:
                    return ids
        return None

    tries = fails = 0
    st = T.fg_stats = {}

    def stat(k):
        st[k] = st.get(k, 0) + 1
    while tries < max_tries and n_main() < node_target and fails < max_fails:
        tries += 1
        if T.kind.count('N') >= notable_target:
            break
        if near:
            pts, rr = near
            cen = rnd.choice(pts)
            spot = None
            for _ in range(20):
                q = (cen[0] + rnd.uniform(-rr, rr), cen[1] + rnd.uniform(-rr, rr))
                if math.dist(q, cen) < rr and not any(math.dist(q, T.pos[j]) < 44 for j in Pl.g.near_nodes(q, 44)) \
                        and Pl.node_ok(q) and not Pl.moat(q):
                    spot = q
                    break
        else:
            # of SPOT_PICK open spots take the cheapest one (its nearest node's lowest class cost), so cheap ground
            # fills first (Curtis: notables within 15 / 20 points)
            spots = [empty_spot(Pl, rnd, clear=rnd.choice(spot_clear or SPOT_CLEAR)) for _ in range(SPOT_PICK)]
            spots = [q for q in spots if q]
            spot = min(spots, key=lambda q: (spot_cost(Pl, q), q)) if spots else None
        if not spot:
            stat('nospot')
            fails += 1
            continue
        nearby = sorted((j for j in Pl.g.near_nodes(spot, 150) if math.dist(spot, T.pos[j]) < 150),
                        key=lambda j: (math.dist(spot, T.pos[j]), j))
        off = rnd.random() < 0.18 and sum(1 for cl in T.clusters if cl['mode'] == 'off') < OFF_CAP * len(T.clusters)
        att = attached_clusters(T)
        cand = [j for j in nearby if (off_ok(T, j) if off else lattice_ok(T, j, 4))]
        if not off:
            cand.sort(key=lambda j: (att.get(j, 0) > 0, math.dist(spot, T.pos[j])))
        if not cand:
            stat('nocand')
            fails += 1
            continue
        at = cand[0] if rnd.random() < 0.6 else rnd.choice(cand[:4])
        area = area_by_cost(Pl.D, at)
        diablo = rnd.random() < FG_DIABLO
        md0 = min_depth(Pl.D, at, area)
        depth = max(rnd.choices([3, 4, 5], DEPTH_W)[0], md0)
        if depth > 5:
            stat('floor')
            fails += 1
            continue
        opts = shapes_for(area, depth, diablo)
        if rnd.random() < TWIN_SHARE:
            cap_ok = cluster_caps(T)
            twin = [n for n in ('Fork', 'Horns', 'Split Arcs') if cap_ok(n) and (n != 'Split Arcs' or not off)
                    and min(n_depths(n)) >= md0]
            opts = [(rnd.choices(twin, [TWIN_W[n] for n in twin])[0], None)] if twin else opts
        if not opts:
            continue
        name, kinds = opts[0]
        multi = len(n_depths(name, kinds)) > 1
        snap = Pl.snapshot()
        jc0 = jewel_counts(T)
        n0 = len(T.clusters)
        ap = T.pos[at]
        ids = place_at(at, name, kinds, area, math.degrees(math.atan2(spot[1] - ap[1], spot[0] - ap[0])), spot=spot)
        for at2 in cand[1:3]:
            if ids:
                break
            at = at2
            area = area_by_cost(Pl.D, at)
            ap = T.pos[at]
            if multi or kinds:
                break
            ids = place_at(at, name, kinds, area, math.degrees(math.atan2(spot[1] - ap[1], spot[0] - ap[0])), spot=spot)
        if not ids:
            stat('first')
            fails += 1
            continue
        v0s = sorted(cost_vec(Pl.D, i) for i in ids if T.kind[i] == 'N')
        v0 = v0s[0]
        va = cost_vec(Pl.D, at)
        ok = True
        allc = [(v, y) for vs in index().values() for _, y in vs for v in [cost_vec(Pl.D, y)]]
        for r in (1, 2, 3):
            t = rot_vec(v0, r)
            area_r = rot_area(area, r)
            got = None
            if multi:
                ta = rot_vec(va, r)
                cands = [(sum(v) - sum(ta), y) for v, y in allc if vmatch(v, ta)]
                rnd.shuffle(cands)
                cands.sort(key=lambda dy: dy[0])
                trials = [(0, y) for _, y in cands[:8]]
            else:
                cands = [(dd, y, sum(min(x + dd, RANK_MAX + 1) for x in v) - sum(min(x, RANK_MAX + 1) for x in t))
                         for v, y in allc for dd in (3, 4, 5) if vmatch(v, t, dd)]
                rnd.shuffle(cands)
                cands.sort(key=lambda c: (c[2], c[0] != depth))
                trials = [(dd, y) for dd, y, _ in cands[:10]]
            off_full = sum(1 for cl in T.clusters if cl['mode'] == 'off') >= OFF_CAP * len(T.clusters)
            for dd, y in trials:
                if not (lattice_ok(T, y, 4) if T.kind[y] not in 'Ss' else (off_ok(T, y) and not off_full)):
                    continue
                sn = Pl.snapshot()
                if multi:
                    nm2, kd2 = image_form(name, kinds, area_r, rnd)
                    got = place_at(y, nm2, kd2, area_r, tries=6) if nm2 else None
                    want = [rot_vec(v, r) for v in v0s]
                else:
                    got = None
                    for nm, kd in shapes_for(area_r, dd, diablo or MOTIFS[name]['diablo'])[:3]:
                        got = place_at(y, nm, kd, area_r, tries=5)
                        if got:
                            break
                    want = [t]
                if got:
                    vs = sorted(cost_vec(Pl.D, i) for i in got if T.kind[i] == 'N')
                    if len(vs) != len(want) or not all(vmatch(a, b) for a, b in zip(vs, want)):
                        Pl.restore(sn)
                        got = None
                if got:
                    break
            if not got:
                stat('partner%d%s' % (r, '' if trials else '-none'))
                ok = False
                break
        if ok and jewel_over(T, jc0):
            stat('jewel')
            ok = False
        if not ok:
            Pl.restore(snap)
            fails += 1
            continue
        Pl.gid += 1
        for q, cl in enumerate(T.clusters[n0:]):
            cl['group'] = Pl.gid
            cl['q'] = q
            by_area.setdefault((cl['area'], cl['name']), []).append(cl)
        Pl.rm.map_nodes(T, Pl.D, [i for cl in T.clusters[n0:] for i in cl['ids']])
        fails = 0
    return tries


JEWEL_CAP = 5   # check 14a: at most this many notables in a jewel radius (0.13 R)


def jewel_groups(T):
    """The 12 sockets in their three role groups of four (own, border, deep)."""
    return [[T.key[c]['J'] for c in ORDER], [T.key[b]['B'] for b in BORDERS], [T.key[c]['D'] for c in ORDER]]


def jewel_counts(T):
    nots = [i for i in range(len(T.pos)) if T.kind[i] == 'N']
    return [[sum(1 for i in nots if math.dist(T.pos[i], T.pos[j]) < 0.13 * R) for j in g] for g in jewel_groups(T)]


def jewel_over(T, before):
    """Check 14a: True when a placement pushed a socket's jewel radius past JEWEL_CAP notables."""
    now = jewel_counts(T)
    for g, g0 in zip(now, before):
        if any(n > max(JEWEL_CAP, b) for n, b in zip(g, g0)):
            return True
    return False


def socket_notables(T, j, nots=None):
    nots = nots if nots is not None else [i for i in range(len(T.pos)) if T.kind[i] == 'N']
    return sum(1 for i in nots if math.dist(T.pos[i], T.pos[j]) < 0.13 * R)


def socket_topup(Pl, rnd):
    """Every socket gets 3-5 notables within its jewel radius (0.13 R), and the sockets of one role match within
    one: clusters are grown near the socket that has the fewest, four at a time."""
    T = Pl.T
    for _ in range(5):
        for base in socket_roles(T):
            socks = [Pl.rm.get(base, r) for r in range(4)]
            cnt = [socket_notables(T, j) for j in socks]
            if min(cnt) >= 3:
                continue
            fill_groups(Pl, rnd, node_target=1400, notable_target=260, max_tries=800, max_fails=300,
                        near=([T.pos[j] for j in socks if socket_notables(T, j) < 3], 0.13 * R - 30))


def promote(Pl, target, rnd):
    """Promote cluster smalls to notables, four at a time (one per cluster of a matched group of four, so one per
    quarter), until the notable count reaches the target. A promoted small is a dead end or sits on a loop (never a
    cut vertex), keeps 2-5 lead-in smalls (3-6 steps from the nearest attribute or socket node), touches no
    notable, keeps 1.5+ smalls per notable in its cluster and passes the area floors; the four costs match up to a
    quarter turn within TOL (round-2 balance must-fix 4: class areas at 12-22 points first)."""
    T = Pl.T
    aj = {i for i in range(len(T.pos)) if T.kind[i] in 'AJ'}
    dA = steps_from(T, aj)

    def ok(i):
        if T.cl_of[i] < 0 or T.kind[i] != 's':
            return False
        cl = T.clusters[T.cl_of[i]]
        if cl['tag'] not in ('cluster', 'set', 'sweep') or not 3 <= dA.get(i, 0) <= 6:
            return False
        if any(T.kind[j] == 'N' for j in T.adj[i]) or not notable_room(Pl, i):
            return False
        ks = [T.kind[j] for j in cl['ids']]
        if ks.count('s') - 1 < 1.5 * (ks.count('N') + 1):
            return False
        return not is_cut(T, i)

    def back(i, q):
        return rot_vec(cost_vec(Pl.D, i), (4 - q) % 4)

    groups = {}
    for ci, cl in enumerate(T.clusters):
        if cl.get('group') is not None and cl.get('q') is not None:
            groups.setdefault(cl['group'], []).append(ci)
    n = T.kind.count('N')
    T.promoted = []

    needy = []

    def pref(i):
        cl = T.clusters[T.cl_of[i]]
        own = Pl.D[cl['area']].get(i, 99) if cl['area'] in CLASSES else 99
        # a socket with fewer than 3 notables in its jewel radius first (check 14a), then 12-22 points
        return (not any(math.dist(T.pos[i], p) < 0.13 * R for p in needy), not 12 <= own <= 22,
                len(T.adj[i]) != 1, -dA[i])

    order = sorted(groups)
    rnd.shuffle(order)
    progress = True
    while n < target and progress:
        progress = False
        jc = jewel_counts(T)
        needy[:] = [T.pos[j] for grp, cnt in zip(jewel_groups(T), jc) for j, c_ in zip(grp, cnt) if c_ < 3]
        for g in order:
            if n >= target:
                break
            cis = sorted(groups[g], key=lambda c: T.clusters[c]['q'])
            if len(cis) != 4:
                continue
            found = None
            for k0, c0 in enumerate(cis):
                q0 = T.clusters[c0]['q']
                xs = sorted((x for x in T.clusters[c0]['ids'] if ok(x)), key=pref)
                for x in xs:
                    ux = back(x, q0)
                    pick = [x]
                    for c in cis:
                        if c == c0:
                            continue
                        q = T.clusters[c]['q']
                        ys = [y for y in T.clusters[c]['ids'] if ok(y) and vmatch(back(y, q), ux)]
                        if not ys:
                            break
                        pick.append(sorted(ys, key=pref)[0])
                    if len(pick) == 4:
                        jc0 = jewel_counts(T)
                        for y in pick:
                            T.kind[y] = 'N'
                        if all(Pl.floors_ok(Pl.D, [y], T.clusters[T.cl_of[y]]['area']) for y in pick) and \
                                not any(T.kind[z] == 'N' for y in pick for z in T.adj[y]) and not jewel_over(T, jc0):
                            found = pick
                            break
                        for y in pick:
                            T.kind[y] = 's'
                if found:
                    break
            if found:
                for y in found:
                    T.clusters[T.cl_of[y]].setdefault('promoted', []).append(y)
                T.promoted.append(found)
                n += 4
                progress = True
    return n


# ---------------------------------------------------------------- the whole build
FILL_SEEDS = 4   # the fill is run from this many fixed seeds (in parallel when it can) and the best kept


def fill_one(Pl, k):
    """§5.4 steps 5-7 from fill seed k: the role fill, the partner fill (wide spots, then tight ones), the socket
    top-up and the promotions in fours up to 240 notables."""
    T = Pl.T
    rnd = random.Random(SEED + 2 + 1000 * k)
    fill(Pl, rnd)
    fill_groups(Pl, rnd, spot_clear=(44, 52, 60), max_fails=5000, node_target=1395)
    fill_groups(Pl, rnd, spot_clear=(36, 40, 44), max_fails=4000, node_target=1395)
    socket_topup(Pl, rnd)
    promote(Pl, 240, rnd)
    T.fill_done = True
    T.fill_seed = k
    return Pl


def fill_score(Pl):
    """Which fill to keep: the counts in range first (main 1,300-1,400, notables 225+), then how many of Curtis's
    reach targets hold (notables within 15 / 20 points >= 19 / 34 for every class; 3-5 notables in every jewel
    radius), then the most notables, then the main tree nearest 1,350."""
    T = Pl.T
    n_not = T.kind.count('N')
    n_main = sum(1 for x in T.kind if x != 'S')
    D = all_costs(T)
    nots = [i for i in range(len(T.pos)) if T.kind[i] == 'N']
    w15 = min(sum(1 for i in nots if D[c].get(i, 99) <= 15) for c in ORDER)
    w20 = min(sum(1 for i in nots if D[c].get(i, 99) <= 20) for c in ORDER)
    jc = [x for g in jewel_counts(T) for x in g]
    reach = (w15 >= 19) + (w20 >= 34) + (3 <= min(jc) and max(jc) <= 5)
    return (1300 <= n_main <= 1400 and n_not >= 225, reach, min(n_not, 255), -abs(n_main - 1350))


_FILL_BASE = None


def _fill_worker(k):
    import pickle
    return pickle.dumps(fill_one(_FILL_BASE, k), protocol=pickle.HIGHEST_PROTOCOL)


def fill_all(Pl, seeds=None):
    """Runs the fill from FILL_SEEDS fixed seeds and keeps the one with the most notables (ties: main tree size
    nearest 1,350, then the lower seed). Deterministic: the seeds are fixed and the choice does not depend on
    timing. Returns the Placer of the kept fill."""
    import pickle
    global _FILL_BASE
    seeds = FILL_SEEDS if seeds is None else seeds
    blob = pickle.dumps(Pl, protocol=pickle.HIGHEST_PROTOCOL)
    results = None
    if seeds > 1:
        try:
            import multiprocessing as mp
            ctx = mp.get_context('fork')
            _FILL_BASE = Pl
            with ctx.Pool(min(seeds, os.cpu_count() or 1)) as pool:
                results = [pickle.loads(b) for b in pool.map(_fill_worker, range(seeds))]
        except (ImportError, ValueError, OSError):
            results = None
        finally:
            _FILL_BASE = None
    if results is None:
        results = [fill_one(pickle.loads(blob), k) for k in range(seeds)]
    best = max(range(seeds), key=lambda k: (fill_score(results[k]), -k))
    out = results[best]
    out.T.fill_scores = [(k, fill_score(results[k])) for k in range(seeds)]
    return out


def build(quick=False, seeds=None, layout_cache=None):
    """Lays out the whole tree. Returns the Tree with costs (T.D plain, T.Dp with the Profane roads), areas,
    travel stats, roles and content dealt. layout_cache: a file that keeps the placed layout (content is dealt after
    the layout and never changes it), so a content-only change reuses it instead of laying the tree out again."""
    import pickle
    if layout_cache and os.path.exists(layout_cache):
        with open(layout_cache, 'rb') as f:
            return finish(pickle.load(f))
    T = build_lattice()
    T.kites = kite_polys(T)
    T.n_lat = len(T.pos)
    T.notes = {}
    assign_areas(T)
    Pl = Placer(T)
    Pl.rm = RoleMap(T, Pl.D)
    for f in (place_scenic, place_socket_sweeps, place_openings, place_crossovers, place_profane):
        f(Pl)
    # the border set pieces, then the four class roads round them, then the class set pieces round the roads (a
    # class set piece hangs off the road's door, and turns aside more easily than a road; the roads cross the
    # borders, where the matched fours of set pieces have the least room)
    place_set_pieces(Pl, ['Zakarum Cloister'])
    after_fill = os.environ.get('POE2_ROADS') == 'after_fill'
    if not after_fill:
        place_class_roads(Pl)
    place_set_pieces(Pl, ['Warrior'])
    if not quick:
        Pl = fill_all(Pl, seeds)
    if after_fill:
        place_class_roads(Pl)
    if layout_cache:
        with open(layout_cache, 'wb') as f:
            pickle.dump(Pl, f, protocol=pickle.HIGHEST_PROTOCOL)
    return finish(Pl)


def finish(Pl):
    """Costs, areas, travel stats, roles and content on a placed tree."""
    T = Pl.T
    T.D = all_costs(T)
    T.Dp = all_costs(T, profane=True)
    for i in range(len(T.pos)):
        if i in T.D['Warrior'] or any(i in T.D[c] for c in ORDER):
            T.area[i] = area_by_cost(T.D, i)
        elif T.cl_of[i] >= 0:
            T.area[i] = T.clusters[T.cl_of[i]]['area']
    T.rm = Pl.rm
    assign_travel(T)
    assign_roles(T)
    deal_content(T)
    return T


# ---------------------------------------------------------------- travel stats (balance must-fix 3)
TRAVEL_SPINE = ('exit', 'rail', 'spoke', 'lateral', 'spine', 'rim', 'frame', 'corner', 'junction', 'socket', 'deep',
                'bsocket', 'start')


def assign_travel(T):
    """Travel smalls (lattice beads). On the landmark spine (rails, spokes, laterals, border spines, rim spurs) the
    stats rotate strictly Life, Res, Mana in the order the area's owner meets them (by point cost, then from the
    anticlockwise side); every class's quarter is dealt the same way, role by role. On the rings and braids every
    second bead carries the area's flavour stat, the rest keep the rotation."""
    D = T.D
    rot = C.TRAVEL_ROTATION

    def order_key(i, area):
        own = owners_of(area)
        return (min(D[c].get(i, 999) for c in own), -side_of(T, area, i), i)

    for area in list(ORDER) + list(BORDERS):
        beads = [i for i in range(T.n_lat) if T.kind[i] == 't' and T.area[i] == area]
        spine = sorted((i for i in beads if T.tag[i] not in ('mid', 'outer', 'braid')), key=lambda i: order_key(i, area))
        ring = sorted((i for i in beads if T.tag[i] in ('mid', 'outer', 'braid')), key=lambda i: order_key(i, area))
        for k, i in enumerate(spine):
            T.stat[i] = rot[k % 3]
        for k, i in enumerate(ring):
            T.stat[i] = C.TRAVEL_FLAVOUR[area] if k % 2 == 0 else rot[(k // 2) % 3]
    # copy the Warrior quarter (and the Zakarum Cloister) to their role images, so every class is dealt alike
    rm = T.rm
    for base in ('Warrior', 'Zakarum Cloister'):
        for i in range(T.n_lat):
            if T.kind[i] == 't' and T.area[i] == base and rm.full(i):
                for r in (1, 2, 3):
                    j = rm.get(i, r)
                    if T.kind[j] == 't' and T.tag[j] not in ('mid', 'outer', 'braid'):
                        T.stat[j] = T.stat[i]


# ---------------------------------------------------------------- roles and content (§11 Content, fun must-fixes)
def node_side(T, area, i):
    return 0 if side_of(T, area, i) >= 0 else 1      # 0: anticlockwise side, as the STYLE and OPENERS tables


def assign_roles(T):
    """Every notable gets T.role[i] = (role, family, side). See poe2_tree_content.py for the role list."""
    T.role = {}
    D = T.D
    nots = [i for i in range(len(T.pos)) if T.kind[i] == 'N']
    for c in ORDER:
        k = T.key[c]
        for side, n in enumerate(k['N']):
            T.role[n] = ('opener', C.OPENERS[c][side], side)
        for side in (0, 1):
            ids = T.notes['style'].get((c, side))
            fam = C.STYLE[c][side][1]
            for i in (ids or []):
                if T.kind[i] == 'N':
                    T.role[i] = ('style', fam, side)
        for i in (T.notes['defence'].get(c) or []):
            if T.kind[i] == 'N':
                T.role[i] = ('defence', C.DEFENCE[c], node_side(T, c, i))
    for i in nots:
        if i in T.role:
            continue
        cl = T.clusters[T.cl_of[i]] if T.cl_of[i] >= 0 else None
        area = T.area[i]
        tag = T.tag[i] if cl is None else cl['tag']
        side = node_side(T, area, i)
        if i in getattr(T, 'road', {}):
            T.role[i] = ('road', T.road[i][0], side)
        elif T.tag[i] == 'capstone':
            T.role[i] = ('capstone', C.STYLE[area][side][1] if area in CLASSES else '', side)
        elif tag == 'profane':
            T.role[i] = ('profane', area, side)
        elif tag == 'crossover':
            b = T.key[cl['area']]['acw'] if side == 0 else T.key[cl['area']]['cw']
            weapon = C.CROSSOVER_WEAPON[cl['area']]
            if weapon[0] == b:
                T.role[i] = ('crossover-weapon', weapon[1], side)
            else:
                T.role[i] = ('crossover', b, side)
        elif tag == 'set':
            T.role[i] = ('set', area, side)
        elif tag == 'sweep':
            T.role[i] = ('socket', area, side)
        elif tag == 'scenic':
            T.role[i] = ('scenic', area, side)
        else:
            T.role[i] = ('new', area, side)
    # family roads: per class side, at least 4 more notables of the style family on that side, 11-25 points,
    # chosen along the way from the style cluster to that side's capstone (fun must-fix 2)
    T.family_roads = {}
    for c in ORDER:
        k = T.key[c]
        for side in (0, 1):
            fam = C.STYLE[c][side][1]
            style = [i for i in nots if T.role.get(i, ('',))[0] == 'style' and T.role[i][2] == side and T.area[i] == c]
            caps = [i for i in k.get('cap', []) if node_side(T, c, i) == side]
            if not style or not caps:
                continue
            path = shortest_path_nodes(T, style[0], caps[0])
            dpath = steps_from(T, set(path))
            cand = [i for i in nots if T.role[i][0] in ('new', 'set', 'socket') and T.area[i] == c
                    and node_side(T, c, i) == side and 11 <= D[c].get(i, 999) <= 25]
            cand.sort(key=lambda i: (dpath.get(i, 99), D[c][i], i))
            pick = cand[:6]
            for i in pick:
                T.role[i] = ('family', fam, side) if T.role[i][0] == 'new' else (T.role[i][0] + '+family', fam, side)
            T.family_roads[(c, side)] = sorted(pick, key=lambda i: D[c][i]) + caps[:1]


def shortest_path_nodes(T, a, b):
    prev = {a: None}
    q = deque([a])
    while q:
        u = q.popleft()
        if u == b:
            break
        for v in sorted(T.adj[u]):
            if v not in prev and (T.kind[v] != 'S' or v == b):
                prev[v] = u
                q.append(v)
    if b not in prev:
        return [a]
    out = [b]
    while prev[out[-1]] is not None:
        out.append(prev[out[-1]])
    return out[::-1]


SIDE_WORD = ('anticlockwise', 'clockwise')


def placeholder(role, family, side):
    return '<%s: %s, %s>' % (role, family, SIDE_WORD[side])


def name_ok(nm):
    return len(nm) <= C.NAME_MAX and not any(w.lower() in nm.lower().split() for w in C.MECHANIC_WORDS)


def deal_content(T):
    """Names, stats and lore. Existing content (poe2_tree_content.py CLUSTERS / BORDER_CLUSTERS) is dealt into the
    notable slots by role: openers, style, defence and the family roads first, then the area's other families by band
    (mid families to the nearer notables, outer to the farther). Slots left over get a placeholder name
    '<role: family, side>' and no stats: the content pass writes them (no final names or numbers here)."""
    T.name = [''] * len(T.pos)
    T.lore = [''] * len(T.pos)
    T.stats = [[] for _ in range(len(T.pos))]
    T.family_of = {}
    T.unused_content = []
    D = T.D
    fams = {}
    for area, lst in list(C.CLUSTERS.items()) + list(C.BORDER_CLUSTERS.items()):
        for f in lst:
            fams[f['key']] = (area, f)
    cut = {nm for lst in getattr(C, 'CUT', {}).values() for nm in lst}
    left = {key: [x for x in f['notables'] if x[0] not in cut] for key, (area, f) in fams.items()}

    pairs_used = {}

    def pair_of(st):
        return stat_pair(st)

    def give(i, key, role):
        if left.get(key):
            # check 27: no two notables of one area share the same two stat keys; take the family's next notable
            # that keeps this, and the family's first only for the fixed roles (openers, style, defence)
            used = pairs_used.setdefault(T.area[i], set())
            # names that break the check 26 rules (mechanic words, over 24 characters) are held back for the content
            # pass to rename; their slots get placeholders
            pick = next((k_ for k_, (nm_, st, _) in enumerate(left[key]) if pair_of(st) not in used and name_ok(nm_)), None)
            if pick is None:
                if role not in ('opener', 'style', 'defence'):
                    return False
                pick = next((k_ for k_, (nm_, _, _) in enumerate(left[key]) if name_ok(nm_)), 0)
            nm, st, lore = left[key].pop(pick)
            if pair_of(st):
                used.add(pair_of(st))
            T.name[i], T.stats[i], T.lore[i] = nm, list(st), lore
            T.family_of[i] = key
            return True
        return False

    nots = sorted((i for i in range(len(T.pos)) if T.kind[i] == 'N'), key=lambda i: (ORDER.index(T.area[i]) if T.area[i] in ORDER else 9, i))
    done = set()
    for c in ORDER:
        for side, n in enumerate(T.key[c]['N']):
            give(n, C.OPENERS[c][side], 'opener')
            done.add(n)
    for i in nots:
        role, fam, side = T.role[i]
        if role == 'style':
            give(i, C.STYLE[T.area[i]][side][0], role)
            done.add(i)
        elif role == 'defence':
            give(i, C.DEFENCE[T.area[i]], role)
            done.add(i)
    for (c, side), road in sorted(T.family_roads.items()):
        keys = C.STYLE[c][side][2]
        for i in road:
            if i in done or T.tag[i] == 'capstone':
                continue
            for key in keys:
                if give(i, key, 'family'):
                    break
            done.add(i)
    # the rest of each area, by band
    for area in list(ORDER) + list(BORDERS):
        own = owners_of(area)
        slots = sorted((i for i in nots if i not in done and T.area[i] == area
                        and T.role[i][0] in ('new', 'set', 'socket', 'scenic', 'family')),
                       key=lambda i: (min(D[c].get(i, 999) for c in own), i))
        src = C.CLUSTERS.get(area) or C.BORDER_CLUSTERS.get(area) or []
        used = set(C.OPENERS.get(area, ())) | ({C.DEFENCE[area]} if area in C.DEFENCE else set())
        if area in C.STYLE:
            used |= {k for s_ in C.STYLE[area] for k in s_[2]}
        mids = [f['key'] for f in src if f['key'] not in used and f['band'] in ('inner', 'mid')]
        outs = [f['key'] for f in src if f['key'] not in used and f['band'] == 'outer']
        # what the opener, style and defence families did not place comes first: it is today's content
        rest = [f['key'] for f in src if f['key'] in used]
        queue = [k_ for k_ in rest + mids for _ in left.get(k_, [])] + [k_ for k_ in outs for _ in left.get(k_, [])]
        # interleave so one family's notables spread over the area instead of one corner; a content-pass family
        # with a 'side' only goes to slots on that side
        for i in slots:
            while queue and not left.get(queue[0]):
                queue.pop(0)
            # families tied to this slot's side first, so the side-free ones are what can run out of room
            order = sorted(range(len(queue)), key=lambda qi: 'side' not in fams[queue[qi]][1])
            for qi in order:
                k_ = queue[qi]
                if fams[k_][1].get('side', T.role[i][2]) != T.role[i][2]:
                    continue
                if left.get(k_) and give(i, k_, 'new'):
                    queue.pop(qi)
                    break
            done.add(i)
        # content the sides left over goes to the area's slots still empty, on either side (the class roads take
        # room, so a side can run out of slots while the other has some spare)
        for i in slots:
            if T.name[i]:
                continue
            for k_ in rest + mids + outs:
                if left.get(k_) and give(i, k_, 'new'):
                    break
        for k_ in rest + mids + outs:
            T.unused_content += [(area, k_, nm) for nm, _, _ in left.get(k_, [])]
    # placeholders, profane names, capstones, crossovers
    for i in nots:
        role, fam, side = T.role[i]
        if role == 'profane':
            T.name[i] = C.PROFANE[T.area[i]]
            st, lore = C.PROFANE_CONTENT.get(T.area[i], ([], ''))
            T.lore[i] = lore or C.PROFANE_TOOLTIP
            T.stats[i] = list(st)
        elif role == 'capstone' and (T.area[i], side) in C.CAPSTONES:
            T.name[i], st, T.lore[i] = C.CAPSTONES[(T.area[i], side)]
            T.stats[i] = list(st)
        elif role in ('crossover', 'crossover-weapon'):
            border = fam if role == 'crossover' else C.CROSSOVER_WEAPON[T.area[i]][0]
            if (T.area[i], border) in C.CROSSOVERS:
                T.name[i], st, T.lore[i] = C.CROSSOVERS[(T.area[i], border)]
                T.stats[i] = list(st)
        if not T.name[i]:
            fam_word = fam if isinstance(fam, str) and fam else T.area[i]
            if role in ('new', 'set', 'socket', 'scenic', 'family'):
                fam_word = T.area[i] if role != 'family' else fam
            T.name[i] = placeholder(role, fam_word, side)
    # shrines
    for area in list(ORDER) + list(BORDERS):
        ks = [T.key[area]['K1'], T.key[area]['K2']] if area in CLASSES else [T.key[area]['K']]
        sh = [x for x in C.SHRINES if x['owner'] == area]
        for i, x in zip(ks, sh):
            T.name[i] = x['name']
            T.stats[i] = list(x['stats'])
            T.lore[i] = '"%s" %s' % (x['message'], x['rules'])
    # smalls
    fam_of_cluster = {}
    for i, key in T.family_of.items():
        if T.cl_of[i] >= 0:
            fam_of_cluster.setdefault(T.cl_of[i], key)
    for i in range(len(T.pos)):
        k = T.kind[i]
        if k == 'A':
            T.stats[i] = list(C.ANY_ATTRIBUTE)
        elif k == 't':
            T.stats[i] = list(C.TRAVEL_STATS[T.stat[i] or 'Life'])
        elif k == 's':
            T.stats[i] = small_stats(T, i, fam_of_cluster, fams)
    scale_stats(T)
    # the class roads' content is final (C.ROADS), after the scales
    for i, (key, pos) in getattr(T, 'road', {}).items():
        node = C.ROADS[key]['nodes'][pos - 1]
        if T.kind[i] == 'N':
            T.name[i], st, T.lore[i] = node
        else:
            T.name[i], st, T.lore[i] = '', node, ''
        T.stats[i] = list(st)


def stat_pair(st):
    """Check 27's key: a two-line notable's two stats; with a third line, all of them (the third makes it distinct)."""
    k = tuple(sorted(x for x, _ in st))
    return (k if len(k) > 2 else k[:2]) if len(k) >= 2 else None


def scale_stats(T):
    """C.STAT_SCALE on notables and cluster smalls, C.NOTABLE_SCALE on notables; positive values, rounded half up."""
    def sc(st, table):
        return [(s, int(v * table[s] + 0.5) if s in table and v > 0 else v) for s, v in st]
    for i in range(len(T.pos)):
        if T.kind[i] == 'N':
            T.stats[i] = sc(sc(T.stats[i], getattr(C, 'STAT_SCALE', {})), getattr(C, 'NOTABLE_SCALE', {}))
        elif T.kind[i] == 's' and T.tag[i] != 'exit' and T.cl_of[i] >= 0 and T.clusters[T.cl_of[i]]['tag'] != 'profane':
            T.stats[i] = sc(T.stats[i], getattr(C, 'STAT_SCALE', {}))


def small_stats(T, i, fam_of_cluster, fams):
    area = T.area[i]
    tag = T.tag[i]
    if tag == 'exit':
        c = T.label[i] if T.label[i] in CLASSES else area
        k = T.key[c]
        return list(C.START_EXITS[c][k['E'].index(i)]) if i in k['E'] else list(C.TRAVEL_STATS['Life'])
    key = None
    if T.cl_of[i] >= 0:
        cl = T.clusters[T.cl_of[i]]
        key = fam_of_cluster.get(T.cl_of[i])
        if cl['tag'] == 'profane':
            return list(C.TRAVEL_STATS[C.TRAVEL_FLAVOUR[cl['area']]])
        if key is None:
            # a cluster whose notables are all placeholders: the nearest dealt family of the same area
            best = None
            for ci, k_ in fam_of_cluster.items():
                if T.clusters[ci]['area'] == cl['area']:
                    d = math.dist(T.clusters[ci]['centre'], cl['centre'])
                    if best is None or d < best[0]:
                        best = (d, k_)
            key = best[1] if best else None
    elif tag == 'open':
        c = area if area in CLASSES else None
        if c:
            k = T.key[c]
            side = node_side(T, c, i)
            key = C.OPENERS[c][side]
    elif tag == 'capstone' and area in CLASSES:
        key = C.STYLE[area][node_side(T, area, i)][0]
    if key is None or key not in fams:
        return list(C.TRAVEL_STATS[C.TRAVEL_FLAVOUR.get(area, 'Life')])
    f = fams[key][1]
    if f.get('small_alt') and (i % 2):
        return list(f['small_alt'])
    return list(f['small'])


# ---------------------------------------------------------------- ascendancy islands (design-spec §3.1)
# Each ascendancy is one island in the sanctum, drawn alone once chosen: the root (first point) and its six
# notables, in tree units with the root toward the class start before the turn. Links (i, j, bend): 0 straight,
# a number bends (sag = bend / 2 of the chord, + to the left), 'a' follows the circle round the island's centre.
# Lore must-fixes 3 and 4: the sword is the Knight of Khanduras's; Exile of the Bough takes the wreath and the
# Ivgorod Patriarch the tower.
ISLANDS = {
    'Knight of Khanduras': ('Warrior', 'sword: pommel, grip, crossguard, blade, point',
                            [(0, -140), (0, -96), (-58, -62), (58, -62), (0, -36), (0, 34), (0, 108)],
                            [(0, 1, 0), (1, 2, 0.1), (1, 3, -0.1), (2, 4, 0.1), (3, 4, -0.1), (4, 5, 0), (5, 6, 0)], -16),
    'Blood Warlord': ('Warrior', 'axe: a long haft, a crescent head that closes into a loop',
                      [(0, -140), (14, -84), (30, -28), (44, 28), (-22, 44), (-6, 108), (74, 96)],
                      [(0, 1, 0), (1, 2, 0), (2, 3, 0), (3, 4, 0.15), (4, 5, -0.25), (5, 6, -0.25), (6, 3, -0.2)], 17),
    'Taan Elementalist': ('Sorcerer', 'three-pointed star: one tip per element, the tips joined by inward arcs',
                          [(0, -140), (0, -86), (0, -24), (-82, -66), (82, -66), (0, 70), (0, 126)],
                          [(0, 1, 0), (1, 2, 0), (2, 3, 0), (2, 4, 0), (2, 5, 0), (5, 6, 0), (3, 5, 0.32), (5, 4, 0.32)], -16),
    'Vizjerei Summoner': ('Sorcerer', 'binding circle: a ring of five wards round the bound demon',
                          [(0, -140)] + [pol(78, a) for a in (270, 342, 54, 126, 198)] + [(0, 0)],
                          [(0, 1, 0), (1, 2, 'a'), (2, 3, 'a'), (3, 4, 'a'), (4, 5, 'a'), (5, 1, 'a'), (3, 6, 0), (4, 6, 0)], 17),
    'Sister of the Sightless Eye': ('Rogue', 'eye: two lids meeting at the corners, the pupil on its own stalk',
                                    [(0, -140), (0, -64), (-104, -6), (104, -6), (-40, 54), (40, 54), (0, 0)],
                                    [(0, 1, 0), (1, 2, -0.2), (1, 3, 0.2), (2, 4, -0.15), (4, 5, -0.1), (5, 3, -0.15), (1, 6, 0)], -16),
    "Viz-Jaq'taar": ('Rogue', 'crescent dagger: a kris blade curving up from the hilt, its inner edge a second path',
                     [(0, -140), (6, -92), (46, -40), (52, 26), (22, 86), (-34, 126), (-24, -20)],
                     [(0, 1, 0), (1, 2, -0.1), (2, 3, -0.1), (3, 4, -0.1), (4, 5, -0.1), (1, 6, 0.15), (6, 4, 0.25)], 17),
    'Exile of the Bough': ('Monk', 'wreath: an open ring of six round the empty centre, open at the top',
                           [(0, -122)] + [(102 * math.cos(math.radians(a)), -20 + 102 * math.sin(math.radians(a)))
                                          for a in (212, 160, 110, 328, 20, 70)],
                           [(0, 1, 'a'), (1, 2, 'a'), (2, 3, 'a'), (0, 4, 'a'), (4, 5, 'a'), (5, 6, 'a')], 17),
    'Ivgorod Patriarch': ('Monk', 'tower: two storeys of paired nodes, a roof notable and the spire',
                          [(0, -140), (-42, -84), (42, -84), (-42, -14), (42, -14), (0, 50), (0, 112)],
                          [(0, 1, 0), (0, 2, 0), (1, 3, 0), (2, 4, 0), (3, 5, 0.1), (4, 5, -0.1), (5, 6, 0)], -16),
}
ISLAND_CENTRE = {'Exile of the Bough': (0, -20)}


def island_geometry(name):
    """Island points, links (i, j, sag_rel) and centre in tree coordinates, turned so the root faces its class."""
    cls, _, pts, links, tilt = ISLANDS[name]
    rot = CLASSES[cls] + 90 + tilt
    P_ = [mtransform(p, (0, 0), rot, False) for p in pts]
    cen = mtransform(ISLAND_CENTRE.get(name, (0, 0)), (0, 0), rot, False)
    L = []
    for i, j, b in links:
        if b == 'a':
            sr = arc_sag_round(P_[i], P_[j], cen) / math.dist(P_[i], P_[j])
        else:
            sr = 0.5 * b
        L.append((i, j, sr))
    return P_, L, cen


# ---------------------------------------------------------------- exact Steiner costs (route-checker must-fix)
def reduced_graph(T, s, terms, block=()):
    """The graph a Steiner tree from start s to `terms` can use: no other start, nothing in `block`, and no dead-end
    piece without a terminal (leaves, and clusters hanging from one node, are pruned)."""
    keep = set(range(len(T.pos))) - (T.starts - {s}) - set(block)
    need = set(terms) | {s}
    adj = {i: {j for j in T.adj[i] if j in keep} for i in keep}
    changed = True
    while changed:
        changed = False
        leaves = [i for i in adj if len(adj[i]) <= 1 and i not in need]
        for i in leaves:
            for j in adj[i]:
                adj[j].discard(i)
            del adj[i]
            changed = True
        by_cl = {}
        for i in adj:
            if T.cl_of[i] >= 0:
                by_cl.setdefault(T.cl_of[i], []).append(i)
        for ci, ids in sorted(by_cl.items()):
            if need & set(ids):
                continue
            outside = {j for i in ids for j in adj[i] if j not in ids}
            if len(outside) <= 1:
                for i in ids:
                    for j in adj[i]:
                        if j in adj:
                            adj[j].discard(i)
                for i in ids:
                    del adj[i]
                changed = True
    return adj


def steiner(T, s, targets, block=None, want_tree=False):
    """Exact Steiner tree cost (points = nodes taken, the start excluded) from start s to every target: the
    Dreyfus-Wagner subset recursion on the reduced graph, vectorised over nodes. Profane notables are left out by
    default (the route marker never walks an untaken one). Returns cost, or (cost, node set)."""
    import numpy as np
    block = prof_nodes(T) if block is None else block
    terms = sorted(set(targets) - {s})
    adj = reduced_graph(T, s, terms, block)
    nodes = sorted(adj)
    idx = {v: k for k, v in enumerate(nodes)}
    n = len(nodes)
    nb = [[idx[j] for j in sorted(adj[v])] for v in nodes]
    K = len(terms)
    INF = 10 ** 6
    dp = {}
    pred = {}
    split = {}

    def relax(lab, prd):
        order = sorted(range(n), key=lambda v: lab[v])
        maxl = int(min(lab.max(), INF - 1))
        buckets = [[] for _ in range(maxl + n + 2)]
        for v in order:
            if lab[v] < INF:
                buckets[int(lab[v])].append(v)
        for d in range(len(buckets)):
            for v in buckets[d]:
                if lab[v] != d:
                    continue
                for u in nb[v]:
                    if d + 1 < lab[u]:
                        lab[u] = d + 1
                        prd[u] = v
                        if d + 1 < len(buckets):
                            buckets[d + 1].append(u)
        return lab, prd

    for k, t in enumerate(terms):
        lab = np.full(n, INF, dtype=np.int64)
        lab[idx[t]] = 0
        prd = np.full(n, -1, dtype=np.int64)
        dp[1 << k], pred[1 << k] = relax(lab, prd)
    full = (1 << K) - 1
    for mask in range(1, full + 1):
        if mask & (mask - 1) == 0:
            continue
        low = mask & -mask
        best = np.full(n, INF, dtype=np.int64)
        bsub = np.full(n, -1, dtype=np.int64)
        sub = (mask - 1) & mask
        while sub:
            if sub & low:
                val = dp[sub] + dp[mask ^ sub]
                better = val < best
                best = np.where(better, val, best)
                bsub = np.where(better, sub, bsub)
            sub = (sub - 1) & mask
        prd = np.full(n, -1, dtype=np.int64)
        dp[mask], pred[mask] = relax(best, prd)
        split[mask] = bsub
    cost = int(dp[full][idx[s]]) if K else 0
    if not want_tree:
        return cost
    out = set()

    def walk(mask, v):
        while pred[mask][v] != -1:
            out.add(nodes[v])
            v = int(pred[mask][v])
        out.add(nodes[v])
        if mask & (mask - 1) == 0:
            return
        sub = int(split[mask][v])
        walk(sub, v)
        walk(mask ^ sub, v)
    if K:
        walk(full, idx[s])
    out.add(s)
    return cost, out


# ---------------------------------------------------------------- output (design-spec §11 TSV)
KIND_NAME = {'S': 'Start', 's': 'Small', 't': 'Small', 'A': 'Attribute', 'N': 'Notable', 'J': 'JewelSocket',
             'K': 'Shrine'}
START_ORDER = ['Warrior', 'Monk', 'Rogue', 'Sorcerer']      # today's start ids 0-3
NON_LATTICE_TAGS = ('open', 'spur', 'capstone')              # lattice-built paths drawn as cluster links
GROUP_OF_TAG = {'open': 'opening', 'spur': 'shrine-spur', 'profane': 'profane'}


def is_lattice(T, i):
    """Orbit -1 in the TSV: the attribute web. A link is a lattice link when both ends are."""
    return i < T.n_lat and T.tag[i] not in NON_LATTICE_TAGS


def tsv_order(T):
    """Old index -> new id. Starts 0-3 (today's order), the 12 sockets in JEWEL_SOCKETS order (save data), the start
    exits, then the rest of the lattice, the clusters and the Profane roads, all by area and angle."""
    first = [T.key[c]['S'] for c in START_ORDER]
    for name in C.JEWEL_SOCKETS:
        first.append(T.key[name[:-5]]['D'] if name.endswith(' deep') else
                     (T.key[name]['J'] if name in CLASSES else T.key[name]['B']))
    for c in START_ORDER:
        first += list(T.key[c]['E'])
    seen = set(first)
    areas = list(ORDER) + list(BORDERS)

    def key(i):
        a = T.area[i] if T.area[i] in areas else 'Warrior'
        return (areas.index(a), round(ang_of(T.pos[i]), 3), round(rad(T.pos[i]), 3), i)
    lat = sorted((i for i in range(len(T.pos)) if i not in seen and is_lattice(T, i)), key=key)
    rest = []
    by_cl = {}
    for i in range(len(T.pos)):
        if i in seen or is_lattice(T, i):
            continue
        by_cl.setdefault(T.cl_of[i], []).append(i)
    singles = sorted(by_cl.pop(-1, []), key=key)
    cls_order = sorted(by_cl, key=lambda ci: (T.clusters[ci]['tag'] == 'profane', key(min(by_cl[ci], key=key))))
    for ci in cls_order:
        rest += sorted(by_cl[ci], key=key)
    order = first + lat + singles + rest
    assert len(order) == len(set(order)) == len(T.pos)
    return {old: new for new, old in enumerate(order)}


def arc_token(T, a, b, sag_rel, ids, gxy, orbit_of):
    """Token for the link a-b (internal ids), owned by the lower TSV id. Straight '-'; 'o' along a shared exact
    orbit; else +R / -R in screen terms (+ bends clockwise seen from the lower id toward the higher)."""
    if ids[b] < ids[a]:
        a, b = b, a
        sag_rel = -sag_rel
    if is_lattice(T, a) and is_lattice(T, b):
        return '-'
    if abs(sag_rel) < 1e-3:
        return '-'
    pa = (round(T.pos[a][0]), round(T.pos[a][1]))
    pb = (round(T.pos[b][0]), round(T.pos[b][1]))
    ca, cb = T.cl_of[a], T.cl_of[b]
    if ca == cb and ca >= 0 and T.clusters[ca]['mode'] == 'orbit' and orbit_of[a] == orbit_of[b] >= 0:
        c = gxy[a]
        ra, rb = math.dist(pa, c), math.dist(pb, c)
        ang = abs(adiff(ang_of((pa[0] - c[0], pa[1] - c[1])), ang_of((pb[0] - c[0], pb[1] - c[1]))))
        if abs(ra - rb) < 1.5 and ang < 175 and abs(ra - ORB[orbit_of[a]]) < 2.5:
            return 'o'
    L = math.dist(pa, pb)
    s = sag_rel * L
    Rr = (L * L / 4 + s * s) / (2 * abs(s))
    return ('+' if s > 0 else '-') + str(max(int(math.ceil(Rr)), int(math.ceil(L / 2))))


def tsv_rows(T):
    """Every row of the TSV as a dict, plus the island rows. Coordinates flip to screen (y down)."""
    ids = tsv_order(T)
    n = len(T.pos)
    # cluster ids, centres and orbits
    cid, gxy, orbit_of, motif, group = {}, {}, {}, {}, {}
    for i in range(n):
        ci = T.cl_of[i]
        if is_lattice(T, i):
            cid[i], gxy[i], orbit_of[i], motif[i] = ids[i], T.pos[i], -1, ''
            group[i] = 'lattice'
            continue
        if ci < 0:
            cid[i], gxy[i], orbit_of[i], motif[i] = ids[i], T.pos[i], 0, ''
            group[i] = 'capstone' if T.kind[i] == 'N' and T.tag[i] == 'capstone' else GROUP_OF_TAG.get(T.tag[i], 'path')
            if T.tag[i] == 'capstone' and T.kind[i] != 'N':
                group[i] = 'capstone-path'
            continue
        cl = T.clusters[ci]
        cid[i] = min(ids[j] for j in cl['ids'])
        cen = cl['centre']
        gxy[i] = cen
        if cl['mode'] == 'orbit' and cl.get('orbit') is not None:
            orbit_of[i] = cl['orbit']
        else:
            d = math.dist(T.pos[i], cen)
            orbit_of[i] = min(range(len(ORB)), key=lambda o: abs(ORB[o] - d))
        motif[i] = cl['name'] if cl['tag'] != 'profane' else ''
        group[i] = 'profane' if cl['tag'] == 'profane' else cl['tag']
    # screen coordinates
    scr = lambda p: (round(p[0]), round(-p[1]))
    links = {i: [] for i in range(n)}
    tokens = {}
    for a, b, sr, st in T.edges:
        links[a].append(b)
        links[b].append(a)
        # flip y: a maths-left bulge is a screen-left bulge seen the same way, so the sign stays (see arc_token)
        tokens[(min(ids[a], ids[b]), max(ids[a], ids[b]))] = (a, b, sr)
    # arc_token works in maths coords; positions flipped to screen keep the visual bend, so the clockwise sign of a
    # maths-left bulge holds on screen
    gxr = {i: (round(gxy[i][0]), round(gxy[i][1])) for i in range(n)}
    rows = []
    for i in sorted(range(n), key=lambda i: ids[i]):
        my = ids[i]
        higher = sorted(ids[j] for j in links[i] if ids[j] > my)
        toks = []
        for h in higher:
            a, b, sr = tokens[(my, h)]
            toks.append(arc_token(T, a, b, sr, ids, gxr, orbit_of))
        arcs = '' if all(t == '-' for t in toks) else ','.join(toks)
        x, y = scr(T.pos[i])
        gx, gy = scr(gxy[i])
        kind = KIND_NAME[T.kind[i]]
        rows.append(dict(id=my, name=T.name[i] if T.kind[i] in 'NKS' else '', kind=kind,
                         cls=T.label[i] if T.kind[i] == 'S' else '', x=x, y=y,
                         stats=','.join('%s=%s' % sv for sv in T.stats[i]),
                         links=','.join(str(j) for j in sorted(ids[j] for j in links[i])),
                         region=T.area[i] or '', lore=T.lore[i], group=group[i], cluster_id=cid[i], gx=gx, gy=gy,
                         orbit=orbit_of[i], arcs=arcs, motif=motif[i],
                         road='%s:%d' % T.road[i] if i in getattr(T, 'road', {}) else ''))
    for row in rows:
        if row['kind'] == 'Start':
            row['name'] = row['cls']
    # ascendancy islands
    nid = n
    for c in ORDER:
        for (asc, root_stats, notables) in C.ASCENDANCIES[c]:
            pts, lks, cen = island_geometry(asc)
            base = nid
            gx, gy = scr(cen)
            adjl = {k: [] for k in range(len(pts))}
            for a_, b_, _ in lks:
                adjl[a_].append(b_)
                adjl[b_].append(a_)
            sag = {}
            for a_, b_, sr in lks:
                sag[(a_, b_)] = sr
                sag[(b_, a_)] = -sr
            for k_, p in enumerate(pts):
                x, y = scr(p)
                nm, st = (asc, root_stats) if k_ == 0 else notables[k_ - 1]
                toks = []
                for h in sorted(x_ for x_ in adjl[k_] if x_ > k_):
                    sr = sag[(k_, h)]
                    if abs(sr) < 1e-3:
                        toks.append('-')
                    else:
                        L = math.dist(scr(pts[k_]), scr(pts[h]))
                        s_ = sr * L
                        Rr = (L * L / 4 + s_ * s_) / (2 * abs(s_))
                        toks.append(('+' if s_ > 0 else '-') + str(max(int(math.ceil(Rr)), int(math.ceil(L / 2)))))
                rows.append(dict(id=base + k_, name=nm, kind='Ascendancy' if k_ == 0 else 'AscendancyNotable', cls=c,
                                 x=x, y=y, stats=','.join('%s=%s' % sv for sv in st),
                                 links=','.join(str(base + x_) for x_ in sorted(adjl[k_])), region='Tristram', lore='',
                                 group='ascendancy', cluster_id=base, gx=gx, gy=gy, orbit=0,
                                 arcs='' if all(t == '-' for t in toks) else ','.join(toks), motif='', road=''))
            nid += len(pts)
    return rows, ids


TSV_COLUMNS = ['id', 'name', 'kind', 'class', 'x', 'y', 'stats', 'links', 'region', 'lore', 'group', 'cluster_id',
               'gx', 'gy', 'orbit', 'arcs', 'motif', 'road']


def write_tsv(rows, path):
    with open(path, 'w', newline='') as out:
        out.write('\t'.join(TSV_COLUMNS) + '\r\n')
        for r in rows:
            vals = [r['id'], r['name'], r['kind'], r['cls'], r['x'], r['y'], r['stats'], r['links'], r['region'],
                    r['lore'], r['group'], r['cluster_id'], r['gx'], r['gy'], r['orbit'], r['arcs'], r['motif'], r['road']]
            out.write('\t'.join(str(v).replace('\t', ' ') for v in vals) + '\r\n')


def write_roles(T, ids, path):
    """notable_roles.tsv: every notable's role tag, family, side, area, costs from the four starts, and whether its
    content is still a placeholder (the content pass's work list)."""
    with open(path, 'w', newline='') as out:
        out.write('id\tname\trole\tfamily\tside\tarea\t' + '\t'.join('cost_' + c for c in ORDER) + '\tplaceholder\r\n')
        for i in sorted((i for i in range(len(T.pos)) if T.kind[i] == 'N'), key=lambda i: ids[i]):
            role, fam, side = T.role[i]
            out.write('\t'.join(str(v) for v in [ids[i], T.name[i], role, fam, SIDE_WORD[side], T.area[i]] +
                                [T.Dp[c].get(i, '') for c in ORDER] + ['yes' if T.name[i].startswith('<') or
                                                                        (role == 'profane' and not T.stats[i]) else 'no']) + '\r\n')


def main(argv):
    import argparse
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('outdir')
    ap.add_argument('--poe2-names', help='POE2 node names, one per line (check 26)')
    ap.add_argument('--no-determinism', action='store_true', help='skip the two-run determinism check (check 25)')
    ap.add_argument('--quick', action='store_true', help='lattice and fixed passes only, no fill (for tests)')
    ap.add_argument('--no-pictures', action='store_true')
    ap.add_argument('--fill-seeds', type=int, default=FILL_SEEDS, help='fill seeds to try (default %d)' % FILL_SEEDS)
    ap.add_argument('--layout-cache', help='keep the placed layout in this file and reuse it (content-only changes)')
    args = ap.parse_args(argv)
    sys.setrecursionlimit(100000)
    os.makedirs(args.outdir, exist_ok=True)
    T = build(quick=args.quick, seeds=args.fill_seeds, layout_cache=args.layout_cache)
    rows, ids = tsv_rows(T)
    tsv = os.path.join(args.outdir, 'passive_tree.tsv')
    write_tsv(rows, tsv)
    write_roles(T, ids, os.path.join(args.outdir, 'notable_roles.tsv'))
    import poe2_tree_checks as K
    report = K.check(T, rows, ids, poe2_names=args.poe2_names, tsv_path=tsv,
                     determinism=not args.no_determinism, quick=args.quick)
    with open(os.path.join(args.outdir, 'check-report.txt'), 'w') as f:
        f.write(report)
    with open(os.path.join(args.outdir, 'check-table.md'), 'w') as f:
        f.write(T.check_table.markdown() + '\n')
    if not args.no_pictures:
        import poe2_tree_draw as W
        W.draw_all(T, rows, ids, args.outdir)
    print(report.split('\n\n')[0])


if __name__ == '__main__':
    main(sys.argv[1:])
