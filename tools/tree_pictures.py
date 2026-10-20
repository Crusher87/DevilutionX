#!/usr/bin/env python3
"""Pictures of part of the passive tree, as SVG (no libraries needed), for questions about a hero's choices.

Each picture crops the tree round the nodes it is about and marks them so they can't be mixed up:
  held      gold dot             nodes the hero holds
  focus     magenta ring + name  the node the question is about
  path      cyan line and rings  the points spent walking to it, in order
  refunded  red cross            a node taken and then given back
  skipped   green dashed ring    a notable the hero passed by
Links are drawn straight; the game draws some of them as arcs.

Usage (a quick look at a few nodes):
  python3 tools/tree_pictures.py passive_tree.tsv --focus 1297 --path 1302,1301,1300 --out picture.svg
"""
import argparse
import html
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tree_audit  # noqa: E402

COLOURS = {
    'bg': '#14110e', 'link': '#4a4339', 'node': '#6d6455', 'notable': '#9a8a62', 'text': '#d9cfbd', 'dim': '#8a8070',
    'held': '#e0b84a', 'focus': '#ff4fd8', 'path': '#3fd0e8', 'refunded': '#ff4a3d', 'skipped': '#4fe07a', 'profane': '#a8372a',
}
LEGEND = [('held', 'You hold it'), ('focus', 'The node this question is about'), ('path', 'Points spent getting there, numbered in the order taken'),
          ('refunded', 'Taken, then refunded'), ('skipped', 'A notable you passed by')]
BIG_KINDS = {'Notable', 'Shrine', 'JewelSocket', 'Start', 'Ascendancy', 'AscendancyNotable'}


def radius(node):
    return {'Start': 16, 'Shrine': 13, 'Notable': 11, 'JewelSocket': 11, 'Ascendancy': 12, 'AscendancyNotable': 9, 'Attribute': 7}.get(node['kind'], 5)


def svg(nodes, held=(), focus=(), path=(), refunded=(), skipped=(), title='', pad=90, size=520):
    held, focus, refunded, skipped = set(held), list(focus), set(refunded), list(skipped)
    path = [p for p in path if p in nodes]
    marked = [i for i in list(focus) + path + list(refunded) + skipped if i in nodes]
    if not marked:
        marked = [i for i in held if i in nodes] or list(nodes)
    xs = [nodes[i]['x'] for i in marked]
    ys = [nodes[i]['y'] for i in marked]
    # A square view round everything marked, never smaller than a cluster or two.
    cx, cy = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2
    half = max(max(xs) - min(xs), max(ys) - min(ys), 220) / 2 + pad
    x0, y0 = cx - half, cy - half
    k = size / (2 * half)

    def at(node):
        return (node['x'] - x0) * k, (node['y'] - y0) * k

    def inside(node, margin=30):
        x, y = at(node)
        return -margin <= x <= size + margin and -margin <= y <= size + margin

    shown = {i: n for i, n in nodes.items() if inside(n)}
    used = set()
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {size} {size + 30 + 18 * len(LEGEND)}" width="100%" style="max-width:{size}px;display:block" '
           f'role="img" aria-label="{html.escape(title)}">',
           f'<rect width="100%" height="100%" fill="{COLOURS["bg"]}"/>']
    for i, n in shown.items():
        for j in n['links']:
            if j > i and j in shown:
                (ax, ay), (bx, by) = at(n), at(shown[j])
                colour = COLOURS['profane'] if n['road'] or shown[j]['road'] else COLOURS['link']
                both = i in held and j in held
                out.append(f'<line x1="{ax:.1f}" y1="{ay:.1f}" x2="{bx:.1f}" y2="{by:.1f}" stroke="{COLOURS["held"] if both else colour}" '
                           f'stroke-width="{2.5 if both else 1.5}" opacity="{0.9 if both else 0.7}"/>')
    # The walk, step by step: each point spent joins the node it was linked from.
    walk = path + [i for i in focus if i in nodes and path]
    before = set(held) - set(walk)
    for step, i in enumerate(walk):
        joined = [j for j in nodes[i]['links'] if j in before or j in walk[:step]]
        if not joined:
            continue
        j = min(joined, key=lambda j: walk.index(j) if j in walk[:step] else -1)
        used.add('path')
        (ax, ay), (bx, by) = at(nodes[j]), at(nodes[i])
        out.append(f'<line x1="{ax:.1f}" y1="{ay:.1f}" x2="{bx:.1f}" y2="{by:.1f}" stroke="{COLOURS["path"]}" stroke-width="4" '
                   f'stroke-linecap="round" opacity="0.9"/>')
    for i, n in shown.items():
        x, y = at(n)
        r = radius(n) * max(min(k * 1.4, 1.5), 0.5)
        fill = COLOURS['held'] if i in held else (COLOURS['notable'] if n['kind'] in BIG_KINDS else COLOURS['node'])
        if i in held:
            used.add('held')
        out.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="{r:.1f}" fill="{fill}" stroke="#000" stroke-width="1"/>')
        if i in path:
            out.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="{r + 4:.1f}" fill="none" stroke="{COLOURS["path"]}" stroke-width="2"/>')
            if len(path) <= 20:
                out.append(f'<text x="{x + r + 5:.1f}" y="{y + 4:.1f}" fill="{COLOURS["path"]}" font-size="11" font-weight="bold" font-family="sans-serif" '
                           f'paint-order="stroke" stroke="{COLOURS["bg"]}" stroke-width="3">{path.index(i) + 1}</text>')
        if i in skipped:
            used.add('skipped')
            out.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="{r + 7:.1f}" fill="none" stroke="{COLOURS["skipped"]}" stroke-width="3" stroke-dasharray="5 4"/>')
        if i in refunded:
            used.add('refunded')
            d = r + 5
            out.append(f'<path d="M{x - d:.1f},{y - d:.1f}L{x + d:.1f},{y + d:.1f}M{x + d:.1f},{y - d:.1f}L{x - d:.1f},{y + d:.1f}" stroke="{COLOURS["refunded"]}" stroke-width="3.5"/>')
        if i in focus:
            used.add('focus')
            out.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="{r + 9:.1f}" fill="none" stroke="{COLOURS["focus"]}" stroke-width="4"/>')
    # Names: the marked nodes in bright text, other notables in view dimmer.
    # Other notables are named only when there are few enough in view to read.
    named = [i for i, n in shown.items() if n['name'] and n['kind'] in BIG_KINDS and inside(n, -10)]
    quiet = len(named) > 14
    for i, n in shown.items():
        if not n['name'] or n['kind'] == 'Small':
            continue
        important = i in focus or i in skipped or i in refunded
        if not important and (n['kind'] not in BIG_KINDS or quiet or not inside(n, -10)):
            continue
        x, y = at(n)
        colour = COLOURS['focus'] if i in focus else (COLOURS['skipped'] if i in skipped else (COLOURS['refunded'] if i in refunded else COLOURS['dim']))
        weight = 'bold' if important else 'normal'
        font = 14 if important else 10
        out.append(f'<text x="{x:.1f}" y="{y - radius(n) - 12:.1f}" fill="{colour}" font-size="{font}" font-weight="{weight}" text-anchor="middle" '
                   f'font-family="sans-serif" paint-order="stroke" stroke="{COLOURS["bg"]}" stroke-width="3">{html.escape(n["name"])}</text>')
    y = size + 22
    if title:
        out.append(f'<text x="10" y="{y - 4}" fill="{COLOURS["text"]}" font-size="13" font-family="sans-serif">{html.escape(title)}</text>')
    for key, text in LEGEND:
        if key not in used:
            continue
        y += 18
        dash = ' stroke-dasharray="4 3"' if key == 'skipped' else ''
        fill = COLOURS[key] if key == 'held' else 'none'
        out.append(f'<circle cx="18" cy="{y - 4}" r="6" fill="{fill}" stroke="{COLOURS[key]}" stroke-width="3"{dash}/>')
        out.append(f'<text x="32" y="{y}" fill="{COLOURS["text"]}" font-size="12" font-family="sans-serif">{html.escape(text)}</text>')
    out.append('</svg>')
    return ''.join(out)


def ids(text):
    return [int(x) for x in (text or '').split(',') if x.strip().isdigit()]


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('tree', nargs='?', default=os.path.join(os.path.dirname(here), 'mods', 'poe2', 'txtdata', 'poe2', 'passive_tree.tsv'))
    for name in ('held', 'focus', 'path', 'refunded', 'skipped'):
        parser.add_argument('--' + name, default='', help='node ids, comma separated')
    parser.add_argument('--title', default='')
    parser.add_argument('--out', default='tree_picture.svg')
    args = parser.parse_args()
    nodes = tree_audit.load(args.tree)
    with open(args.out, 'w', encoding='utf-8') as f:
        f.write(svg(nodes, ids(args.held), ids(args.focus), ids(args.path), ids(args.refunded), ids(args.skipped), args.title))
    print(f"Wrote {args.out}")


if __name__ == '__main__':
    main()
