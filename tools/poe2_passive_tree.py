#!/usr/bin/env python3
"""Generates the POE2 Mashup passive tree: mods/poe2/txtdata/poe2/passive_tree.tsv.

Four classes start around the central sanctum, which holds the ascendancies. From each start, branches
run outwards and form shapes: weapons (a sword, an axe, a bow, a dagger, staves), circles and triangles.
Notables sit at the ends and corners of the shapes, jewel sockets in their hearts, and shrines (big
trade-offs) hang off the outer ring. "+5 to any Attribute" nodes, where the player picks the stat, are
spread along every path; the tree is the only source of attributes besides items.

The classes aren't evenly spaced, and each region has its own shapes, so the tree isn't symmetrical.

Usage: tools/poe2_passive_tree.py [output.tsv] [--png preview.png]
"""

import math
import os
import sys

# Class start angles in degrees on screen: 0 is right, -90 is up.
CLASSES = [
    ('Warrior', -100),
    ('Monk', -8),
    ('Rogue', 78),
    ('Sorcerer', 188),
]

START_RADIUS = 112
INNER_RING = 142
OUTER_RING = 386
SHRINE_RADIUS = 418
MIN_DISTANCE = 15

ANY_ATTRIBUTE = [('AnyAttribute', 5)]

THEMES = {
    'Warrior': [('Damage', 5), ('ArmorPercent', 8), ('LifePercent', 4), ('ToHit', 4), ('Strength', 10)],
    'Monk': [('EvasionPercent', 8), ('Damage', 4), ('ToHit', 4), ('LifeRegen', 1), ('ArmorPercent', 6)],
    'Rogue': [('ToHit', 5), ('EvasionPercent', 8), ('Damage', 4), ('LightningResist', 6), ('Dexterity', 10)],
    'Sorcerer': [('ManaPercent', 5), ('EnergyShieldPercent', 8), ('ManaRegen', 10), ('MagicResist', 6), ('Magic', 10)],
    'Ring': [('LifePercent', 3), ('ManaPercent', 3), ('AllResist', 3), ('LifeRegen', 1)],
}

NOTABLES = {
    'Warrior': {
        'pommel': ("King Leoric's Might", [('Strength', 20)]),
        'guard_left': ("Arkaine's Valor", [('ArmorPercent', 25), ('AllResist', 5)]),
        'guard_right': ("Griswold's Edge", [('Damage', 15), ('ToHit', 8)]),
        'tip': ("Lachdanan's Oath", [('Damage', 20), ('LifePercent', 8)]),
        'axe_head': ('Executioner', [('Damage', 22), ('AttackSpeed', 1)]),
        'axe_beard': ('Cleaving Blow', [('Damage', 12), ('Strength', 8)]),
        'shield_top': ('Defender of Tristram', [('ArmorPercent', 20), ('LifeRegen', 3)]),
        'shield_bottom': ('Shield Wall', [('ArmorPercent', 15), ('AllResist', 6)]),
    },
    'Monk': {
        'staff_foot': ('Ivgorod Discipline', [('Strength', 10), ('Dexterity', 10)]),
        'staff_band': ('Sky Tower Kata', [('AttackSpeed', 1), ('Damage', 8)]),
        'beads_top': ("Patriarch's Calm", [('LifeRegen', 4), ('AllResist', 6)]),
        'beads_bottom': ('Iron Body', [('ArmorPercent', 15), ('EvasionPercent', 15)]),
        'tri_a': ('Fists of Zakarum', [('Damage', 18), ('ToHit', 6)]),
        'tri_b': ('Spirit Strike', [('Spirit', 10), ('Damage', 8)]),
        'tri_c': ('Hundred Palms', [('AttackSpeed', 1), ('Dexterity', 8)]),
    },
    'Rogue': {
        'nock': ('Sightless Eye', [('Dexterity', 20)]),
        'arrowhead': ('Windforce Draw', [('Damage', 18), ('AttackSpeed', 1)]),
        'limb_top': ('Blackoak Fletching', [('Damage', 15), ('ToHit', 8)]),
        'limb_bottom': ('Eaglehorn Focus', [('ToHit', 10), ('EvasionPercent', 10)]),
        'dagger_tip': ('Gnat Sting', [('Damage', 15), ('LightningResist', 12)]),
        'dagger_hilt': ('Rogue Encampment', [('EvasionPercent', 25), ('LifePercent', 4)]),
    },
    'Sorcerer': {
        'tri_top': ('Vizjerei Lore', [('Magic', 20)]),
        'tri_left': ("Horazon's Study", [('SpellLevels', 1)]),
        'tri_right': ('Mana Shield Ward', [('EnergyShieldPercent', 25), ('ManaPercent', 6)]),
        'staff_orb': ('Arcane Star', [('ManaRegen', 30), ('Magic', 6)]),
        'staff_foot': ('Storm Spire', [('LightningResist', 15), ('CastSpeed', 1)]),
        'orb_far': ('Staff of Shadows', [('EnergyShieldPercent', 15), ('MagicResist', 12)]),
    },
    'Warrior|Monk': {
        'a': ('Swordsmanship', [('Strength', 6), ('Dexterity', 6)]),
        'b': ('Blade Dance', [('Damage', 10), ('EvasionPercent', 10)]),
        'c': ('Battle Hardened', [('ArmorPercent', 15), ('LifePercent', 6)]),
    },
    'Monk|Rogue': {
        'a': ('Light Feet', [('EvasionPercent', 15), ('Dexterity', 6)]),
        'b': ('Deadly Precision', [('ToHit', 10), ('Damage', 8)]),
    },
    'Rogue|Sorcerer': {
        'a': ('Shadowcraft', [('Dexterity', 5), ('Magic', 5), ('EvasionPercent', 8)]),
        'b': ('Enchanted Arrows', [('Damage', 10), ('ManaPercent', 6)]),
        'c': ('Scholar of Lut Gholein', [('Magic', 8), ('ManaRegen', 15)]),
    },
    'Sorcerer|Warrior': {
        'a': ('Blood Magic Rites', [('LifePercent', 6), ('ManaPercent', 6)]),
        'b': ('Iron Constitution', [('Vitality', 10), ('Strength', 6)]),
    },
}

# Shrines past the outer ring: a big boon with a price, like Diablo's shrines.
SHRINES = [
    (-122, 'Gloomy Shrine', [('ArmorPercent', 40), ('Damage', -10)]),
    (-78, 'Weird Shrine', [('Damage', 25), ('ToHit', -5)]),
    (-30, "Murphy's Shrine", [('AttackSpeed', 1), ('ArmorPercent', -15)]),
    (14, 'Divine Shrine', [('LifePercent', 12), ('ManaPercent', -10)]),
    (58, 'Abandoned Shrine', [('Dexterity', 20), ('Strength', -10)]),
    (102, 'Glowing Shrine', [('AllResist', 15), ('Magic', -10)]),
    (150, 'Fascinating Shrine', [('SpellLevels', 1), ('ManaPercent', -10)]),
    (212, 'Stone Shrine', [('EnergyShieldPercent', 40), ('ManaRegen', -25)]),
]

# Two ascendancies per class: the root's stats, then six notables.
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
            ('Battle Frenzy', [('AttackSpeed', 1)]),
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
            ('Rapid Volley', [('AttackSpeed', 1)]),
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
            ('Storm Channel', [('CastSpeed', 1)]),
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
            ('Whirlwind Kata', [('AttackSpeed', 1)]),
            ('Blessed Staff', [('Damage', 20), ('ToHit', 15)]),
            ('Perfect Form', [('Damage', 30)]),
        ]),
        ('Ivgorod Patriarch', [('Magic', 10)], [
            ('Faith Ward', [('MagicResist', 20)]),
            ('Meditation', [('ManaRegen', 40)]),
            ('Smite', [('Damage', 20)]),
            ('Spirit of Ivgorod', [('ManaPercent', 20)]),
            ('Iron Skin', [('EnergyShieldPercent', 30)]),
            ('Divine Wrath', [('Damage', 30), ('CastSpeed', 1)]),
        ]),
    ],
}

# Where each ascendancy's nodes sit, relative to its column in the sanctum, and how they link:
# the root at the bottom, then a fork that joins again at the top.
ASCENDANCY_LAYOUT = [(0, 30), (0, 10), (-18, -5), (18, -5), (-18, -28), (18, -28), (0, -45)]
ASCENDANCY_LINKS = [(0, 1), (1, 2), (1, 3), (2, 4), (3, 5), (4, 6), (5, 6)]
ASCENDANCY_COLUMNS = (-38, 38)


class Tree:
    def __init__(self):
        self.nodes = []

    def add(self, kind, point, stats, name='', hero_class=''):
        x, y = point
        self.nodes.append({'name': name, 'kind': kind, 'class': hero_class, 'x': round(x), 'y': round(y), 'stats': list(stats), 'links': set()})
        return len(self.nodes) - 1

    def link(self, a, b):
        assert a != b
        self.nodes[a]['links'].add(b)
        self.nodes[b]['links'].add(a)

    def chain(self, ids):
        for a, b in zip(ids, ids[1:]):
            self.link(a, b)


def polar(radius, degrees):
    angle = math.radians(degrees)
    return (radius * math.cos(angle), radius * math.sin(angle))


class Frame:
    """A local frame for a shape: u runs outwards along the axis, v across it (to the right of the axis)."""

    def __init__(self, origin, degrees):
        self.origin = origin
        angle = math.radians(degrees)
        self.u = (math.cos(angle), math.sin(angle))
        self.v = (-math.sin(angle), math.cos(angle))

    def at(self, u, v=0):
        return (self.origin[0] + u * self.u[0] + v * self.v[0], self.origin[1] + u * self.u[1] + v * self.v[1])


def radial_frame(radius, degrees):
    return Frame(polar(radius, degrees), degrees)


class Builder:
    def __init__(self):
        self.tree = Tree()
        self.counters = {}

    def small(self, point, theme, attribute=False):
        """A small node with the theme's next stat, or a "+5 to any Attribute" node."""
        if attribute:
            return self.tree.add('Attribute', point, ANY_ATTRIBUTE)
        count = self.counters.get(theme, 0)
        self.counters[theme] = count + 1
        if '|' in theme:
            first, second = theme.split('|')
            themes = THEMES[first if count % 2 == 0 else second]
            stat = themes[(count // 2) % len(themes)]
        else:
            themes = THEMES[theme]
            stat = themes[count % len(themes)]
        return self.tree.add('Small', point, [stat])

    def notable(self, point, theme, key):
        name, stats = NOTABLES[theme][key]
        return self.tree.add('Notable', point, stats, name)

    def socket(self, point):
        return self.tree.add('JewelSocket', point, [])

    def path(self, points, theme, attribute_every=3, attribute_offset=1):
        """Small nodes along the points, linked in order; every few is an attribute node."""
        ids = [self.small(p, theme, attribute=(i % attribute_every == attribute_offset)) for i, p in enumerate(points)]
        self.tree.chain(ids)
        return ids

    def circle(self, center, radius, count, theme, start_degrees=-90, special=None):
        """A ring of nodes. `special` maps a position on the ring to a node id made by the caller's function."""
        ids = []
        for i in range(count):
            point = (center[0] + radius * math.cos(math.radians(start_degrees + 360 * i / count)),
                     center[1] + radius * math.sin(math.radians(start_degrees + 360 * i / count)))
            if special and i in special:
                ids.append(special[i](point))
            else:
                ids.append(self.small(point, theme, attribute=(i % 4 == 2)))
        for a, b in zip(ids, ids[1:] + ids[:1]):
            self.tree.link(a, b)
        return ids


def line(frame, u_from, u_to, count, v=0):
    """`count` points evenly spaced from u_from to u_to, both included."""
    if count == 1:
        return [frame.at(u_from, v)]
    return [frame.at(u_from + (u_to - u_from) * i / (count - 1), v) for i in range(count)]


def build():
    b = Builder()
    tree = b.tree

    starts = {name: tree.add('Start', polar(START_RADIUS, degrees), [], name, name) for name, degrees in CLASSES}

    # The inner ring round the sanctum joins the classes; its nodes are the first stepping stones.
    inner = {}
    for k in range(30):
        degrees = -100 + 12 * k
        inner[degrees] = b.small(polar(INNER_RING, degrees), 'Ring', attribute=(k % 2 == 0))
    keys = sorted(inner)
    for a, c in zip(keys, keys[1:] + keys[:1]):
        tree.link(inner[a], inner[c])

    def inner_at(degrees):
        return inner[min(inner, key=lambda d: abs(((d - degrees + 180) % 360) - 180))]

    for name, degrees in CLASSES:
        tree.link(starts[name], inner_at(degrees))
        tree.link(starts[name], inner_at(degrees - 12))
        tree.link(starts[name], inner_at(degrees + 12))

    # The outer ring holds the far ends together; it has gaps where the big shapes reach it.
    outer = {}
    for k in range(48):
        degrees = -100 + 7.5 * k
        outer[degrees] = b.small(polar(OUTER_RING, degrees), 'Ring', attribute=(k % 3 == 0))
    okeys = sorted(outer)
    for a, c in zip(okeys, okeys[1:] + okeys[:1]):
        tree.link(outer[a], outer[c])

    def outer_at(degrees):
        return outer[min(outer, key=lambda d: abs(((d - degrees + 180) % 360) - 180))]

    sockets = []

    # ---- Warrior: a great sword straight out, an axe on the right, a round shield on the left.
    w = -100
    f = radial_frame(0, w)
    trunk = b.path(line(f, 168, 190, 2), 'Warrior', attribute_offset=0)
    tree.link(inner_at(w), trunk[0])
    pommel = b.notable(f.at(212), 'Warrior', 'pommel')
    tree.link(trunk[-1], pommel)
    grip = b.small(f.at(232), 'Warrior')
    tree.link(pommel, grip)
    guard_mid = b.small(f.at(252), 'Warrior', attribute=True)
    tree.link(grip, guard_mid)
    guard = [b.notable(f.at(252, -54), 'Warrior', 'guard_left'), b.small(f.at(252, -36), 'Warrior'), b.small(f.at(252, -18), 'Warrior'), guard_mid,
             b.small(f.at(252, 18), 'Warrior'), b.small(f.at(252, 36), 'Warrior'), b.notable(f.at(252, 54), 'Warrior', 'guard_right')]
    tree.chain(guard)
    blade = b.path(line(f, 272, 344, 5), 'Warrior')
    tree.link(guard_mid, blade[0])
    tip = b.notable(f.at(364), 'Warrior', 'tip')
    tree.link(tip, outer_at(w))
    tree.link(blade[-1], tip)

    # Axe: the haft runs out from the inner ring, the head curves round to the right at its top.
    axe = Frame(polar(0, 0), w + 28)
    haft = b.path(line(axe, 172, 292, 6), 'Warrior')
    tree.link(inner_at(w + 28), haft[0])
    # The head curves round on the sword's side, its edge a notable.
    head = [b.small(axe.at(316, -22), 'Warrior'), b.small(axe.at(322, -44), 'Warrior')]
    edge = b.notable(axe.at(300, -62), 'Warrior', 'axe_head')
    tail = [b.small(axe.at(276, -46), 'Warrior'), b.small(axe.at(268, -24), 'Warrior')]
    tree.chain([haft[-1]] + head + [edge] + tail + [haft[-2]])
    beard = b.notable(axe.at(318, 22), 'Warrior', 'axe_beard')
    tree.link(haft[-1], beard)
    tree.link(beard, outer_at(w + 32))

    # Shield: a ring with a jewel socket in its boss, on the left.
    shield_center = polar(272, w - 24)
    shield = b.circle(shield_center, 38, 10, 'Warrior', start_degrees=w - 24 - 180,
                      special={0: lambda p: b.small(p, 'Warrior'), 3: lambda p: b.notable(p, 'Warrior', 'shield_top'), 7: lambda p: b.notable(p, 'Warrior', 'shield_bottom')})
    shield_arm = b.path(line(Frame(polar(0, 0), w - 24), 170, 210, 2), 'Warrior', attribute_offset=1)
    tree.link(inner_at(w - 24), shield_arm[0])
    tree.link(shield_arm[-1], shield[0])
    boss = b.socket(shield_center)
    sockets.append(boss)
    tree.link(boss, shield[3])
    tree.link(boss, shield[7])
    tree.link(shield[5], outer_at(w - 24))

    # ---- Monk: a quarterstaff straight out with bands, prayer beads on the left, a triangle on the right.
    m = -8
    f = radial_frame(0, m)
    staff = b.path(line(f, 168, 348, 9), 'Monk', attribute_offset=1)
    tree.link(inner_at(m), staff[0])
    foot = b.notable(f.at(190, -26), 'Monk', 'staff_foot')
    tree.link(staff[1], foot)
    band = b.notable(f.at(280, 26), 'Monk', 'staff_band')
    tree.link(staff[5], band)
    tree.link(staff[-1], outer_at(m))

    beads_center = polar(262, m - 26)
    beads = b.circle(beads_center, 42, 12, 'Monk', start_degrees=m - 26 + 180,
                     special={3: lambda p: b.notable(p, 'Monk', 'beads_top'), 9: lambda p: b.notable(p, 'Monk', 'beads_bottom')})
    beads_arm = b.path(line(Frame(polar(0, 0), m - 26), 170, 204, 2), 'Monk')
    tree.link(inner_at(m - 24), beads_arm[0])
    tree.link(beads_arm[-1], beads[0])
    bead_socket = b.socket(beads_center)
    sockets.append(bead_socket)
    tree.link(bead_socket, beads[0])
    tree.link(bead_socket, beads[6])
    tree.link(beads[6], outer_at(m - 26))

    # Triangle: three notables at the corners, two small nodes along each side.
    def triangle(center, size, rotation, theme, corner_keys):
        corners = [polar(size, rotation + 120 * i) for i in range(3)]
        corners = [(center[0] + c[0], center[1] + c[1]) for c in corners]
        ids = [b.notable(c, theme, key) for c, key in zip(corners, corner_keys)]
        for i in range(3):
            a, c = corners[i], corners[(i + 1) % 3]
            side = [b.small((a[0] + (c[0] - a[0]) * t, a[1] + (c[1] - a[1]) * t), theme, attribute=(t > 0.5 and i == 0)) for t in (1 / 3, 2 / 3)]
            tree.chain([ids[i]] + side + [ids[(i + 1) % 3]])
        return ids

    tri = triangle(polar(286, m + 30), 46, m + 30 + 180, 'Monk', ['tri_a', 'tri_b', 'tri_c'])
    tri_arm = b.path(line(Frame(polar(0, 0), m + 30), 170, 214, 3), 'Monk')
    tree.link(inner_at(m + 30), tri_arm[0])
    tree.link(tri_arm[-1], tri[0])
    tree.link(tri[1], outer_at(m + 36))

    # ---- Rogue: a drawn bow with its arrow pointing out, a dagger on the right.
    r = 78
    f = radial_frame(0, r)
    nock_path = b.path(line(f, 166, 190, 2), 'Rogue', attribute_offset=0)
    tree.link(inner_at(r), nock_path[0])
    nock = b.notable(f.at(214), 'Rogue', 'nock')
    tree.link(nock_path[-1], nock)
    # The limb curves out from one end of the string to the other; the arrow runs through both.
    bow = Frame(f.at(240), r)
    limb = []
    for i, t in enumerate(range(-90, 91, 22)):
        t = -90 + 22.5 * i
        point = bow.at(70 * math.cos(math.radians(t)), 86 * math.sin(math.radians(t)))
        if i == 1:
            limb.append(b.notable(point, 'Rogue', 'limb_top'))
        elif i == 7:
            limb.append(b.notable(point, 'Rogue', 'limb_bottom'))
        else:
            limb.append(b.small(point, 'Rogue', attribute=(i == 4)))
    tree.chain(limb)
    string = [limb[0]] + [b.small(bow.at(0, v), 'Rogue', attribute=(v == 0)) for v in (-57, -29, 0, 29, 57)] + [limb[-1]]
    tree.chain(string)
    tree.link(nock, string[3])
    shaft = [b.small(bow.at(35), 'Rogue'), b.small(bow.at(99), 'Rogue')]
    tree.chain([string[3], shaft[0], limb[4], shaft[1]])
    arrowhead = b.notable(bow.at(124), 'Rogue', 'arrowhead')
    tree.link(shaft[1], arrowhead)
    tree.link(arrowhead, outer_at(r))

    dagger = Frame(polar(0, 0), r + 30)
    grip = b.path(line(dagger, 170, 196, 2), 'Rogue')
    tree.link(inner_at(r + 30), grip[0])
    hilt = b.notable(dagger.at(222), 'Rogue', 'dagger_hilt')
    tree.link(grip[-1], hilt)
    cross = [b.small(dagger.at(222, -22), 'Rogue'), b.small(dagger.at(222, 22), 'Rogue')]
    tree.link(cross[0], hilt)
    tree.link(hilt, cross[1])
    dblade = b.path(line(dagger, 246, 294, 3), 'Rogue')
    tree.link(hilt, dblade[0])
    dtip = b.notable(dagger.at(320), 'Rogue', 'dagger_tip')
    tree.link(dblade[-1], dtip)
    tree.link(dtip, outer_at(r + 30))

    # ---- Sorcerer: a Horadric circle with a triangle inside, a staff with an orb on the left.
    s = 188
    f = radial_frame(0, s)
    sorc_center = f.at(276)
    arm = b.path(line(f, 166, 188, 2), 'Sorcerer', attribute_offset=0)
    tree.link(inner_at(s), arm[0])
    # The triangle points away from the centre; its corners sit on the circle.
    circle_r = 66
    corners = {0: 'tri_top', 4: 'tri_left', 8: 'tri_right'}
    ring = b.circle(sorc_center, circle_r, 12, 'Sorcerer', start_degrees=s,
                    special={k: (lambda key: (lambda p: b.notable(p, 'Sorcerer', key)))(key) for k, key in corners.items()})
    tree.link(arm[-1], ring[6])
    heart = b.socket(sorc_center)
    sockets.append(heart)
    for a, c in ((0, 4), (4, 8), (8, 0)):
        pa = tree.nodes[ring[a]]
        pc = tree.nodes[ring[c]]
        mid = b.small(((pa['x'] + pc['x']) / 2, (pa['y'] + pc['y']) / 2), 'Sorcerer', attribute=(a == 4))
        tree.link(ring[a], mid)
        tree.link(mid, ring[c])
        tree.link(mid, heart)
    tree.link(ring[0], outer_at(s))

    staff = Frame(polar(0, 0), s - 36)
    shaft_ids = b.path(line(staff, 170, 280, 5), 'Sorcerer', attribute_offset=2)
    tree.link(inner_at(s - 36), shaft_ids[0])
    foot = b.notable(staff.at(206, 26), 'Sorcerer', 'staff_foot')
    tree.link(shaft_ids[1], foot)
    orb_center = staff.at(330)
    orb = b.circle(orb_center, 28, 6, 'Sorcerer', start_degrees=s - 36 + 180,
                   special={3: lambda p: b.notable(p, 'Sorcerer', 'orb_far')})
    tree.link(shaft_ids[-1], orb[0])
    orb_heart = b.notable(orb_center, 'Sorcerer', 'staff_orb')
    tree.link(orb_heart, orb[0])
    tree.link(orb[3], outer_at(s - 36))

    # ---- Where two classes meet: small shapes that mix both.
    def wheel(center_degrees, radius, theme, keys):
        center = polar(radius, center_degrees)
        ids = b.circle(center, 30, 8, theme, start_degrees=center_degrees + 180,
                       special={2: lambda p: b.notable(p, theme, keys[0]), 6: lambda p: b.notable(p, theme, keys[1])})
        hub = b.socket(center)
        sockets.append(hub)
        tree.link(hub, ids[0])
        tree.link(hub, ids[4])
        return ids

    # Warrior | Monk: a triangle pointing out, between the axe and the beads.
    wm = triangle(polar(318, -52), 32, -52, 'Warrior|Monk', ['a', 'b', 'c'])
    tree.link(wm[0], outer_at(-52))
    wm_arm = b.path(line(Frame(polar(0, 0), -52), 170, 250, 4), 'Warrior|Monk')
    tree.link(inner_at(-52), wm_arm[0])
    tree.link(wm_arm[-1], wm[1])
    tree.link(wm_arm[-1], wm[2])

    # Monk | Rogue: a wheel with a jewel socket.
    mr = wheel(52, 306, 'Monk|Rogue', ['a', 'b'])
    tree.link(mr[4], outer_at(52))
    mr_arm = b.path(line(Frame(polar(0, 0), 52), 170, 256, 4), 'Monk|Rogue')
    tree.link(inner_at(52), mr_arm[0])
    tree.link(mr_arm[-1], mr[0])

    # Rogue | Sorcerer: the widest gap, a long arc with a wheel in it.
    rs = wheel(134, 306, 'Rogue|Sorcerer', ['a', 'b'])
    tree.link(rs[4], outer_at(134))
    rs_arm = b.path(line(Frame(polar(0, 0), 134), 170, 256, 4), 'Rogue|Sorcerer')
    tree.link(inner_at(134), rs_arm[0])
    tree.link(rs_arm[-1], rs[0])
    scholar = b.notable(Frame(polar(0, 0), 134).at(226, 34), 'Rogue|Sorcerer', 'c')
    tree.link(scholar, rs_arm[2])

    # Sorcerer | Warrior: a short fork.
    sw = Frame(polar(0, 0), 212)
    sw_arm = b.path(line(sw, 170, 230, 3), 'Sorcerer|Warrior')
    tree.link(inner_at(212), sw_arm[0])
    sw_left = b.notable(sw.at(262, -24), 'Sorcerer|Warrior', 'a')
    sw_right = b.notable(sw.at(262, 24), 'Sorcerer|Warrior', 'b')
    tree.link(sw_arm[-1], sw_left)
    tree.link(sw_arm[-1], sw_right)
    sw_out = b.small(sw.at(300), 'Sorcerer|Warrior', attribute=True)
    tree.link(sw_left, sw_out)
    tree.link(sw_right, sw_out)
    tree.link(sw_out, outer_at(212))

    # Shrines hang off the outer ring.
    for degrees, title, node_stats in SHRINES:
        shrine = tree.add('Shrine', polar(SHRINE_RADIUS, degrees), node_stats, title)
        tree.link(outer_at(degrees), shrine)

    # The sanctum in the middle holds the ascendancies of the player's class, side by side.
    for name, _ in CLASSES:
        for column, (title, root_stats, ascendancy_notables) in zip(ASCENDANCY_COLUMNS, ASCENDANCIES[name]):
            ids = []
            for index, (dx, y) in enumerate(ASCENDANCY_LAYOUT):
                if index == 0:
                    ids.append(tree.add('Ascendancy', (column + dx, y), root_stats, title, name))
                else:
                    node_title, node_stats = ascendancy_notables[index - 1]
                    ids.append(tree.add('AscendancyNotable', (column + dx, y), node_stats, node_title, name))
            for a, c in ASCENDANCY_LINKS:
                tree.link(ids[a], ids[c])

    return tree, sockets


def check(tree):
    nodes = tree.nodes
    main = [i for i, n in enumerate(nodes) if not n['kind'].startswith('Ascendancy')]
    for start in (i for i in main if nodes[i]['kind'] == 'Start'):
        seen = {start}
        todo = [start]
        while todo:
            for link in nodes[todo.pop()]['links']:
                if link not in seen:
                    seen.add(link)
                    todo.append(link)
        missing = set(main) - seen
        assert not missing, f'{nodes[start]["name"]} cannot reach {sorted(missing)}'
    close = []
    for i, a in enumerate(main):
        for c in main[i + 1:]:
            distance = math.hypot(nodes[a]['x'] - nodes[c]['x'], nodes[a]['y'] - nodes[c]['y'])
            if distance < MIN_DISTANCE:
                close.append((round(distance, 1), a, c))
    assert not close, f'nodes too close: {sorted(close)[:10]}'
    names = [n['name'] for n in nodes if n['name'] and n['kind'] != 'Start']
    duplicates = {name for name in names if names.count(name) > 1}
    assert not duplicates, f'duplicate names: {duplicates}'
    for node in nodes:
        assert math.hypot(node['x'], node['y']) < 432, node


def write(tree, path):
    with open(path, 'w', newline='') as out:
        out.write('id\tname\tkind\tclass\tx\ty\tstats\tlinks\r\n')
        for i, node in enumerate(tree.nodes):
            node_stats = ','.join(f'{stat}={value}' for stat, value in node['stats'])
            links = ','.join(str(link) for link in sorted(node['links']))
            out.write(f'{i}\t{node["name"]}\t{node["kind"]}\t{node["class"]}\t{node["x"]}\t{node["y"]}\t{node_stats}\t{links}\r\n')


def preview(tree, path):
    from PIL import Image, ImageDraw
    size = 1000
    scale = size / 2 / 440
    image = Image.new('RGB', (size, size), (20, 8, 8))
    draw = ImageDraw.Draw(image)

    def at(node):
        return (size / 2 + node['x'] * scale, size / 2 + node['y'] * scale)

    for node in tree.nodes:
        if node['kind'].startswith('Ascendancy') and node['class'] != 'Warrior':
            continue
        for link in node['links']:
            draw.line([at(node), at(tree.nodes[link])], fill=(110, 110, 110), width=2)
    radius = {'Start': 13, 'Notable': 9, 'Shrine': 10, 'JewelSocket': 9, 'Attribute': 5, 'Small': 5, 'Ascendancy': 9, 'AscendancyNotable': 7}
    color = {'Start': (180, 40, 40), 'Notable': (200, 140, 60), 'Shrine': (70, 110, 200), 'JewelSocket': (60, 180, 120), 'Attribute': (200, 200, 160), 'Small': (140, 140, 140),
             'Ascendancy': (220, 180, 80), 'AscendancyNotable': (200, 150, 70)}
    for node in tree.nodes:
        if node['kind'].startswith('Ascendancy') and node['class'] != 'Warrior':
            continue
        x, y = at(node)
        rad = radius[node['kind']] * scale * 1.1
        draw.ellipse([x - rad, y - rad, x + rad, y + rad], fill=color[node['kind']])
    image.save(path)


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    args = [a for a in sys.argv[1:]]
    png = None
    if '--png' in args:
        png = args[args.index('--png') + 1]
        del args[args.index('--png'):args.index('--png') + 2]
    path = args[0] if args else os.path.join(root, 'mods', 'poe2', 'txtdata', 'poe2', 'passive_tree.tsv')
    tree, sockets = build()
    if png:
        preview(tree, png)
    check(tree)
    write(tree, path)
    kinds = {}
    for node in tree.nodes:
        kinds[node['kind']] = kinds.get(node['kind'], 0) + 1
    print(f'{len(tree.nodes)} nodes {kinds}')


if __name__ == '__main__':
    main()
