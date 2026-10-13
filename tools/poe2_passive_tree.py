#!/usr/bin/env python3
"""Generates the POE2 Mashup passive tree: mods/poe2/txtdata/poe2/passive_tree.tsv.

The tree is laid out like a Horadric seal. The six classes start on a circle around the central
sanctum, which holds the ascendancies. Three rings run around it, joined by spokes. Clusters of
notables sit between the spokes, and shrines (big trade-offs) hang off the outer ring.
Each class's region leans on its own attributes and themes, and the borders between two classes
mix both.

Usage: tools/poe2_passive_tree.py [output.tsv]
"""

import math
import os
import sys

# Class start angles in degrees on screen: 0 is right, -90 is up.
CLASSES = [
    ('Warrior', -90),
    ('Monk', -30),
    ('Rogue', 30),
    ('Bard', 90),
    ('Sorcerer', 150),
    ('Barbarian', 210),
]

START_RADIUS = 100
RINGS = (130, 205, 290)

ATTRIBUTES = {
    'Warrior': [('Strength', 5)],
    'Monk': [('Strength', 3), ('Dexterity', 3)],
    'Rogue': [('Dexterity', 5)],
    'Bard': [('Dexterity', 3), ('Magic', 3)],
    'Sorcerer': [('Magic', 5)],
    'Barbarian': [('Vitality', 5)],
}

BORDER_ATTRIBUTES = {
    'Warrior|Monk': [('Strength', 4), ('Dexterity', 2)],
    'Monk|Rogue': [('Dexterity', 4), ('Strength', 2)],
    'Rogue|Bard': [('Dexterity', 4), ('Magic', 2)],
    'Bard|Sorcerer': [('Magic', 4), ('Dexterity', 2)],
    'Sorcerer|Barbarian': [('Magic', 3), ('Vitality', 3)],
    'Barbarian|Warrior': [('Vitality', 3), ('Strength', 3)],
}

THEMES = {
    'Warrior': [('Damage', 5), ('ArmorPercent', 8), ('LifePercent', 4), ('ToHit', 4)],
    'Monk': [('EvasionPercent', 8), ('Damage', 4), ('ToHit', 4), ('LifeRegen', 1)],
    'Rogue': [('ToHit', 5), ('EvasionPercent', 8), ('Damage', 4), ('LightningResist', 6)],
    'Bard': [('ManaPercent', 4), ('ToHit', 4), ('EvasionPercent', 6), ('MagicResist', 6)],
    'Sorcerer': [('ManaPercent', 5), ('EnergyShieldPercent', 8), ('ManaRegen', 10), ('MagicResist', 6)],
    'Barbarian': [('LifePercent', 5), ('LifeRegen', 1), ('ArmorPercent', 6), ('FireResist', 6)],
}

# In order: the notable on the class's own spoke, the inner clusters left and right of the start,
# the outer clusters left and right, and the notable past the outer ring.
CLASS_NOTABLES = {
    'Warrior': [
        ("Lachdanan's Oath", [('LifePercent', 10), ('ArmorPercent', 15)]),
        ("Arkaine's Valor", [('ArmorPercent', 25), ('AllResist', 5)]),
        ("Griswold's Edge", [('Damage', 15), ('ToHit', 8)]),
        ("Warlord's Fury", [('Damage', 20), ('AttackSpeed', 1)]),
        ("King Leoric's Might", [('Strength', 15), ('LifePercent', 6)]),
        ('Defender of Tristram', [('ArmorPercent', 15), ('LifeRegen', 3)]),
    ],
    'Monk': [
        ('Ivgorod Discipline', [('Dexterity', 10), ('Strength', 10)]),
        ('Sky Tower Kata', [('AttackSpeed', 1), ('Damage', 8)]),
        ('Iron Body', [('ArmorPercent', 15), ('EvasionPercent', 15)]),
        ("Patriarch's Calm", [('LifeRegen', 4), ('AllResist', 6)]),
        ('Fists of Zakarum', [('Damage', 18), ('ToHit', 6)]),
        ('Spirit Strike', [('Spirit', 10), ('Damage', 8)]),
    ],
    'Rogue': [
        ('Sightless Eye', [('ToHit', 12), ('Damage', 8)]),
        ('Blackoak Fletching', [('Damage', 15), ('Dexterity', 6)]),
        ('Windforce Draw', [('AttackSpeed', 1), ('ToHit', 5)]),
        ('Rogue Encampment', [('EvasionPercent', 25), ('LifePercent', 4)]),
        ('Gnat Sting', [('Damage', 12), ('LightningResist', 12)]),
        ('Eaglehorn Focus', [('ToHit', 10), ('EvasionPercent', 10)]),
    ],
    'Bard': [
        ("Siren's Song", [('ManaPercent', 10), ('Magic', 8)]),
        ('Kehjistan Ballad', [('MagicResist', 15), ('ManaRegen', 20)]),
        ('Twin Blades', [('Damage', 12), ('ToHit', 8)]),
        ('Lute of Westmarch', [('CastSpeed', 1), ('Magic', 6)]),
        ('Charming Voice', [('MinionDamage', 20), ('Spirit', 10)]),
        ('Song of Tristram', [('LifePercent', 6), ('ManaPercent', 6)]),
    ],
    'Sorcerer': [
        ('Vizjerei Lore', [('Magic', 15), ('ManaPercent', 8)]),
        ("Horazon's Study", [('SpellLevels', 1)]),
        ('Mana Shield Ward', [('EnergyShieldPercent', 25), ('ManaPercent', 6)]),
        ('Arcane Star', [('ManaRegen', 30), ('Magic', 6)]),
        ('Storm Spire', [('LightningResist', 15), ('CastSpeed', 1)]),
        ('Staff of Shadows', [('EnergyShieldPercent', 15), ('MagicResist', 12)]),
    ],
    'Barbarian': [
        ('Mount Arreat', [('LifePercent', 12), ('Vitality', 8)]),
        ('Harrogath Blood', [('LifeRegen', 5), ('LifePercent', 6)]),
        ('Thunder of Bul-Kathos', [('Damage', 18), ('Strength', 8)]),
        ("Ancients' Pact", [('AllResist', 8), ('ArmorPercent', 12)]),
        ('Rage of the North', [('Damage', 12), ('AttackSpeed', 1)]),
        ('Two-Handed Fury', [('Damage', 20), ('ToHit', 5)]),
    ],
}

# Notables where two classes meet: the inner cluster, then the outer one.
HYBRID_NOTABLES = {
    'Warrior|Monk': [
        ('Swordsmanship', [('Strength', 6), ('Dexterity', 6)]),
        ('Blade Dance', [('Damage', 10), ('EvasionPercent', 10)]),
    ],
    'Monk|Rogue': [
        ('Light Feet', [('EvasionPercent', 15), ('Dexterity', 6)]),
        ('Deadly Precision', [('ToHit', 10), ('Damage', 8)]),
    ],
    'Rogue|Bard': [
        ('Shadowcraft', [('Dexterity', 5), ('Magic', 5), ('EvasionPercent', 8)]),
        ('Enchanted Arrows', [('Damage', 10), ('ManaPercent', 6)]),
    ],
    'Bard|Sorcerer': [
        ('Scholar of Lut Gholein', [('Magic', 8), ('ManaRegen', 15)]),
        ('Arcane Resonance', [('ManaPercent', 10), ('MagicResist', 10)]),
    ],
    'Sorcerer|Barbarian': [
        ('Blood Magic Rites', [('LifePercent', 6), ('ManaPercent', 6)]),
        ('Ancient Wards', [('EnergyShieldPercent', 12), ('LifeRegen', 3)]),
    ],
    'Barbarian|Warrior': [
        ('Iron Constitution', [('Vitality', 8), ('Strength', 6)]),
        ('Battle Hardened', [('ArmorPercent', 15), ('LifePercent', 6)]),
    ],
}

# Shrines past the outer ring, left and right of each class: a big boon with a price, like Diablo's shrines.
SHRINES = {
    'Warrior': [
        ('Gloomy Shrine', [('ArmorPercent', 40), ('Damage', -10)]),
        ('Weird Shrine', [('Damage', 25), ('ToHit', -5)]),
    ],
    'Monk': [
        ("Murphy's Shrine", [('AttackSpeed', 1), ('ArmorPercent', -15)]),
        ('Divine Shrine', [('LifePercent', 12), ('ManaPercent', -10)]),
    ],
    'Rogue': [
        ('Abandoned Shrine', [('Dexterity', 20), ('Strength', -10)]),
        ('Glowing Shrine', [('AllResist', 15), ('Magic', -10)]),
    ],
    'Bard': [
        ('Ornate Shrine', [('CastSpeed', 1), ('ManaPercent', -8)]),
        ('Shimmering Shrine', [('ManaPercent', 20), ('ArmorPercent', -10)]),
    ],
    'Sorcerer': [
        ('Fascinating Shrine', [('SpellLevels', 1), ('ManaPercent', -10)]),
        ('Stone Shrine', [('EnergyShieldPercent', 40), ('ManaRegen', -25)]),
    ],
    'Barbarian': [
        ('Mysterious Shrine', [('Vitality', 20), ('Strength', -3), ('Magic', -3), ('Dexterity', -3)]),
        ('Creepy Shrine', [('Strength', 25), ('Magic', -10)]),
    ],
}

# Two ascendancies per class: the root's stats, then six notables.
ASCENDANCIES = {
    'Warrior': [
        ('Knight of Westmarch', [('Strength', 10)], [
            ('Shield Wall', [('ArmorPercent', 30)]),
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
    'Bard': [
        ('Blade Dancer', [('Strength', 5), ('Dexterity', 5), ('Magic', 5)], [
            ('Graceful Steps', [('Spirit', 30)]),
            ("Dancer's Poise", [('Strength', 10), ('Dexterity', 10), ('Magic', 10)]),
            ('Rhythm of Steel', [('SpellLevels', 1)]),
            ('Whirling Blades', [('Damage', 20)]),
            ('Swift Parry', [('AllResist', 10)]),
            ('Finale', [('Spirit', 30), ('Damage', 10)]),
        ]),
        ('Kehjistan Siren', [('Vitality', 10)], [
            ('Haunting Melody', [('Damage', 25)]),
            ('Piercing Note', [('ToHit', 20)]),
            ('Ward Song', [('MagicResist', 20), ('AllResist', 5)]),
            ('Discord', [('Damage', 20)]),
            ('Lullaby of Steel', [('ArmorPercent', 25), ('EvasionPercent', 25)]),
            ('Song of Renewal', [('LifePercent', 15)]),
        ]),
    ],
    'Barbarian': [
        ('Arreat Ravager', [('Strength', 10)], [
            ('Fire of the Ancients', [('FireResist', 20)]),
            ('Frenzy', [('Damage', 25)]),
            ('Storm of Arreat', [('LightningResist', 20), ('Damage', 10)]),
            ('Mountain Strength', [('LifePercent', 12)]),
            ("Bul-Kathos' Fury", [('Damage', 20), ('FireResist', 10)]),
            ('Whirlwind', [('AttackSpeed', 1)]),
        ]),
        ("Ancients' Chosen", [('Vitality', 10)], [
            ('Forge of Harrogath', [('FireResist', 25)]),
            ("Talic's Hide", [('ArmorPercent', 35)]),
            ("Madawc's Ward", [('AllResist', 10)]),
            ("Korlic's Vigor", [('LifePercent', 15)]),
            ('Blood of Arreat', [('LifeRegen', 8)]),
            ('Living Legend', [('Damage', 30)]),
        ]),
    ],
}

# Where each ascendancy's nodes sit, relative to its column in the sanctum, and how they link:
# the root at the bottom, then a fork that joins again at the top.
ASCENDANCY_LAYOUT = [(0, 30), (0, 10), (-18, -5), (18, -5), (-18, -28), (18, -28), (0, -45)]
ASCENDANCY_LINKS = [(0, 1), (1, 2), (1, 3), (2, 4), (3, 5), (4, 6), (5, 6)]
ASCENDANCY_COLUMNS = (-40, 40)


class Tree:
    def __init__(self):
        self.nodes = []

    def add(self, kind, x, y, stats, name='', hero_class=''):
        self.nodes.append({'name': name, 'kind': kind, 'class': hero_class, 'x': round(x), 'y': round(y), 'stats': list(stats), 'links': set()})
        return len(self.nodes) - 1

    def add_polar(self, kind, radius, degrees, stats, name='', hero_class=''):
        angle = math.radians(degrees)
        return self.add(kind, radius * math.cos(angle), radius * math.sin(angle), stats, name, hero_class)

    def link(self, a, b):
        self.nodes[a]['links'].add(b)
        self.nodes[b]['links'].add(a)


def normalize(degrees):
    """Angles from -90 up to 270, so every class start is in range."""
    return (degrees + 90) % 360 - 90


def region(degrees):
    """The class whose region holds the angle, or 'A|B' on the border between two classes."""
    for i, (name, start) in enumerate(CLASSES):
        offset = normalize(degrees - start + 90) - 90
        if abs(offset) < 30:
            return name
        if offset == 30:
            return f'{name}|{CLASSES[(i + 1) % len(CLASSES)][0]}'
    raise ValueError(degrees)


class Stats:
    """Picks small node stats, cycling through each region's themes so neighbours differ."""

    def __init__(self):
        self.counters = {}

    def attributes(self, degrees):
        where = region(degrees)
        return BORDER_ATTRIBUTES[where] if '|' in where else ATTRIBUTES[where]

    def theme(self, degrees):
        where = region(degrees)
        count = self.counters.get(where, 0)
        self.counters[where] = count + 1
        if '|' in where:
            classes = where.split('|')
            themes = THEMES[classes[count % 2]]
            return [themes[(count // 2) % len(themes)]]
        themes = THEMES[where]
        return [themes[count % len(themes)]]


def build():
    tree = Tree()
    stats = Stats()

    starts = {}
    for name, degrees in CLASSES:
        starts[name] = tree.add_polar('Start', START_RADIUS, degrees, [], name, name)

    rings = []
    for ring_index, radius in enumerate(RINGS):
        ring = {}
        for k in range(36):
            degrees = -90 + 10 * k
            if ring_index == 0:
                node_stats = stats.attributes(degrees)
            elif ring_index == 1:
                node_stats = stats.attributes(degrees) if k % 2 == 0 else stats.theme(degrees)
            else:
                node_stats = stats.attributes(degrees) if k % 3 == 0 else stats.theme(degrees)
            ring[normalize(degrees)] = tree.add_polar('Small', radius, degrees, node_stats)
        keys = sorted(ring)
        for a, b in zip(keys, keys[1:] + keys[:1]):
            tree.link(ring[a], ring[b])
        rings.append(ring)

    for name, degrees in CLASSES:
        for offset in (-10, 0, 10):
            tree.link(starts[name], rings[0][normalize(degrees + offset)])

    class_notables = {name: list(notables) for name, notables in CLASS_NOTABLES.items()}
    class_starts = {normalize(degrees): name for name, degrees in CLASSES}

    def notable(name):
        title, node_stats = class_notables[name].pop(0)
        return title, node_stats

    # Spokes join the rings every 20 degrees; the middle of each class's own spoke is a notable.
    spoke_notables = {}
    for j in range(18):
        degrees = normalize(-90 + 20 * j)
        if degrees in class_starts:
            title, node_stats = notable(class_starts[degrees])
            spoke_notables[degrees] = (title, node_stats)
    for j in range(18):
        degrees = normalize(-90 + 20 * j)
        a = tree.add_polar('Small', 155, degrees, stats.attributes(degrees))
        if degrees in spoke_notables:
            title, node_stats = spoke_notables[degrees]
            b = tree.add_polar('Notable', 180, degrees, node_stats, title)
        else:
            b = tree.add_polar('Small', 180, degrees, stats.theme(degrees))
        tree.link(rings[0][degrees], a)
        tree.link(a, b)
        tree.link(b, rings[1][degrees])
        c = tree.add_polar('Small', 233, degrees, stats.theme(degrees))
        d = tree.add_polar('Small', 262, degrees, stats.theme(degrees))
        tree.link(rings[1][degrees], c)
        tree.link(c, d)
        tree.link(d, rings[2][degrees])

    # Clusters between the spokes: a small node, a fork of two, then a notable at the dead end.
    def cluster_notable(degrees, outer):
        where = region(degrees)
        if '|' in where:
            return HYBRID_NOTABLES[where][1 if outer else 0]
        return notable(where)

    gaps = [normalize(-80 + 20 * j) for j in range(18)]
    for outer, (ring, radii, spread) in enumerate(((rings[0], (150, 168, 185), 5), (rings[1], (228, 248, 268), 3.5))):
        for degrees in gaps:
            title, node_stats = cluster_notable(degrees, outer)
            a = tree.add_polar('Small', radii[0], degrees, stats.theme(degrees))
            b = tree.add_polar('Small', radii[1], degrees - spread, stats.theme(degrees))
            c = tree.add_polar('Small', radii[1], degrees + spread, stats.theme(degrees))
            n = tree.add_polar('Notable', radii[2], degrees, node_stats, title)
            tree.link(ring[degrees], a)
            tree.link(a, b)
            tree.link(a, c)
            tree.link(b, n)
            tree.link(c, n)

    # Past the outer ring: each class's last notable straight out, and its two shrines either side.
    for name, degrees in CLASSES:
        degrees = normalize(degrees)
        small = tree.add_polar('Small', 318, degrees, stats.theme(degrees))
        title, node_stats = notable(name)
        end = tree.add_polar('Notable', 345, degrees, node_stats, title)
        tree.link(rings[2][degrees], small)
        tree.link(small, end)
        for (title, node_stats), offset in zip(SHRINES[name], (-20, 20)):
            side = normalize(degrees + offset)
            small = tree.add_polar('Small', 318, side, stats.theme(side))
            shrine = tree.add_polar('Shrine', 348, side, node_stats, title)
            tree.link(rings[2][side], small)
            tree.link(small, shrine)

    for name, notables in class_notables.items():
        assert not notables, f'{name} has unused notables: {notables}'

    # The sanctum in the middle holds the ascendancies of the player's class, side by side.
    for name, _ in CLASSES:
        for column, (title, root_stats, ascendancy_notables) in zip(ASCENDANCY_COLUMNS, ASCENDANCIES[name]):
            ids = []
            for index, (dx, y) in enumerate(ASCENDANCY_LAYOUT):
                if index == 0:
                    ids.append(tree.add('Ascendancy', column + dx, y, root_stats, title, name))
                else:
                    node_title, node_stats = ascendancy_notables[index - 1]
                    ids.append(tree.add('AscendancyNotable', column + dx, y, node_stats, node_title, name))
            for a, b in ASCENDANCY_LINKS:
                tree.link(ids[a], ids[b])
    return tree


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
    closest = min(
        (math.hypot(nodes[a]['x'] - nodes[b]['x'], nodes[a]['y'] - nodes[b]['y']), a, b)
        for i, a in enumerate(main) for b in main[i + 1:])
    names = [n['name'] for n in nodes if n['name'] and n['kind'] != 'Start']
    duplicates = {name for name in names if names.count(name) > 1}
    assert not duplicates, f'duplicate names: {duplicates}'
    return closest


def write(tree, path):
    with open(path, 'w', newline='') as out:
        out.write('id\tname\tkind\tclass\tx\ty\tstats\tlinks\r\n')
        for i, node in enumerate(tree.nodes):
            node_stats = ','.join(f'{stat}={value}' for stat, value in node['stats'])
            links = ','.join(str(link) for link in sorted(node['links']))
            out.write(f'{i}\t{node["name"]}\t{node["kind"]}\t{node["class"]}\t{node["x"]}\t{node["y"]}\t{node_stats}\t{links}\r\n')


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(root, 'mods', 'poe2', 'txtdata', 'poe2', 'passive_tree.tsv')
    tree = build()
    distance, a, b = check(tree)
    write(tree, path)
    kinds = {}
    for node in tree.nodes:
        kinds[node['kind']] = kinds.get(node['kind'], 0) + 1
    print(f'{len(tree.nodes)} nodes {kinds}; closest main nodes {a} and {b} are {distance:.1f} apart')


if __name__ == '__main__':
    main()
