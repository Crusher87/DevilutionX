#!/usr/bin/env python3
"""Pictures for the generated passive tree (stage 3): the whole-tree layout in the style of design-layout.png, an
in-game preview at zoom 1 (0.28 px per tree unit, lattice links 2 px YELLOW+10, cluster links 1 px GRAY+11, no area
blobs) drawn from the TSV rows as the loader reads them, and the lattice overlay of check 8d."""
import math
import os

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt  # noqa: E402
from matplotlib.patches import Circle  # noqa: E402
import numpy as np  # noqa: E402

import poe2_passive_tree as G  # noqa: E402
import poe2_tree_content as C  # noqa: E402

BG = '#0b0908'
COLOURS = {'Warrior': '#8c8f96', 'Sorcerer': '#4f6fb3', 'Rogue': '#a8443f', 'Monk': '#c27a2c',
           'Zakarum Cloister': '#c2a45a', 'Horadric Archive': '#6f8fc8', 'Sightless Eye': '#a8708a',
           'Khanduras Muster': '#9a8458'}
KSTYLE = {  # kind: (size, face, edge)
    'S': (60, '#f3e2b8', '#b9a66a'), 's': (7, '#8d8677', 'none'), 't': (8, '#b7a77f', 'none'),
    'A': (12, '#e8d9a8', '#7b6a3f'), 'N': (20, '#9a8a62', '#d9c48a'), 'J': (26, '#1a1714', '#5fb3c9'),
    'K': (30, '#3a1d4a', '#c49be0'), 'cap': (26, '#4a4232', '#8d8060'), 'asc': (16, '#ffe1a0', '#ffb347'),
    'prof': (20, '#5a1d16', '#a8372a'),
}
TRAVEL_COL = {'Life': '#b0584c', 'Res': '#7f9a74', 'Mana': '#5f78b0'}
PROFANE_COL = '#a8372a'

# Game palette entries the panel uses (Source/panels/passive_tree_panel.cpp), read from the game palette.
PAL = {'Y10': (0x5b, 0x51, 0x34), 'G11': (0x3d, 0x3d, 0x3d), 'R10': (0x7c, 0x19, 0x19), 'Y9': (0x69, 0x60, 0x3f),
       'O9': (0x9b, 0x3b, 0x18), 'G14': (0x11, 0x11, 0x11), 'B4': (0x87, 0x8d, 0xac), 'BE6': (0xa5, 0x5a, 0x5a),
       'G4': (0xa3, 0xa3, 0xa3), 'Y6': (0x98, 0x8b, 0x5d), 'R11': (0x66, 0x15, 0x15), 'R6': (0xc7, 0x38, 0x38),
       'R15': (0x0c, 0x05, 0x05)}
# Unreachable node fill per area ramp (ramp + 10).
AREA_FILL = {'Warrior': (76, 76, 76), 'Sorcerer': (57, 65, 95), 'Rogue': (124, 25, 25), 'Monk': (133, 50, 19)}
AREA_FILL_BORDER = (104, 49, 49)   # BEIGE+10


def caps_of(T):
    return {i for c in G.ORDER for i in T.key[c]['cap']}


# ---------------------------------------------------------------- whole tree, design-layout style
def draw_area_blobs(ax, T, alpha=0.035, size=2600):
    for a, col in COLOURS.items():
        pts = [T.pos[i] for i in range(len(T.pos)) if T.area[i] == a]
        if pts:
            xs, ys = zip(*pts)
            ax.scatter(xs, ys, s=size, c=col, alpha=alpha, edgecolors='none', zorder=0)


def draw_sanctum(ax, chosen):
    ax.add_patch(Circle((0, 0), G.SANCTUM, fc='#2a2318', ec='#b9a66a', lw=1.5, zorder=1))
    ax.add_patch(Circle((0, 0), G.SANCTUM, fc='#000000', ec='none', alpha=0.62, zorder=1.5))
    pts, links, _ = G.island_geometry(chosen)
    for i, j, sr in links:
        xs, ys = zip(*G.arc_poly(pts[i], pts[j], sr * math.dist(pts[i], pts[j]), 12))
        ax.plot(xs, ys, color='#ffcf7a', lw=1.8, zorder=6, solid_capstyle='round')
    for k, p in enumerate(pts):
        sz, fc, ec = KSTYLE['asc']
        ax.scatter([p[0]], [p[1]], s=sz * (5 if k == 0 else 3.4), c=fc if k else '#fff3cf', edgecolors='#0b0908',
                   linewidths=2.2, zorder=7)


def draw_tree(ax, T, scale=1.0):
    caps = caps_of(T)
    for e in T.edges:
        a, b, _, st = e
        xs, ys = zip(*T.poly(e, 12))
        if 'profane' in (T.tag[a], T.tag[b]) and st != 'lat':
            ax.plot(xs, ys, color=PROFANE_COL, lw=3.4 * scale, ls=(0, (2.4, 1.2)), zorder=3)
            ax.plot(xs, ys, color='#2a0d0a', lw=0.6 * scale, ls=(0, (1, 4)), zorder=3.1)   # the crack
        elif G.is_lattice(T, a) and G.is_lattice(T, b):
            ax.plot(xs, ys, color='#d6b35e', lw=2.6 * scale, alpha=0.95, zorder=2, solid_capstyle='round')
        else:
            ax.plot(xs, ys, color='#8f8a7c', lw=0.9 * scale, alpha=0.85, zorder=2, solid_capstyle='round')
    for i, p in enumerate(T.pos):
        k = T.kind[i]
        if i in caps:
            sz, fc, ec = KSTYLE['cap']
            ax.scatter([p[0]], [p[1]], s=sz * 4 * scale, marker='h', c=fc, edgecolors=ec, linewidths=1.6 * scale, zorder=5)
            continue
        if T.tag[i] == 'profane' and k == 'N':
            sz, fc, ec = KSTYLE['prof']
            ax.scatter([p[0]], [p[1]], s=sz * 3 * scale, c=fc, edgecolors=ec, linewidths=1.6 * scale, zorder=5)
            continue
        sz, fc, ec = KSTYLE[k]
        sz *= scale
        if k == 'K':
            ax.scatter([p[0]], [p[1]], s=sz * 4, marker='D', c=fc, edgecolors=ec, linewidths=1.4 * scale, zorder=5)
        elif k == 'J':
            ax.scatter([p[0]], [p[1]], s=sz * 4, c=fc, edgecolors=ec, linewidths=2.4 * scale, zorder=5)
        elif k == 'S':
            ax.scatter([p[0]], [p[1]], s=sz * 4, c=fc, edgecolors=ec, linewidths=1.5, zorder=5)
        elif k in 'NA':
            ax.scatter([p[0]], [p[1]], s=sz * 3, c=fc, edgecolors=ec, linewidths=1.0 * scale, zorder=4)
        elif k == 't':
            ax.scatter([p[0]], [p[1]], s=sz * 2.2, c=TRAVEL_COL.get(T.stat[i], fc), edgecolors='none', zorder=3)
        else:
            ax.scatter([p[0]], [p[1]], s=sz * 2, c=fc, edgecolors='none', zorder=3)


def legend(ax, x, y, size=9, step=46):
    items = [('S', 'class start'), ('s', 'small'), ('t', 'travel small (life / res / mana)'), ('A', '+5 any attribute'),
             ('N', 'notable (dimmer)'), ('cap', 'capstone notable (dim frame)'), ('prof', 'Profane notable'),
             ('J', 'jewel socket'), ('K', 'shrine (keystone)'), ('asc', 'ascendancy node (bright)')]
    for n, (k, text) in enumerate(items):
        sz, fc, ec = KSTYLE[k]
        mk = {'K': 'D', 'cap': 'h'}.get(k, 'o')
        ax.scatter([x], [y - n * step], s=min(sz, 22) * 4, marker=mk, c=fc, edgecolors=ec, linewidths=1.5, zorder=9)
        ax.text(x + 32, y - n * step, text, color='#ddd', fontsize=size, va='center', zorder=9)
    yy = y - len(items) * step
    ax.plot([x - 18, x + 18], [yy, yy], color='#d6b35e', lw=2.6, zorder=9)
    ax.text(x + 32, yy, 'lattice link (straight, bends only at nodes)', color='#ddd', fontsize=size, va='center')
    yy -= step
    ax.plot([x - 18, x + 18], [yy, yy], color=PROFANE_COL, lw=2.2, ls=(0, (3, 1.6)), zorder=9)
    ax.text(x + 32, yy, 'Profane Path (cracked, dull red)', color='#ddd', fontsize=size, va='center')


def label_areas(ax):
    for c, a in G.CLASSES.items():
        p = G.pol(G.R + (95 if a in (90, 270) else 70), a)
        ha = {0: 'left', 180: 'right'}.get(a, 'center')
        ax.text(p[0], p[1], c.upper() + '\n' + C.AREA_NAMES[c], color=COLOURS[c], ha=ha, va='center', fontsize=12,
                weight='bold')
    for b, v in G.BORDERS.items():
        p = G.pol(G.R + 95, v[0])
        ax.text(p[0], p[1], b, color=COLOURS[b], ha='center', va='center', fontsize=11.5, weight='bold')


def draw_layout(T, path):
    fig, ax = plt.subplots(figsize=(17, 17), dpi=110)
    fig.patch.set_facecolor(BG)
    ax.set_facecolor(BG)
    draw_area_blobs(ax, T)
    draw_sanctum(ax, next(n for n in G.ISLANDS if G.ISLANDS[n][0] == 'Sorcerer'))
    draw_tree(ax, T)
    label_areas(ax)
    legend(ax, -1560, -700)
    n_main = sum(1 for k in T.kind if k != 'S')
    n_not = sum(1 for k in T.kind if k == 'N')
    ax.text(-1560, 1640, 'Passive tree rework, Stage 3 generated tree (tools/poe2_passive_tree.py, seed %d)' % G.SEED,
            color='#f0e2b8', fontsize=16, weight='bold')
    ax.text(-1560, 1590,
            'Lattice = thick gold, straight links that bend only at nodes. Clusters = thin lines, placed in matched groups '
            'of four by point cost.\nSoft colour = the area each node belongs to (cheapest start; borders where two '
            'classes tie). %d main nodes, %d notables.\nCentre: the dimmed sanctum and one chosen ascendancy island.'
            % (n_main, n_not), color='#cfc3a3', fontsize=10, va='top')
    ax.set_xlim(-1580, 1580)
    ax.set_ylim(-1420, 1660)
    ax.set_aspect('equal')
    ax.axis('off')
    fig.savefig(path, facecolor=fig.get_facecolor(), bbox_inches='tight')
    plt.close(fig)


# ---------------------------------------------------------------- in-game preview at zoom 1, from the TSV rows
class Canvas:
    def __init__(self, extent, scale):
        self.scale = scale
        self.n = int(math.ceil(2 * extent * scale)) + 1
        self.half = self.n / 2
        self.img = np.zeros((self.n, self.n, 3), np.uint8)

    def pt(self, x, y):   # tree units (screen frame, y down) to pixels
        return x * self.scale + self.half, y * self.scale + self.half

    def disc(self, c, r, col):
        cx, cy = round(c[0]), round(c[1])
        r = int(r)
        for dy in range(-r, r + 1):
            for dx in range(-r, r + 1):
                if dx * dx + dy * dy <= r * r + r and 0 <= cy + dy < self.n and 0 <= cx + dx < self.n:
                    self.img[cy + dy, cx + dx] = col

    def diamond(self, c, r, col):
        cx, cy = round(c[0]), round(c[1])
        for dy in range(-r, r + 1):
            for dx in range(-r, r + 1):
                if abs(dx) + abs(dy) <= r and 0 <= cy + dy < self.n and 0 <= cx + dx < self.n:
                    self.img[cy + dy, cx + dx] = col

    def line(self, p, q, col, th):
        p = (round(p[0]), round(p[1]))
        q = (round(q[0]), round(q[1]))
        steps = max(abs(q[0] - p[0]), abs(q[1] - p[1]), 1)
        horiz = abs(q[0] - p[0]) >= abs(q[1] - p[1])
        for k in range(steps + 1):
            x = round(p[0] + (q[0] - p[0]) * k / steps)
            y = round(p[1] + (q[1] - p[1]) * k / steps)
            for t in range(th):   # thickness grows across the line, as the engine's DrawLine does
                xx, yy = (x, y + t) if horiz else (x + t, y)
                if 0 <= yy < self.n and 0 <= xx < self.n:
                    self.img[yy, xx] = col

    def ring(self, c, r, col):
        for k in range(int(2 * math.pi * r) + 8):
            a = 2 * math.pi * k / (int(2 * math.pi * r) + 8)
            x, y = round(c[0] + r * math.cos(a)), round(c[1] + r * math.sin(a))
            if 0 <= y < self.n and 0 <= x < self.n:
                self.img[y, x] = col


def node_size(kind):
    return {'Start': 13, 'Shrine': 11, 'Ascendancy': 11, 'Notable': 10, 'JewelSocket': 9, 'AscendancyNotable': 8}.get(kind, 5)


def draw_game(T, rows, path, scale=0.28, extent=1300, chosen_class='Sorcerer', upscale=3):
    import poe2_tree_checks as K
    cv = Canvas(extent, scale)
    by_id = {r['id']: r for r in rows}
    asc_ok = set()
    for r in rows:   # only the chosen island shows (here the first island of chosen_class)
        if r['kind'] == 'Ascendancy' and r['cls'] == chosen_class:
            stack = [r['id']]
            while stack:
                x = stack.pop()
                if x in asc_ok:
                    continue
                asc_ok.add(x)
                stack += [int(y) for y in by_id[x]['links'].split(',') if y and by_id[int(y)]['kind'] == 'AscendancyNotable']
            break
    shown = {r['id'] for r in rows if r['kind'] not in ('Ascendancy', 'AscendancyNotable') or r['id'] in asc_ok}
    cv.disc(cv.pt(0, 0), G.SANCTUM * scale, PAL['G14'])
    cv.ring(cv.pt(0, 0), G.SANCTUM * scale, PAL['Y6'])
    rim = max(round(scale * 1.5), 1)
    for r in rows:
        if r['id'] not in shown:
            continue
        links = [int(x) for x in r['links'].split(',') if x]
        higher = [x for x in links if x > r['id']]
        toks = r['arcs'].split(',') if r['arcs'] else ['-'] * len(higher)
        for h, tok in zip(higher, toks):
            if h not in shown:
                continue
            o = by_id[h]
            lattice = r['orbit'] < 0 and o['orbit'] < 0
            profane = r['group'] == 'profane' and o['group'] == 'profane' or \
                (r['group'] == 'profane') != (o['group'] == 'profane') and not lattice
            col = PAL['Y10'] if lattice else (PAL['R10'] if profane else PAL['G11'])
            th = rim + 1 if lattice else rim
            pts = K.loader_arc((r['x'], r['y']), (o['x'], o['y']), tok, (r['gx'], r['gy']),
                               r['cluster_id'] == o['cluster_id'] and r['orbit'] == o['orbit'] and r['orbit'] >= 0)
            if profane and len(pts) == 3:
                pts = [(r['x'] + (o['x'] - r['x']) * k / 8, r['y'] + (o['y'] - r['y']) * k / 8) for k in range(9)]
            pp = [cv.pt(*p) for p in pts]
            for k in range(1, len(pp)):
                if not profane or k % 3 != 2:
                    cv.line(pp[k - 1], pp[k], col, th)
    for r in rows:
        if r['id'] not in shown:
            continue
        c = cv.pt(r['x'], r['y'])
        rad = max(round(node_size(r['kind']) * scale), 2)
        kind = r['kind']
        if kind == 'Start':
            fill = PAL['R6'] if r['cls'] == chosen_class else PAL['R11']
        elif kind in ('Ascendancy', 'AscendancyNotable'):
            fill = (0x59, 0x59, 0x59)
        else:
            fill = AREA_FILL.get(r['region'], AREA_FILL_BORDER)
        if r['group'] == 'capstone':
            cv.ring(c, rad + 2 * rim + 2, PAL['Y9'])
        if kind == 'Shrine':
            cv.diamond(c, rad + rim, PAL['B4'])
            cv.diamond(c, rad, fill)
        elif kind in ('Notable', 'Ascendancy', 'AscendancyNotable'):
            cv.disc(c, rad + rim, PAL['O9'])
            cv.disc(c, rad, PAL['G14'])
            cv.disc(c, max(rad - rim, 1), fill)
        elif kind == 'JewelSocket':
            cv.disc(c, rad + rim, fill)
            cv.disc(c, rad, PAL['G4'])
            cv.disc(c, max(rad - 2 * rim, 1), (0, 0, 0))
        elif kind == 'Attribute':
            cv.disc(c, rad + rim, PAL['BE6'])
            cv.disc(c, rad, fill)
        else:
            cv.disc(c, rad, fill)
    plt.imsave(path, cv.img)
    if upscale > 1:
        big = np.repeat(np.repeat(cv.img, upscale, axis=0), upscale, axis=1)
        root, ext = os.path.splitext(path)
        plt.imsave(root + '-x%d' % upscale + ext, big)


# ---------------------------------------------------------------- 8d lattice overlay
def draw_overlay(T, path):
    fig, axs = plt.subplots(1, 2, figsize=(20, 10.4), dpi=100)
    fig.patch.set_facecolor(BG)
    lat = [e for e in T.edges if G.is_lattice(T, e[0]) and G.is_lattice(T, e[1])]
    cols = ['#e8c66a', '#5fb3c9', '#c27a2c', '#b0584c']
    for ax in axs:
        ax.set_facecolor(BG)
        ax.set_aspect('equal')
        ax.axis('off')
        ax.set_xlim(-1260, 1260)
        ax.set_ylim(-1260, 1260)
        ax.add_patch(Circle((0, 0), G.R, fill=False, ec='#333', lw=0.8))
    for e in lat:
        xs, ys = zip(*T.poly(e, 2))
        axs[0].plot(xs, ys, color='#d6b35e', lw=1.6)
    for i in range(T.n_lat):
        if T.kind[i] in 'SJK' and G.is_lattice(T, i):
            p = T.pos[i]
            axs[0].scatter([p[0]], [p[1]], s=30, c={'S': '#f3e2b8', 'J': '#5fb3c9', 'K': '#c49be0'}[T.kind[i]], zorder=3)
    for k, rot in enumerate((0, 90, 180, 270)):
        ca, sa = math.cos(math.radians(rot)), math.sin(math.radians(rot))
        for e in lat:
            pts = [(x * ca - y * sa, x * sa + y * ca) for x, y in T.poly(e, 2)]
            xs, ys = zip(*pts)
            axs[1].plot(xs, ys, color=cols[k], lw=1.1, alpha=0.75)
    axs[0].set_title('Lattice only', color='#f0e2b8', fontsize=13)
    axs[1].set_title('Lattice turned 0 (gold), 90 (blue), 180 (orange), 270 (red) degrees, overlaid',
                     color='#f0e2b8', fontsize=13)
    fig.savefig(path, facecolor=fig.get_facecolor(), bbox_inches='tight')
    plt.close(fig)


def draw_all(T, rows, ids, outdir):
    draw_layout(T, os.path.join(outdir, 'tree-layout.png'))
    draw_game(T, rows, os.path.join(outdir, 'tree-ingame-zoom1.png'))
    draw_overlay(T, os.path.join(outdir, 'lattice-overlay.png'))
