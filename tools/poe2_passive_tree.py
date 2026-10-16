#!/usr/bin/env python3
"""Generates the POE2 Mashup passive tree: mods/poe2/txtdata/poe2/passive_tree.tsv (stage 1 of the tree plan).

What the tree holds (regions, clusters, notables, shrines, jewel sockets, distance rules) is in
tools/poe2_tree_content.py; this file only lays it out, links it and checks the result.

The layout, around Tristram in the middle (the sanctum, which holds the ascendancies and no path crosses):
  - Four class starts on a compass, Sorcerer top, Warrior right, Rogue bottom, Monk left. Each start has exactly
    two exits, joined to each other like a gate.
  - From each exit a spoke runs outwards through two ring roads of "+5 to any Attribute" nodes.
  - Between neighbours, a road from each start's facing exit meets at the border zone's hub (the roads together are
    the cloister wall round Tristram); the border zone's spine runs from the hub out to both rings, with its jewel
    socket on the outer ring. Border clusters hang only off the hub and the spine, so every border notable, shrine
    and socket is as many steps from one neighbouring start as from the other.
  - Shrines go in before the other clusters: a class's two hang off its spokes between the rings, 12-13 steps
    out; a border's hangs off its spine one node beyond ring 1, 12 steps from both neighbours.
  - Border clusters are split into two mirrored halves, one on each side of the same spine node, like the arms of
    a transept (a border's wedge is too narrow for a whole cluster on one side).
  - One mid cluster per class hangs off the ring-1 node 9 steps out, so the middle of the climb has notables
    10-11 steps out. The rest go nearest-first, the biggest of each band first, off the spokes, rings and the
    class jewel: each tries the usual fan (a small, then up to three branches of small and notable), then a
    compact shape (one small with the notables round it), turning a little either way, until it neither crosses
    a link nor crowds a node. A cluster with no room anywhere goes in two pieces. "Halls of the Blind" goes on the
    Rogue's Sightless Eye side (LORE_FLANKS).
  - Spacing depends on kind: two notables at least 38 apart, a notable or shrine at least 30 from anything, the
    rest 23; no link passes within 16 of a node it doesn't join; no node has more than 5 links; no node lies beyond
    radius 625.
  - Connectors (short paths of one to three small nodes) then close loops, the ones that save most steps first,
    so many notables have two ways in; one that would break a distance rule is taken back. If a class's middle
    notable is still too far out, a few more connectors bring the nearest far notables in.

The TSV's last column, group, says how each node was placed, i.e. the group of the link to the node it grew from:
  gate       a start and its two exits (the links start-exit and the exit-exit gate)
  wall       the roads from the exits to the border hubs, and the hubs: the cloister wall round Tristram
  spoke      a class spoke, from an exit out through both rings
  spine      a border spine, from the hub out to the border's jewel socket on ring 2
  ring1      ring 1 (r360) and the class jewel sockets on it
  ring2      ring 2 (r520)
  connector  a short path added afterwards to close a loop
  cluster    everything else: cluster smalls and notables, shrines and their spurs, and the ascendancy nodes
A link takes the group of its newer end (the higher id), so the panel can draw the roads (gate, wall, ring1,
ring2) as roads. Columns are read by position, so new ones only ever go at the end.

Usage: tools/poe2_passive_tree.py [output.tsv] [--png preview.png] [--report report.txt]
The checker runs every time; it fails the run when a hard rule breaks and prints how every distance rule went.
"""

import collections
import heapq
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import poe2_tree_content as C  # noqa: E402

# Geometry, in tree units. The panel shows a circle of radius 720, with its seal ring at 672 and area names at 692.
SANCTUM_RADIUS = 110
WALL_RADIUS = 128  # no link may come closer to the middle than this
R_START = 150
R_EXIT = 190
EXIT_DEG = 9
R_HUB = 258
R1 = 360
R2 = 520
# Steps along a spoke from the exit to ring 1 and on to ring 2, and along a spine from the hub.
SPOKE_TO_R1 = 7
SPOKE_TO_R2 = 5
HUB_TO_R1 = 4
SPINE_TO_R2 = 5
# Links from a start's exit to a border hub.
ROAD_STEPS = 5
# Small nodes the connectors may add, and how many of them are '+5 to any Attribute' (one in this many).
CONNECTOR_NODES = 110
CONNECTOR_ATTRIBUTE_EVERY = 7
# Connectors go in by how many steps they save, at least this many: the big savers first, then smaller ones.
CONNECTOR_GAINS = (4, 3, 2)
SP_R1 = SPOKE_TO_R1
SP_R2 = SPOKE_TO_R1 + SPOKE_TO_R2
SPINE_R1 = HUB_TO_R1
SPINE_R2 = HUB_TO_R1 + SPINE_TO_R2
SPOKE_DEG_R1 = 21
SPOKE_DEG_R2 = 24
MAX_RADIUS = 625     # no node beyond this: the panel's seal ring is at 672 and the area names at 692
LINK = 28
RING1_LINK = 46
RING2_LINK = 50
# How close two nodes may be, centre to centre, by kind: two notables, a notable or shrine and anything, the rest.
NOTABLE_SPACING = 38
BIG_SPACING = 30
MIN_SPACING = 23
BIG_KINDS = ('Notable', 'Shrine')
SPACING_MARGIN = 0.75  # placement keeps this much more room, so rounding to whole units in the TSV breaks nothing
NOTABLE_LINK = BIG_SPACING + 2  # a notable sits at least this far from the node it hangs off
LINK_CLEARANCE = 16  # how close a link may pass to a node that isn't one of its ends
MAX_LINKS = 5        # no node gets more links than this
CLASS_WINDOW = 31    # class nodes stay within this many degrees of their axis
BORDER_WINDOW = 18   # border nodes stay within this many degrees of the border's axis (beyond the hub)

CLASS_ORDER = ['Warrior', 'Monk', 'Rogue', 'Sorcerer']
# Notables whose lore names a neighbour: their cluster goes on the side of the class facing that border if it fits.
LORE_FLANKS = {'Halls of the Blind': 'Sightless Eye'}
RING_SMALLS = [[('LifePercent', 3)], [('AllResist', 3)], [('ManaPercent', 3)]]

# Two ascendancies per class, carried over from the old tree unchanged (stage 2 reworks them): the root's
# stats, then six notables. Speed is now in percent (10% skips one frame), so the old "1" became 10.
ASCENDANCIES = {
    'Warrior': [
        ('Knight of Westmarch', [('Strength', 10)], [
            ('Shield Bearer', [('ArmorPercent', 30)]),
            ('Westmarch Steel', [('LifePercent', 10), ('Strength', 10)]),
            ("Crusader's Blow", [('Damage', 25)]),
            ('Holy Vigil', [('Damage', 20), ('ToHit', 10)]),
            ('Bulwark of Light', [('LifePercent', 15), ('ArmorPercent', 20)]),
            ('Unyielding', [('Vitality', 20), ('LifeRegen', 5)]),
        ]),
        ('Blood Warlord', [('Vitality', 10)], [
            ('War Cry', [('Damage', 20)]),
            ('Battle Frenzy', [('AttackSpeed', 10)]),
            ('Scarred Veteran', [('AllResist', 10)]),
            ('Bloodlust', [('LifeRegen', 8)]),
            ('Iron Hide', [('ArmorPercent', 40)]),
            ("Butcher's Cleaver", [('Damage', 30), ('ToHit', 15)]),
        ]),
    ],
    'Rogue': [
        ('Sister of the Sightless Eye', [('Dexterity', 10)], [
            ('Far Sight', [('Damage', 20)]),
            ('Eye of the Sightless', [('ToHit', 20)]),
            ('Rapid Volley', [('AttackSpeed', 10)]),
            ('Piercing Shot', [('Damage', 25)]),
            ('Blessed Aim', [('Dexterity', 15), ('EvasionPercent', 20)]),
            ('Hail of Arrows', [('Damage', 30)]),
        ]),
        ("Viz-Jaq'taar", [('Vitality', 10)], [
            ('Shadow Step', [('EvasionPercent', 30)]),
            ('Venom Lore', [('LifeRegen', 6)]),
            ('Mage Slayer', [('AllResist', 12)]),
            ('Poisoned Blades', [('Damage', 20), ('LightningResist', 10)]),
            ('Death Mark', [('Damage', 30)]),
            ('Cloak of Shadows', [('LifePercent', 12), ('ManaPercent', 12)]),
        ]),
    ],
    'Sorcerer': [
        ('Taan Elementalist', [('Magic', 10)], [
            ('Storm Channel', [('CastSpeed', 10)]),
            ('Chain Lightning Mastery', [('SpellLevels', 1)]),
            ('Tempest Ward', [('LightningResist', 20), ('ManaPercent', 15)]),
            ('Mana Shield', [('EnergyShieldPercent', 30)]),
            ('Inferno', [('Damage', 25)]),
            ('Elemental Dominion', [('SpellLevels', 1), ('ManaRegen', 30)]),
        ]),
        ('Vizjerei Summoner', [('Vitality', 10)], [
            ('Pact of Fire', [('FireResist', 20), ('LifePercent', 10)]),
            ('Binding Rites', [('Spirit', 30)]),
            ('Golem Mastery', [('MinionDamage', 40)]),
            ('Demonic Pact', [('Damage', 20)]),
            ('Blood Ward', [('LifeRegen', 6), ('EnergyShieldPercent', 20)]),
            ('Lord of the Bound', [('MinionDamage', 60), ('Spirit', 20)]),
        ]),
    ],
    'Monk': [
        ('Sky Tower Ascetic', [('Dexterity', 10)], [
            ('Flowing Water', [('EvasionPercent', 25), ('EnergyShieldPercent', 25)]),
            ('Inner Peace', [('AllResist', 10)]),
            ('Thousand Fists', [('Damage', 25)]),
            ('Whirlwind Kata', [('AttackSpeed', 10)]),
            ('Blessed Staff', [('Damage', 20), ('ToHit', 15)]),
            ('Perfect Form', [('Damage', 30)]),
        ]),
        ('Ivgorod Patriarch', [('Magic', 10)], [
            ('Faith Ward', [('MagicResist', 20)]),
            ('Meditation', [('ManaRegen', 40)]),
            ('Smite', [('Damage', 20)]),
            ('Spirit of Ivgorod', [('ManaPercent', 20)]),
            ('Iron Skin', [('EnergyShieldPercent', 30)]),
            ('Divine Wrath', [('Damage', 30), ('CastSpeed', 10)]),
        ]),
    ],
}

# Each ascendancy's nodes, relative to its column in the sanctum: the root at the bottom, then a fork that joins again.
ASCENDANCY_LAYOUT = [(0, 30), (0, 10), (-18, -5), (18, -5), (-18, -28), (18, -28), (0, -45)]
ASCENDANCY_LINKS = [(0, 1), (1, 2), (1, 3), (2, 4), (3, 5), (4, 6), (5, 6)]
ASCENDANCY_COLUMNS = (-38, 38)


def spacing(kind_a, kind_b):
    """How far apart two nodes of these kinds must be."""
    if kind_a == 'Notable' and kind_b == 'Notable':
        return NOTABLE_SPACING
    if kind_a in BIG_KINDS or kind_b in BIG_KINDS:
        return BIG_SPACING
    return MIN_SPACING


def polar(radius, degrees):
    a = math.radians(degrees)
    return (radius * math.cos(a), radius * math.sin(a))


def angle_of(p):
    return math.degrees(math.atan2(p[1], p[0]))


def angle_diff(a, b):
    return ((a - b + 180) % 360) - 180


def seg_cross(p1, p2, p3, p4):
    def orient(a, b, c):
        return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
    d1, d2, d3, d4 = orient(p3, p4, p1), orient(p3, p4, p2), orient(p1, p2, p3), orient(p1, p2, p4)
    # Strict: segments on one line (rounding noise around 0) don't count as crossing.
    return d1 * d2 < -1e-6 and d3 * d4 < -1e-6


def point_seg_distance(p, a, b):
    dx, dy = b[0] - a[0], b[1] - a[1]
    length = dx * dx + dy * dy
    t = 0 if length == 0 else max(0, min(1, ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / length))
    return math.dist(p, (a[0] + t * dx, a[1] + t * dy))


class Tree:
    def __init__(self):
        self.nodes = []
        self.links = set()
        self.adjacency = collections.defaultdict(set)

    def add(self, kind, p, stats=(), name='', region='', lore='', tag='', hero_class='', group='cluster'):
        self.nodes.append({'kind': kind, 'x': p[0], 'y': p[1], 'stats': list(stats), 'name': name, 'region': region,
                           'lore': lore, 'tag': tag, 'class': hero_class, 'group': group})
        return len(self.nodes) - 1

    def pos(self, i):
        return (self.nodes[i]['x'], self.nodes[i]['y'])

    def link(self, a, b):
        assert a != b
        self.links.add((min(a, b), max(a, b)))
        self.adjacency[a].add(b)
        self.adjacency[b].add(a)

    def unlink(self, a, b):
        self.links.discard((min(a, b), max(a, b)))
        self.adjacency[a].discard(b)
        self.adjacency[b].discard(a)

    def degree(self, i):
        return len(self.adjacency[i])

    def snapshot(self):
        return len(self.nodes), set(self.links)

    def restore(self, snap):
        """Takes back every node and link added since snapshot()."""
        count, links = snap
        del self.nodes[count:]
        self.links = set(links)
        self.adjacency = collections.defaultdict(set)
        for a, c in self.links:
            self.adjacency[a].add(c)
            self.adjacency[c].add(a)

    def chain(self, ids):
        for a, b in zip(ids, ids[1:]):
            self.link(a, b)

    def main_nodes(self):
        return [i for i, n in enumerate(self.nodes) if not n['kind'].startswith('Ascendancy')]


class Builder:
    def __init__(self):
        self.tree = Tree()
        self.small_counts = collections.Counter()
        self.placement_tries = 0

    # --- small nodes ---------------------------------------------------------------------------------------------
    def small(self, p, stats_options, region, tag, key, group='cluster'):
        """A small node; stats_options alternate per key (a cluster, spoke or road)."""
        count = self.small_counts[key]
        self.small_counts[key] += 1
        stats = stats_options[count % len(stats_options)]
        return self.tree.add('Small', p, stats, region=region, tag=tag, group=group)

    def snapshot(self):
        return self.tree.snapshot(), collections.Counter(self.small_counts)

    def restore(self, snap):
        self.tree.restore(snap[0])
        self.small_counts = collections.Counter(snap[1])

    # --- placement checks --------------------------------------------------------------------------------------
    @staticmethod
    def room(kind_a, kind_b):
        need = spacing(kind_a, kind_b)
        return need + SPACING_MARGIN if need > MIN_SPACING else need

    def fits(self, points, links, region, window_axis, window, kinds=None):
        """points: new positions (of kinds, small by default); links: pairs of ('n', index into points) or ('e', existing id)."""
        tree = self.tree
        kinds = kinds or ['Small'] * len(points)
        main = [i for i in range(len(tree.nodes)) if not tree.nodes[i]['kind'].startswith('Ascendancy')]
        for k, p in enumerate(points):
            r = math.hypot(*p)
            if r > MAX_RADIUS or r < R_EXIT - 5:
                return False
            if window is not None and abs(angle_diff(angle_of(p), window_axis)) > window:
                return False
            for j, q in enumerate(points[:k]):
                if math.dist(p, q) < self.room(kinds[k], kinds[j]):
                    return False
            for i in main:
                if math.dist(p, tree.pos(i)) < self.room(kinds[k], tree.nodes[i]['kind']):
                    return False
        # No node may end up with more than MAX_LINKS links.
        new_links = collections.Counter(e[1] for a, b in links for e in (a, b) if e[0] == 'e')
        if any(tree.degree(i) + count > MAX_LINKS for i, count in new_links.items()):
            return False

        def at(end):
            return points[end[1]] if end[0] == 'n' else tree.pos(end[1])

        segments = [(at(a), at(b), a, b) for a, b in links]
        for k, (p1, p2, a, b) in enumerate(segments):
            if math.dist(p1, p2) > 60:
                return False
            ends = {e[1] for e in (a, b) if e[0] == 'e'}
            for i in main:
                if i not in ends and point_seg_distance(tree.pos(i), p1, p2) < LINK_CLEARANCE:
                    return False
            for j, q in enumerate(points):
                if ('n', j) not in (a, b) and point_seg_distance(q, p1, p2) < LINK_CLEARANCE:
                    return False
            for c, d in tree.links:
                if c in ends or d in ends:
                    continue
                if seg_cross(p1, p2, tree.pos(c), tree.pos(d)):
                    return False
            for q1, q2, c, d in segments[:k]:
                if len({a, b, c, d}) < 4:
                    continue
                if seg_cross(p1, p2, q1, q2):
                    return False
        # New nodes keep clear of the links already there, as new links keep clear of the nodes.
        for p in points:
            for c, d in tree.links:
                if point_seg_distance(p, tree.pos(c), tree.pos(d)) < LINK_CLEARANCE:
                    return False
        return True

    # --- clusters ------------------------------------------------------------------------------------------------
    @staticmethod
    def cluster_shape(origin, direction, notables, smalls_before, spread, step, lead_small=True, max_branches=3):
        """Positions of a spur: smalls_before small nodes, then up to three branches holding the notables.

        Returns (points, kinds, links, notable_slots) where links use ('a',) for the anchor and indexes otherwise.
        """
        points, kinds, links = [], [], []
        x, y = origin
        d = math.radians(direction)
        prev = 'a'
        for _ in range(smalls_before):
            x += step * math.cos(d)
            y += step * math.sin(d)
            points.append((x, y))
            kinds.append('Small')
            links.append((prev, len(points) - 1))
            prev = len(points) - 1
        hub, hub_pos = prev, (x, y)
        notable_step = max(step * 1.15, NOTABLE_LINK)
        if notables == 1:
            p = (x + max(step * 1.2, NOTABLE_LINK) * math.cos(d), y + max(step * 1.2, NOTABLE_LINK) * math.sin(d))
            points.append(p)
            kinds.append('Notable')
            links.append((hub, len(points) - 1))
            return points, kinds, links
        branches = min(notables, max_branches)
        per_branch = [1] * branches
        extra = notables - branches
        order = [0, branches - 1, 1] if branches == 3 else [0, 1][:branches]
        for k in range(extra):
            per_branch[order[k % len(order)]] += 1
        # Neighbouring branches' first nodes must stay apart: smalls by MIN_SPACING, notables by NOTABLE_SPACING.
        first_gap, first_step = (MIN_SPACING + 1, step) if lead_small else (NOTABLE_SPACING + 1, notable_step)
        spread = max(spread, math.degrees(2 * math.asin(min(1.0, first_gap / (2 * first_step)))))
        for b in range(branches):
            offset = spread * (b - (branches - 1) / 2)
            bd = math.radians(direction + offset)
            bx, by = hub_pos
            last = hub
            for k in range(per_branch[b]):
                if lead_small or k > 0:
                    bx += step * math.cos(bd)
                    by += step * math.sin(bd)
                    points.append((bx, by))
                    kinds.append('Small')
                    links.append((last, len(points) - 1))
                    last = len(points) - 1
                bx += notable_step * math.cos(bd)
                by += notable_step * math.sin(bd)
                points.append((bx, by))
                kinds.append('Notable')
                links.append((last, len(points) - 1))
                last = len(points) - 1
                # Later notables in a branch bend outwards a little, so long branches don't run into each other.
                bd += math.radians(12 if offset >= 0 else -12) if branches > 1 else 0
        return points, kinds, links

    def place_cluster(self, anchor, direction, cluster, region, window_axis, window, smalls_before=1, spread=48, tag=''):
        ids = self.try_cluster(anchor, direction, cluster, region, window_axis, window, smalls_before, spread, tag)
        if ids is None:
            raise RuntimeError(f'no room for cluster {cluster["key"]} ({region}) off node {anchor} heading {direction:.0f}')
        return ids

    def try_cluster(self, anchor, direction, cluster, region, window_axis, window, smalls_before=1, spread=48, tag='', quick=False, lead_small=True, max_branches=3, prefer=None, extra_smalls=(0, 1)):
        """Places a cluster off anchor if it fits somewhere near direction; returns its ids, or None.
        self.last_try is the (smalls, turn, spread, step) it took; prefer is such a tuple to try first, so a
        mirrored partner can take the mirror image of its twin."""
        tree = self.tree
        notables = cluster['notables']
        origin = tree.pos(anchor)
        turns = (0, 15, -15, 30, -30) if quick else (0, 10, -10, 20, -20, 30, -30, 42, -42, 55, -55, 70, -70)
        tries = [prefer] if prefer else []
        for sb in (smalls_before + extra for extra in extra_smalls):
            for turn in turns:
                for spread_scale in ((1.0, 1.3) if quick else (1.0, 0.85, 1.2, 1.4)):
                    for step_scale in ((1.0,) if quick else (1.0, 0.9, 1.12)):
                        tries.append((sb, turn, spread_scale, step_scale))
        for sb, turn, spread_scale, step_scale in tries:
            self.placement_tries += 1
            points, kinds, links = self.cluster_shape(origin, direction + turn, len(notables), sb, spread * spread_scale, LINK * step_scale, lead_small, max_branches)
            mapped = [(('e', anchor) if a == 'a' else ('n', a), ('n', b)) for a, b in links]
            if not self.fits(points, mapped, region, window_axis, window, kinds):
                continue
            self.last_try = (sb, turn, spread_scale, step_scale)
            ids = []
            notable_index = 0
            stats_options = [cluster['small']] + ([cluster['small_alt']] if cluster.get('small_alt') else [])
            for p, kind in zip(points, kinds):
                if kind == 'Notable':
                    name, stats, lore = notables[notable_index]
                    notable_index += 1
                    ids.append(tree.add('Notable', p, stats, name=name, region=region, lore=lore, tag=tag))
                else:
                    ids.append(self.small(p, stats_options, region, tag, cluster['key']))
            for a, b in links:
                tree.link(anchor if a == 'a' else ids[a], ids[b])
            return ids
        return None

    def place_spur(self, anchors, end_kind, end_stats, end_name, end_lore, small_stats, region, window_axis, window, tag):
        """A straight spur ending in one special node (a shrine), off the first (anchor, direction, links) with room;
        links is how many links the spur has, so the end lands that many steps beyond the anchor."""
        for anchor, direction, length in anchors:
            ids = self.try_spur(anchor, direction, length, end_kind, end_stats, end_name, end_lore, small_stats, region, window_axis, window, tag)
            if ids is not None:
                return ids
        raise RuntimeError(f'no room for {end_name}')

    def try_spur(self, anchor, direction, length, end_kind, end_stats, end_name, end_lore, small_stats, region, window_axis, window, tag):
        tree = self.tree
        origin = tree.pos(anchor)
        n = length
        for turn in (0, 10, -10, 20, -20, 32, -32, 45, -45, 60, -60, 80, -80):
            for step_scale in (1.0, 0.9, 1.12):
                d = math.radians(direction + turn)
                step = LINK * step_scale
                points = [(origin[0] + step * k * math.cos(d), origin[1] + step * k * math.sin(d)) for k in range(1, n)]
                points.append((origin[0] + step * (n - 1 + 1.25) * math.cos(d), origin[1] + step * (n - 1 + 1.25) * math.sin(d)))
                links = [(('e', anchor) if k == 0 else ('n', k - 1), ('n', k)) for k in range(n)]
                if not self.fits(points, links, region, window_axis, window, ['Small'] * (n - 1) + [end_kind]):
                    continue
                ids = [self.small(p, [small_stats], region, tag, f'spur:{end_name}') for p in points[:-1]]
                ids.append(tree.add(end_kind, points[-1], end_stats, name=end_name, region=region, lore=end_lore, tag=tag))
                tree.chain([anchor] + ids)
                return ids
        return None


def ring_region(p):
    """Which region a ring node belongs to, by its angle."""
    a = angle_of(p)
    for name, border in C.BORDERS.items():
        if abs(angle_diff(a, border['angle'])) <= 11:
            return name
    return min(C.REGIONS, key=lambda name: abs(angle_diff(a, C.REGIONS[name]['angle'])))


def build():
    b = Builder()
    tree = b.tree
    axes = {name: region['angle'] for name, region in C.REGIONS.items()}

    starts = {name: tree.add('Start', polar(R_START, axes[name]), name=name, region=name, tag='start', hero_class=name, group='gate') for name in CLASS_ORDER}

    # Jewel sockets first, in save order (a socket's index is its place among the sockets).
    sockets = {}
    for owner in C.JEWEL_SOCKETS:
        if owner in C.REGIONS:
            sockets[owner] = tree.add('JewelSocket', polar(R1, axes[owner]), region=owner, tag='socket', group='ring1')
        else:
            sockets[owner] = tree.add('JewelSocket', polar(R2, C.BORDERS[owner]['angle']), region=owner, tag='socket', group='spine')

    # Exits, gates and spokes.
    exits, spokes = {}, {}
    for name in CLASS_ORDER:
        axis = axes[name]
        for index, side in enumerate((-1, 1)):
            exit_stats = C.START_EXITS[name][index]
            key = f'spoke:{name}:{side}'
            b.small_counts[key] += 1
            exit_node = tree.add('Small', polar(R_EXIT, axis + side * EXIT_DEG), exit_stats, region=name, tag='exit', group='gate')
            tree.link(starts[name], exit_node)
            exits[(name, side)] = exit_node
            ids = [exit_node]
            for k in range(1, SPOKE_TO_R1 + 1):
                t = k / SPOKE_TO_R1
                p = polar(R_EXIT + (R1 - R_EXIT) * t, axis + side * (EXIT_DEG + (SPOKE_DEG_R1 - EXIT_DEG) * t))
                ids.append(b.small(p, [exit_stats], name, 'spoke', key, 'spoke'))
            for k in range(1, SPOKE_TO_R2 + 1):
                t = k / SPOKE_TO_R2
                p = polar(R1 + (R2 - R1) * t, axis + side * (SPOKE_DEG_R1 + (SPOKE_DEG_R2 - SPOKE_DEG_R1) * t))
                ids.append(b.small(p, [exit_stats], name, 'spoke', key, 'spoke'))
            tree.chain(ids)
            spokes[(name, side)] = ids
        # The gate: a start's two exits are joined, so travellers can pass in front of it.
        tree.link(exits[(name, -1)], exits[(name, 1)])

    # Border zones: roads from both neighbours' facing exits meet at the hub; the spine runs out to both rings.
    # The roads form the cloister wall round Tristram.
    hubs, spines = {}, {}
    for bname, border in C.BORDERS.items():
        axis = border['angle']
        clusters = C.BORDER_CLUSTERS[bname]
        hub = b.small(polar(R_HUB, axis), [clusters[0]['small']], bname, 'hub', f'road:{bname}', 'wall')
        hubs[bname] = hub
        for cls in border['between']:
            caxis = axes[cls]
            diff = angle_diff(axis, caxis)
            side = 1 if diff > 0 else -1
            start = (R_EXIT, caxis + side * EXIT_DEG)
            end = (R_HUB, caxis + diff)
            road = []
            for k in range(1, ROAD_STEPS):
                t = k / ROAD_STEPS
                # The road turns away from the spoke a little faster than it climbs, so its first node keeps clear of the spoke's.
                p = polar(start[0] + (end[0] - start[0]) * t, start[1] + (end[1] - start[1]) * (1 - (1 - t) ** 1.15))
                road.append(b.small(p, [clusters[0]['small']], cls if t < 0.5 else bname, 'road', f'road:{bname}', 'wall'))
            tree.chain([exits[(cls, side)]] + road + [hub])
        spine = [hub]
        spine_stats = [clusters[1]['small'], clusters[2]['small']]
        for k in range(1, HUB_TO_R1 + 1):
            spine.append(b.small(polar(R_HUB + (R1 - R_HUB) * k / HUB_TO_R1, axis), spine_stats, bname, 'spine', f'spine:{bname}', 'spine'))
        for k in range(1, SPINE_TO_R2):
            spine.append(b.small(polar(R1 + (R2 - R1) * k / SPINE_TO_R2, axis), spine_stats, bname, 'spine', f'spine:{bname}', 'spine'))
        spine.append(sockets[bname])
        tree.chain(spine)
        spines[bname] = spine

    # Ring roads through the anchors on each ring, mostly "+5 to any Attribute".
    def ring(radius, anchors, link_length, tag):
        items = sorted((deg % 360, node) for deg, node in anchors.items())
        for (d1, n1), (d2, n2) in zip(items, items[1:] + [(items[0][0] + 360, items[0][1])]):
            arc = math.radians(d2 - d1) * radius
            count = max(round(arc / link_length) - 1, 0)
            mids = []
            for i in range(1, count + 1):
                p = polar(radius, d1 + (d2 - d1) * i / (count + 1))
                region = ring_region(p)
                if i % 2 == 1:
                    mids.append(tree.add('Attribute', p, C.ANY_ATTRIBUTE, region=region, tag=tag, group=tag))
                else:
                    mids.append(b.small(p, RING_SMALLS, region, tag, tag, tag))
            tree.chain([n1] + mids + [n2])

    r1, r2 = {}, {}
    for (name, side), ids in spokes.items():
        r1[axes[name] + side * SPOKE_DEG_R1] = ids[SP_R1]
        r2[axes[name] + side * SPOKE_DEG_R2] = ids[SP_R2]
    for name in CLASS_ORDER:
        r1[axes[name]] = sockets[name]
    for bname, border in C.BORDERS.items():
        r1[border['angle']] = spines[bname][SPINE_R1]
        r2[border['angle']] = spines[bname][SPINE_R2]
    ring(R1, r1, RING1_LINK, 'ring1')
    ring(R2, r2, RING2_LINK, 'ring2')

    def steps_from(origin):
        dist = {origin: 0}
        queue = collections.deque([origin])
        while queue:
            n = queue.popleft()
            for m in tree.adjacency[n]:
                if m not in dist and tree.nodes[m]['kind'] != 'Start':
                    dist[m] = dist[n] + 1
                    queue.append(m)
        return dist

    # Every other cluster goes to the nearest free spot: candidates are (anchor, direction) pairs off the region's
    # spokes, rings and spine, sorted by how many steps the anchor is from the region's start (or hub), so the tree
    # fills from the inside out. A cluster takes the first candidate where it fits without crossings or crowding.
    # A border's clusters hang only off nodes as many steps from both neighbouring starts (the hub and the spine),
    # so every border notable is as near to one neighbour as to the other.
    axes_all = dict(axes, **{bname: border['angle'] for bname, border in C.BORDERS.items()})
    windows = dict({name: CLASS_WINDOW for name in CLASS_ORDER}, **{bname: BORDER_WINDOW for bname in C.BORDERS})
    from_start = {name: steps_from(starts[name]) for name in CLASS_ORDER}
    candidates = {}
    for region in list(CLASS_ORDER) + list(C.BORDERS):
        axis = axes_all[region]
        dist = steps_from(starts[region] if region in starts else hubs[region])
        found = []
        for i, node in enumerate(tree.nodes):
            if node['tag'] not in ('spoke', 'ring1', 'ring2', 'spine', 'socket', 'hub') or i not in dist:
                continue
            p = tree.pos(i)
            if abs(angle_diff(angle_of(p), axis)) > windows[region] + 2:
                continue
            if region in C.BORDERS:
                one, other = C.BORDERS[region]['between']
                if from_start[one].get(i) != from_start[other].get(i):
                    continue
            a = angle_of(p)
            if node['tag'] in ('spoke', 'spine', 'hub'):
                directions = [a + 70, a - 70, a + 100, a - 100]
            elif node['tag'] == 'ring2' or (node['tag'] == 'socket' and region in C.BORDERS):
                directions = [a, a + 30, a - 30]
            else:
                directions = [a, a + 180, a + 150, a - 150]
            for d in directions:
                found.append((dist[i], i, d))
        found.sort()
        candidates[region] = found

    def flank_of(region, cluster):
        """+1 or -1 when one of the cluster's notables names the neighbour on that side of the region's axis."""
        for name, _, _ in cluster['notables']:
            if name in LORE_FLANKS and region in axes:
                return 1 if angle_diff(C.BORDERS[LORE_FLANKS[name]]['angle'], axes[region]) > 0 else -1
        return None

    def on_flank(region, cluster, ids, flank):
        names = {name for name, _, _ in cluster['notables'] if name in LORE_FLANKS}
        return all(angle_diff(angle_of(tree.pos(i)), axes_all[region]) * flank > 2 for i in ids if tree.nodes[i]['name'] in names)

    def place_near(region, cluster):
        """The cluster takes the nearest candidate where it fits: quick shapes first, then every shape."""
        flank = flank_of(region, cluster)
        for wanted, quick in [(f, q) for f in ([flank, None] if flank else [None]) for q in (True, False)]:
            for step, anchor, direction in candidates[region]:
                if wanted and angle_diff(angle_of(tree.pos(anchor)), axes_all[region]) * wanted < -2:
                    continue
                snap = b.snapshot()
                # A lone notable gets a small node before it, so it never sits right on a road. Where the usual fan
                # doesn't fit, the compact shape may: one small with the notables round it.
                lone = len(cluster['notables']) == 1
                ids = b.try_cluster(anchor, direction, cluster, region, axes_all[region], windows[region], 1 if lone else 0, 48, f'd{step}', quick=quick)
                if ids is None and not lone:
                    ids = b.try_cluster(anchor, direction, cluster, region, axes_all[region], windows[region], 1, 48, f'd{step}', quick=quick, lead_small=False, extra_smalls=(0,))
                if ids is None:
                    continue
                if wanted and not on_flank(region, cluster, ids, wanted):
                    b.restore(snap)
                    continue
                placed_in[cluster['key']] = step
                return
            if wanted and not quick:
                print(f'note: {cluster["key"]} found no room on its lore flank; placed by distance only')
        # No room for the whole cluster: it goes in two pieces, each in the nearest pocket it fits.
        if len(cluster['notables']) > 1:
            half = (len(cluster['notables']) + 1) // 2
            print(f'note: {cluster["key"]} ({region}) split in two to fit')
            place_near(region, dict(cluster, notables=cluster['notables'][:half]))
            place_near(region, dict(cluster, notables=cluster['notables'][half:]))
            return
        raise RuntimeError(f'no room for {cluster["key"]}')

    placed_in = {}
    # Each class's first two notables: one off each spoke, close to the start.
    for name in CLASS_ORDER:
        axis = axes[name]
        inner = [c for c in C.CLUSTERS[name] if c['band'] == 'inner']
        for side, cluster in zip((-1, 1), inner):
            b.place_cluster(spokes[(name, side)][2], axis + side * 70, cluster, name, axis, CLASS_WINDOW, smalls_before=0, tag='inner')
    # Border inner clusters sit at the hub, or next to it on the spine.
    for bname, border in C.BORDERS.items():
        axis = border['angle']
        spine = spines[bname]
        # The notables sit right on the hub, in the gaps between the spine and the two roads (about 60 degrees
        # either side of the spine).
        hub_slots = [(spine[0], axis + turn) for turn in (0, 30, -30, 60, -60)] + [(spine[1], axis + turn) for turn in (90, -90, 60, -60)]
        if not any(b.try_cluster(anchor, direction, C.BORDER_CLUSTERS[bname][0], bname, axis, BORDER_WINDOW + 10, 0, 120, 'binner', lead_small=False, max_branches=2) is not None
                   for anchor, direction in hub_slots):
            raise RuntimeError(f'no room for {C.BORDER_CLUSTERS[bname][0]["key"]}')

    # Shrines, before the other clusters so they get their places. A class's two shrines hang off its spokes
    # 11-12 steps out, between the rings, one on each side, so each lands 12-13 steps from the start. A border's
    # shrine hangs off its spine one node beyond ring 1, 12 steps from both neighbours.
    for shrine in C.SHRINES:
        owner = shrine['owner']
        lore = f'"{shrine["message"]}" {shrine["rules"]}'
        if owner in C.REGIONS:
            axis = axes[owner]
            index = [s['name'] for s in C.SHRINES if s['owner'] == owner].index(shrine['name'])
            side = -1 if index == 0 else 1
            sp = spokes[(owner, side)]
            small_stats = C.START_EXITS[owner][0]
            anchors = [(sp[k], axis + side * turn, 1) for k in (SP_R1 + 3, SP_R1 + 4) for turn in (70, -70)] + [(sp[SP_R1 + 2], axis + side * turn, 2) for turn in (70, -70)]
            b.place_spur(anchors, 'Shrine', shrine['stats'], shrine['name'], lore, small_stats, owner, axis, CLASS_WINDOW, 'shrine')
        else:
            axis = C.BORDERS[owner]['angle']
            spine = spines[owner]
            small_stats = C.BORDER_CLUSTERS[owner][0]['small']
            anchors = [(spine[SPINE_R1 + 1], axis + turn, 1) for turn in (90, -90)] + [(spine[SPINE_R1 + 2], axis + turn, 1) for turn in (90, -90)] + [(spine[SPINE_R1 + 1], axis + turn, 2) for turn in (90, -90)]
            b.place_spur(anchors, 'Shrine', shrine['stats'], shrine['name'], lore, small_stats, owner, axis, BORDER_WINDOW + 10, 'bshrine')

    # A border's wedge is narrow, so each of its clusters is split into two mirrored halves, one on each side of
    # the same spine node, like the arms of a transept: the mid clusters nearest the hub first, then the outer one.
    # All of them hang off the spine, so they are as near to one neighbour as to the other.
    for bname, border in C.BORDERS.items():
        axis = border['angle']
        spine = spines[bname]
        for cluster in C.BORDER_CLUSTERS[bname][1:4]:
            half = (len(cluster['notables']) + 1) // 2
            halves = [dict(cluster, notables=cluster['notables'][:half]), dict(cluster, notables=cluster['notables'][half:])]
            done = False
            for k in range(len(spine)):
                for turn in (90, 75, 105, 60, 120, 135):
                    snap = b.snapshot()
                    if b.try_cluster(spine[k], axis + turn, halves[0], bname, axis, BORDER_WINDOW, 0, 48, f'd{k}', quick=True) is None:
                        continue
                    sb, t, spread_scale, step_scale = b.last_try
                    if halves[1]['notables'] and b.try_cluster(spine[k], axis - turn, halves[1], bname, axis, BORDER_WINDOW, 0, 48, f'd{k}', quick=True,
                                                               prefer=(sb, -t, spread_scale, step_scale)) is None:
                        b.restore(snap)
                        continue
                    placed_in[cluster['key']] = k
                    done = True
                    break
                if done:
                    break
            if not done:
                raise RuntimeError(f'no room for {cluster["key"]}')

    # One mid cluster per class hangs straight off the ring-1 node 9 steps out, between the spoke and the class
    # jewel, compact (one small, then the notables round it), so the middle of the climb has notables 10-11 steps
    # out, not only the ring round the jewel at 12. It turns away from the jewel, leaving the way out past it free.
    for name in CLASS_ORDER:
        axis = axes[name]
        mids = [c for c in C.CLUSTERS[name] if c['band'] == 'mid']
        if not mids:
            continue
        cluster = mids[1] if len(mids) > 1 else mids[0]
        ring_nodes = sorted((i for i, n in enumerate(tree.nodes) if n['tag'] == 'ring1' and n['region'] == name and from_start[name].get(i) == 9),
                            key=lambda i: (abs(angle_diff(angle_of(tree.pos(i)), axis)), -angle_diff(angle_of(tree.pos(i)), axis)))
        for anchor in ring_nodes:
            a = angle_of(tree.pos(anchor))
            away = a + (45 if angle_diff(a, axis) > 0 else -45)
            step = from_start[name][anchor]
            if (b.try_cluster(anchor, away, cluster, name, axis, CLASS_WINDOW, 1, 40, f'd{step}', lead_small=False, extra_smalls=(0,)) is not None
                    or b.try_cluster(anchor, away, cluster, name, axis, CLASS_WINDOW, 0, 40, f'd{step}', extra_smalls=(0,)) is not None):
                placed_in[cluster['key']] = step
                break

    # Within a band the biggest clusters go first, so the smaller ones can fill the pockets left near the start.
    for band in ('mid', 'outer'):
        for name in CLASS_ORDER:
            for cluster in sorted((c for c in C.CLUSTERS[name] if c['band'] == band), key=lambda c: -len(c['notables'])):
                if cluster['key'] not in placed_in:
                    place_near(name, cluster)
    b.slots = placed_in

    # Connectors: short paths of one or two small nodes that close loops between clusters and the roads, so most
    # notables can be reached from two sides, as in POE2. The pairs far apart in steps but close on the map go first.
    add_connectors(b, CONNECTOR_NODES, starts)
    mend_medians(b, starts)

    # The sanctum: each class's two ascendancies, side by side (the panel shows only the player's class).
    for name in CLASS_ORDER:
        for column, (title, root_stats, ascendancy_notables) in zip(ASCENDANCY_COLUMNS, ASCENDANCIES[name]):
            ids = []
            for index, (dx, y) in enumerate(ASCENDANCY_LAYOUT):
                if index == 0:
                    ids.append(tree.add('Ascendancy', (column + dx, y), root_stats, name=title, region='Tristram', hero_class=name))
                else:
                    node_title, node_stats = ascendancy_notables[index - 1]
                    ids.append(tree.add('AscendancyNotable', (column + dx, y), node_stats, name=node_title, region='Tristram', hero_class=name))
            for a, c in ASCENDANCY_LINKS:
                tree.link(ids[a], ids[c])

    return tree, starts, sockets, b


def add_connectors(b, budget, starts):
    """Adds short paths that close loops between nodes close on the map but many steps apart."""
    tree = b.tree
    adjacency = collections.defaultdict(set)
    for a, c in tree.links:
        adjacency[a].add(c)
        adjacency[c].add(a)
    main = [i for i in tree.main_nodes() if tree.nodes[i]['kind'] != 'Start']

    def graph_distance(u, v, limit):
        dist = {u: 0}
        queue = collections.deque([u])
        while queue:
            n = queue.popleft()
            if dist[n] >= limit:
                continue
            for m in adjacency[n]:
                if m not in dist and tree.nodes[m]['kind'] != 'Start':
                    dist[m] = dist[n] + 1
                    if m == v:
                        return dist[m]
                    queue.append(m)
        return limit + 1

    pairs = []
    for k, u in enumerate(main):
        for v in main[k + 1:]:
            if tree.nodes[u]['region'] != tree.nodes[v]['region']:
                continue
            if {'Shrine', 'JewelSocket'} & {tree.nodes[u]['kind'], tree.nodes[v]['kind']}:
                continue
            d = math.dist(tree.pos(u), tree.pos(v))
            if 52 <= d <= 118:
                gaps = connector_gaps(tree.nodes[u]['kind'], tree.nodes[v]['kind'], d)
                if gaps:
                    pairs.append((d, u, v, gaps))
    added = 0
    made = 0
    misses = distance_misses(tree, starts)
    # The pair with the biggest gain goes first (the first in pairs' order on a tie). Gains only shrink as links are
    # added, so a heap of stale gains is enough: re-measure the top, and take it if it's still at least the next.
    def gain_of(index):
        d, u, v, gaps = pairs[index]
        return graph_distance(u, v, 12) - len(gaps)

    tried = set()
    for min_gain in CONNECTOR_GAINS:
        heap = [(-gain_of(k), k) for k in range(len(pairs)) if k not in tried]
        heap = [entry for entry in heap if -entry[0] >= min_gain]
        heapq.heapify(heap)
        while added < budget and heap:
            _, index = heapq.heappop(heap)
            gain = gain_of(index)
            if gain < min_gain:
                continue
            if heap and (-gain, index) > heap[0]:
                heapq.heappush(heap, (-gain, index))
                continue
            tried.add(index)
            d, u, v, gaps = pairs[index]
            mids = len(gaps) - 1
            pu, pv = tree.pos(u), tree.pos(v)
            along = [sum(gaps[:k]) / d for k in range(1, mids + 1)]
            points = [(pu[0] + (pv[0] - pu[0]) * t, pu[1] + (pv[1] - pu[1]) * t) for t in along]
            links = [(('e', u), ('n', 0))] + [(('n', k), ('n', k + 1)) for k in range(mids - 1)] + [(('n', mids - 1), ('e', v))]
            region = tree.nodes[u]['region']
            if not b.fits(points, links, region, None, None):
                continue
            ids = []
            for p in points:
                made += 1
                if made % CONNECTOR_ATTRIBUTE_EVERY == 0:
                    ids.append(tree.add('Attribute', p, C.ANY_ATTRIBUTE, region=region, tag='connector', group='connector'))
                elif region in C.REGIONS:
                    ids.append(b.small(p, C.START_EXITS[region], region, 'connector', f'connector:{region}', 'connector'))
                else:
                    ids.append(b.small(p, [C.BORDER_CLUSTERS[region][0]['small']], region, 'connector', f'connector:{region}', 'connector'))
            chain = [u] + ids + [v]
            tree.chain(chain)
            for a, c in zip(chain, chain[1:]):
                adjacency[a].add(c)
                adjacency[c].add(a)
            # A shortcut must not break a distance rule; take it back if it does. One that mends a rule keeps it mended.
            now = distance_misses(tree, starts)
            if not now <= misses:
                for a, c in zip(chain, chain[1:]):
                    tree.unlink(a, c)
                    adjacency[a].discard(c)
                    adjacency[c].discard(a)
                for i in reversed(ids):
                    assert i == len(tree.nodes) - 1
                    tree.nodes.pop()
                    del adjacency[i]
                made -= len(ids)
                continue
            misses = now
            added += len(ids)
    b.connectors = added


def mend_medians(b, starts):
    """When a class's middle notable is still too far out, adds connectors from nearer nodes of the class to the
    nearest notables beyond the limit (or the node before one), until the median is back in range."""
    tree = b.tree
    high = C.DISTANCE_RULES['own_notable_median'][1]
    added = 0
    for name, start in starts.items():
        for _ in range(12):
            dist = distances_from(tree, tree.adjacency, start)
            own = sorted(dist[i] for i, n in enumerate(tree.nodes) if n['region'] == name and n['kind'] == 'Notable' and i in dist)
            if own[len(own) // 2] <= high:
                break
            misses = distance_misses(tree, starts)
            region_nodes = [i for i, n in enumerate(tree.nodes) if n['region'] == name and i in dist and n['kind'] in ('Small', 'Attribute', 'Notable')]
            options = []
            for t in (i for i in region_nodes if tree.nodes[i]['kind'] == 'Notable' and dist[i] > high):
                for w in [t] + [m for m in tree.adjacency[t] if tree.nodes[m]['kind'] == 'Small' and dist[m] == dist[t] - 1]:
                    for u in region_nodes:
                        d = math.dist(tree.pos(u), tree.pos(w))
                        gaps = connector_gaps(tree.nodes[u]['kind'], tree.nodes[w]['kind'], d) if 52 <= d <= 118 else None
                        if gaps and dist[u] + len(gaps) + (w != t) <= high:
                            options.append((dist[t], d, u, w, t, gaps))
            options.sort()
            for _, d, u, w, t, gaps in options:
                pu, pv = tree.pos(u), tree.pos(w)
                along = [sum(gaps[:k]) / d for k in range(1, len(gaps))]
                points = [(pu[0] + (pv[0] - pu[0]) * f, pu[1] + (pv[1] - pu[1]) * f) for f in along]
                links = [(('e', u), ('n', 0))] + [(('n', k), ('n', k + 1)) for k in range(len(points) - 1)] + [(('n', len(points) - 1), ('e', w))]
                if not b.fits(points, links, name, None, None):
                    continue
                snap = b.snapshot()
                ids = [b.small(p, C.START_EXITS[name], name, 'connector', f'connector:{name}', 'connector') for p in points]
                tree.chain([u] + ids + [w])
                if distance_misses(tree, starts) <= misses:
                    added += len(ids)
                    break
                b.restore(snap)
            else:
                break
    b.connectors += added


def connector_gaps(kind_u, kind_v, d):
    """The link lengths of a connector of length d between nodes of these kinds: one to three small nodes, the
    links about 30 long, and the first and last long enough to keep a notable's room. None if it can't be done."""
    ends = [Builder.room(kind_u, 'Small') + 0.5, Builder.room(kind_v, 'Small') + 0.5]
    for mids in sorted((1, 2, 3), key=lambda m: abs(d / (m + 1) - 30)):
        base = [ends[0]] + [MIN_SPACING + 1] * (mids - 1) + [ends[1]]
        if sum(base) <= d and d / (mids + 1) <= 45:
            return [g * d / sum(base) for g in base]
    return None


def distance_misses(tree, starts):
    """How many distance rules the tree misses (the checker's rules, without the report)."""
    return set(check(tree, starts, geometry=False)[2])


# --- the checker ---------------------------------------------------------------------------------------------------

def distances_from(tree, adjacency, start):
    """Steps from a start to every node, without passing through another start or the sanctum."""
    dist = {start: 0}
    queue = collections.deque([start])
    while queue:
        n = queue.popleft()
        for m in adjacency[n]:
            if m in dist or tree.nodes[m]['kind'] in ('Start', 'Ascendancy', 'AscendancyNotable'):
                continue
            dist[m] = dist[n] + 1
            queue.append(m)
    return dist


def check(tree, starts, geometry=True):
    """Returns (errors, report lines). Errors are hard rules; the report says how every distance rule went."""
    errors, report = [], []
    nodes = tree.nodes
    main = tree.main_nodes()
    main_set = set(main)
    adjacency = collections.defaultdict(set)
    for a, c in tree.links:
        adjacency[a].add(c)
        adjacency[c].add(a)
    allocatable = [i for i in main if nodes[i]['kind'] != 'Start']
    kinds = collections.Counter(nodes[i]['kind'] for i in allocatable)
    report.append(f'main tree: {len(allocatable)} allocatable nodes {dict(kinds)}; ascendancy nodes {len(nodes) - len(main)}')

    missed = []
    section = ['tree']

    def rule(label, value, low=None, high=None):
        ok = (low is None or value >= low) and (high is None or value <= high)
        if not ok:
            missed.append(f'{section[0]}: {label}')
        bounds = f'{low if low is not None else ""}..{high if high is not None else ""}'
        report.append(f'  {"ok  " if ok else "MISS"} {label}: {value} (wanted {bounds})')
        return ok

    R = C.DISTANCE_RULES
    misses = 0
    share = kinds['Attribute'] / len(allocatable)
    misses += not rule('attribute share', round(share, 3), *R['attribute_share'])
    share = kinds['Notable'] / len(allocatable)
    misses += not rule('notable share', round(share, 3), *R['notable_share'])
    if kinds['Shrine'] != len(C.SHRINES):
        errors.append(f'{kinds["Shrine"]} shrines, wanted {len(C.SHRINES)}')
    if kinds['JewelSocket'] != len(C.JEWEL_SOCKETS):
        errors.append(f'{kinds["JewelSocket"]} jewel sockets, wanted {len(C.JEWEL_SOCKETS)}')

    neighbours = {}
    for name, region in C.REGIONS.items():
        others = sorted(C.REGIONS, key=lambda other: abs(angle_diff(C.REGIONS[other]['angle'], region['angle'])))
        neighbours[name] = (others[1:3], others[3])
    from_start = {}
    for name in C.REGIONS:
        start = starts[name]
        dist = distances_from(tree, adjacency, start)
        from_start[name] = dist
        missing = main_set - set(dist) - set(starts.values())
        if missing:
            errors.append(f'{name} cannot reach {len(missing)} nodes, e.g. {sorted(missing)[:5]}')
        if len(adjacency[start]) != 2:
            errors.append(f'{name} start has {len(adjacency[start])} links, wanted 2')

        def steps(region, kind):
            return sorted(dist[i] for i in main if nodes[i]['region'] == region and nodes[i]['kind'] == kind and i in dist)

        report.append(f'{name}:')
        section[0] = name
        own = steps(name, 'Notable')
        misses += not rule('own first notable', own[0], *R['own_first_notable'])
        misses += not rule('own notable median', own[len(own) // 2], *R['own_notable_median'])
        misses += not rule('own farthest notable', own[-1], *R['own_farthest_notable'])
        for value in steps(name, 'Shrine'):
            misses += not rule('own shrine', value, *R['own_shrines'])
        misses += not rule('class jewel', dist[[i for i in main if nodes[i]['kind'] == 'JewelSocket' and nodes[i]['region'] == name][0]], *R['class_jewel'])
        border_firsts = []
        for bname, border in C.BORDERS.items():
            if name not in border['between']:
                continue
            first = steps(bname, 'Notable')[0]
            border_firsts.append(first)
            misses += not rule(f'{bname} first notable', first, *R['border_first_notable'])
            misses += not rule(f'{bname} shrine', steps(bname, 'Shrine')[0], *R['border_shrine'])
            misses += not rule(f'{bname} jewel', steps(bname, 'JewelSocket')[0], *R['border_jewel'])
        if R['border_equal'] and len(set(border_firsts)) > 1:
            misses += not rule('both borders equally near', 0, 1, 1)
        near, opposite = neighbours[name]
        for other in near:
            misses += not rule(f'neighbour {other} first notable', steps(other, 'Notable')[0], R['neighbour_first_notable_min'])
            misses += not rule(f'neighbour {other} shrine', steps(other, 'Shrine')[0], R['neighbour_shrine_min'])
        misses += not rule(f'opposite {opposite} first notable', steps(opposite, 'Notable')[0], *R['opposite_first_notable'])
        misses += not rule(f'opposite {opposite} shrine', steps(opposite, 'Shrine')[0], R['opposite_shrine_min'])
        report.append(f'  farthest node {max(dist.values())} steps')

    # A border's notables, shrine and jewel socket are as many steps from one neighbour as from the other.
    report.append('borders:')
    section[0] = 'borders'
    for bname, border in C.BORDERS.items():
        one, other = border['between']
        uneven = [i for i in main if nodes[i]['region'] == bname and nodes[i]['kind'] in ('Notable', 'Shrine', 'JewelSocket')
                  and from_start[one].get(i) != from_start[other].get(i)]
        misses += not rule(f'{bname} notables, shrine and jewel nearer one neighbour', len(uneven), 0, 0)

    if not geometry:
        return errors, report, missed
    # Geometry: no crossings, no crowding, no link through Tristram.
    links = [(a, c) for a, c in tree.links if a in main_set and c in main_set]
    crossings = 0
    for i, (a, c) in enumerate(links):
        for d, e in links[i + 1:]:
            if len({a, c, d, e}) == 4 and seg_cross(tree.pos(a), tree.pos(c), tree.pos(d), tree.pos(e)):
                crossings += 1
                report.append(f'  crossing: {a}-{c} ({nodes[a]["tag"]}/{nodes[c]["tag"]}) with {d}-{e} ({nodes[d]["tag"]}/{nodes[e]["tag"]})')
    if crossings:
        errors.append(f'{crossings} crossing links')
    # Spacing by kind: two notables NOTABLE_SPACING, a notable or shrine and anything BIG_SPACING, the rest MIN_SPACING.
    close = collections.Counter()
    for i, a in enumerate(main):
        for c in main[i + 1:]:
            need = spacing(nodes[a]['kind'], nodes[c]['kind'])
            if math.dist(tree.pos(a), tree.pos(c)) < need:
                close[need] += 1
                report.append(f'  crowded: {a} ({nodes[a]["kind"]}) and {c} ({nodes[c]["kind"]}) {math.dist(tree.pos(a), tree.pos(c)):.1f} apart, wanted {need}')
    for need, count in sorted(close.items()):
        errors.append(f'{count} pairs of nodes closer than {need}')
    crowded_links = sum(1 for a, c in links for i in main
                        if i not in (a, c) and point_seg_distance(tree.pos(i), tree.pos(a), tree.pos(c)) < LINK_CLEARANCE - 1)
    report.append(f'links passing within {LINK_CLEARANCE - 1} of a node they do not join: {crowded_links}')
    busy = [i for i in main if len(adjacency[i]) > MAX_LINKS]
    if busy:
        errors.append(f'{len(busy)} nodes with more than {MAX_LINKS} links, e.g. {busy[:5]}')
    far = [i for i in main if math.hypot(*tree.pos(i)) > MAX_RADIUS + 0.5]
    if far:
        errors.append(f'{len(far)} nodes beyond radius {MAX_RADIUS}, e.g. {far[:5]}')
    through_wall = [l for l in links if point_seg_distance((0, 0), tree.pos(l[0]), tree.pos(l[1])) < WALL_RADIUS]
    if through_wall:
        errors.append(f'{len(through_wall)} links pass through Tristram')
    lengths = sorted(math.dist(tree.pos(a), tree.pos(c)) for a, c in links)
    report.append(f'links: {len(links)}, length min {lengths[0]:.0f} median {lengths[len(lengths) // 2]:.0f} max {lengths[-1]:.0f}; '
                  f'crossings {crossings}; max radius {max(math.hypot(*tree.pos(i)) for i in main):.0f}')
    degrees = collections.Counter(len(adjacency[i]) for i in main)
    report.append(f'degree histogram {sorted(degrees.items())}')
    names = [n['name'] for n in nodes if n['name'] and n['kind'] != 'Start']
    duplicates = sorted({x for x in names if names.count(x) > 1})
    if duplicates:
        errors.append(f'duplicate names: {duplicates}')
    report.append(f'distance rules missed: {misses}')
    return errors, report, misses


# --- output --------------------------------------------------------------------------------------------------------

def write(tree, path):
    adjacency = collections.defaultdict(set)
    for a, c in tree.links:
        adjacency[a].add(c)
        adjacency[c].add(a)
    with open(path, 'w', newline='') as out:
        out.write('id\tname\tkind\tclass\tx\ty\tstats\tlinks\tregion\tlore\tgroup\r\n')
        for i, node in enumerate(tree.nodes):
            node_stats = ','.join(f'{stat}={value}' for stat, value in node['stats'])
            links = ','.join(str(link) for link in sorted(adjacency[i]))
            out.write(f'{i}\t{node["name"]}\t{node["kind"]}\t{node["class"]}\t{round(node["x"])}\t{round(node["y"])}\t{node_stats}\t{links}\t{node["region"]}\t{node["lore"]}\t{node["group"]}\r\n')


# The game's colours, so the preview looks like the panel: PAL16 ramps of the game palette (entries 160-255, the
# same on every level; levels/l1data/l1.pal). Index 0 of each ramp is the lightest.
PAL16 = {
    'BEIGE': [(232, 202, 202), (215, 178, 178), (202, 158, 158), (189, 143, 143), (179, 128, 128), (168, 113, 113), (165, 90, 90), (156, 73, 73),
              (139, 65, 65), (121, 57, 57), (104, 49, 49), (86, 41, 41), (68, 33, 33), (51, 25, 25), (27, 14, 14), (12, 7, 7)],
    'BLUE': [(200, 205, 234), (178, 183, 215), (159, 165, 198), (147, 153, 185), (135, 141, 172), (121, 127, 160), (102, 112, 153), (88, 99, 141),
             (78, 88, 125), (67, 76, 111), (57, 65, 95), (47, 54, 80), (37, 43, 65), (25, 30, 45), (13, 17, 27), (5, 7, 12)],
    'YELLOW': [(255, 227, 164), (238, 209, 140), (221, 196, 126), (204, 183, 117), (188, 168, 108), (171, 154, 99), (152, 139, 93), (135, 126, 84),
               (120, 111, 73), (105, 96, 63), (91, 81, 52), (72, 64, 39), (57, 49, 29), (49, 40, 22), (26, 20, 8), (20, 11, 0)],
    'ORANGE': [(255, 226, 179), (244, 201, 150), (231, 179, 126), (220, 159, 112), (208, 140, 98), (199, 123, 82), (204, 97, 51), (199, 75, 31),
               (177, 67, 27), (155, 59, 24), (133, 50, 19), (111, 41, 16), (90, 34, 12), (63, 23, 8), (37, 14, 3), (15, 5, 0)],
    'RED': [(255, 189, 189), (244, 150, 150), (232, 125, 125), (224, 108, 108), (216, 91, 91), (207, 73, 73), (199, 56, 56), (191, 39, 39),
            (169, 34, 34), (147, 30, 30), (124, 25, 25), (102, 21, 21), (79, 17, 17), (57, 13, 13), (35, 9, 9), (12, 5, 5)],
    'GRAY': [(243, 243, 243), (222, 222, 222), (204, 204, 204), (184, 184, 184), (163, 163, 163), (148, 148, 148), (133, 133, 133), (115, 115, 115),
             (102, 102, 102), (89, 89, 89), (76, 76, 76), (61, 61, 61), (46, 46, 46), (30, 30, 30), (17, 17, 17), (255, 255, 255)],
}
# Each area's ramp: the classes in their own colour, the border zones beige.
REGION_RAMPS = {'Warrior': 'GRAY', 'Sorcerer': 'BLUE', 'Rogue': 'RED', 'Monk': 'ORANGE'}
ROAD_GROUPS = ('gate', 'wall', 'ring1', 'ring2')
# The panel's geometry (Source/panels/passive_tree_panel.cpp): TreeExtent, SealRadius, RegionNameRadius.
PANEL_EXTENT = 720
SEAL_RADIUS = 672
REGION_NAME_RADIUS = 692


def preview(tree, path):
    """A picture of the tree in the panel's colours and shapes: notables framed in gold and white, shrines as
    diamonds, sockets as holes, "+5" nodes with a beige rim; roads (gate, wall and rings) as double lines; each
    area tinted (Warrior grey, Sorcerer blue, Rogue red, Monk orange, borders beige); names round the rim."""
    from PIL import Image, ImageDraw, ImageFont
    size = 1600
    scale = size / 2 / PANEL_EXTENT
    image = Image.new('RGB', (size, size), (0, 0, 0))
    draw = ImageDraw.Draw(image)
    try:
        font = ImageFont.load_default(size=17)
    except TypeError:
        font = ImageFont.load_default()

    def at(i):
        return (size / 2 + tree.nodes[i]['x'] * scale, size / 2 + tree.nodes[i]['y'] * scale)

    def ring(radius, color, width):
        r = radius * scale
        draw.ellipse([size / 2 - r, size / 2 - r, size / 2 + r, size / 2 + r], outline=color, width=width)

    ring(SEAL_RADIUS, PAL16['RED'][11], 3)
    for radius in (R1, R2):
        ring(radius, PAL16['RED'][13], 1)
    r = SANCTUM_RADIUS * scale
    draw.ellipse([size / 2 - r, size / 2 - r, size / 2 + r, size / 2 + r], fill=PAL16['GRAY'][14], outline=PAL16['YELLOW'][6], width=2)
    main = set(tree.main_nodes())
    links = [(a, c) for a, c in tree.links if a in main and c in main]
    # A link belongs to the group of its newer end (see the module notes).
    roads = [(a, c) for a, c in links if tree.nodes[max(a, c)]['group'] in ROAD_GROUPS]
    for a, c in links:
        draw.line([at(a), at(c)], fill=PAL16['GRAY'][11], width=2)
    for a, c in roads:
        draw.line([at(a), at(c)], fill=PAL16['BEIGE'][11], width=6)
    for a, c in roads:
        draw.line([at(a), at(c)], fill=(0, 0, 0), width=2)

    sizes = {'Start': 13, 'Notable': 10, 'Shrine': 13, 'JewelSocket': 9, 'Attribute': 5, 'Small': 5}
    rim = max(round(scale * 1.5), 1)
    for i in sorted(main):
        n = tree.nodes[i]
        x, y = at(i)
        radius = sizes[n['kind']] * scale
        ramp = PAL16[REGION_RAMPS.get(n['region'], 'BEIGE')]
        if n['region'] not in REGION_RAMPS:
            ramp = ramp[:3] + ramp[:-3]  # the border zones three shades lighter, so they don't read as Rogue red

        def fill(extent, color):
            if n['kind'] == 'Shrine':
                draw.polygon([(x, y - extent), (x + extent, y), (x, y + extent), (x - extent, y)], fill=color)
            else:
                draw.ellipse([x - extent, y - extent, x + extent, y + extent], fill=color)

        if n['kind'] == 'Start':
            fill(radius, PAL16['RED'][6])
        elif n['kind'] == 'Notable':
            fill(radius + rim, PAL16['ORANGE'][4])
            fill(radius, PAL16['GRAY'][15])
            fill(max(radius - rim, 1), ramp[7])
        elif n['kind'] == 'Shrine':
            fill(radius + rim, PAL16['BLUE'][4])
            fill(radius, ramp[7])
        elif n['kind'] == 'JewelSocket':
            fill(radius + rim, ramp[7])
            fill(radius, PAL16['GRAY'][4])
            fill(max(radius - 2 * rim, 1), (0, 0, 0))
        elif n['kind'] == 'Attribute':
            fill(radius + rim, PAL16['BEIGE'][6])
            fill(radius, ramp[9])
        else:
            fill(radius, ramp[9])

    # Area names round the rim, each in the direction of its nodes, as the panel places them.
    sums = collections.defaultdict(lambda: [0.0, 0.0])
    for i in main:
        sums[tree.nodes[i]['region']][0] += tree.nodes[i]['x']
        sums[tree.nodes[i]['region']][1] += tree.nodes[i]['y']
    for name, (sx, sy) in sums.items():
        length = math.hypot(sx, sy)
        if not length:
            continue
        x, y = size / 2 + sx / length * REGION_NAME_RADIUS * scale, size / 2 + sy / length * REGION_NAME_RADIUS * scale
        color = PAL16['GRAY'][3] if name in C.REGIONS else PAL16['GRAY'][7]
        draw.text((x, y), name, fill=color, font=font, anchor='mm')
    draw.text((size / 2, size / 2), 'Tristram', fill=PAL16['YELLOW'][6], font=font, anchor='mm')
    image.save(path)


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    args = list(sys.argv[1:])
    options = {}
    for flag in ('--png', '--report'):
        if flag in args:
            k = args.index(flag)
            options[flag] = args[k + 1]
            del args[k:k + 2]
    path = args[0] if args else os.path.join(root, 'mods', 'poe2', 'txtdata', 'poe2', 'passive_tree.tsv')
    tree, starts, _, builder = build()
    errors, report, _ = check(tree, starts)
    text = '\n'.join(report + [f'ERROR {e}' for e in errors])
    text += f'\nplacement: {builder.connectors} connector nodes added (budget {CONNECTOR_NODES})'
    print(text)
    if '--report' in options:
        with open(options['--report'], 'w') as f:
            f.write(text + '\n')
    if '--png' in options:
        preview(tree, options['--png'])
    if errors:
        sys.exit('tree rules broken; nothing written')
    write(tree, path)
    print(f'wrote {len(tree.nodes)} nodes to {path}')


if __name__ == '__main__':
    main()
