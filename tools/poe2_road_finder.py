"""Finds the line of a Profane Road between its door and far gate on a placed tree (local tool, ported from the
roads-measure beam search, research/overnight-2026-10-18/roads-measure/beam.py).

A road is a chain of straight links through free ground, bending gently (each turn at most `turn` degrees). Its
nodes keep the generator's spacing from every node and link already placed; its links keep clear of nodes but may
pass over other links (they bridge them), crossing cleanly: at 30 degrees or more and well away from any node."""

import math

import numpy as np

GRID = 10.0
TRACE = []
STEP = (75, 130)          # road link length
CROSS_CLEAR = 26.0        # a crossing point keeps this far from every other node
CROSS_ANGLE = 30.0        # no shallow crossings
NOTE_CROSS = 46.0         # a crossing point keeps this far from a notable on the road
SMALL_CROSS = 28.0        # and this far from a small on the road


def pt_seg_dist(P, A, B):
    """distances from points P (n,2) to segments A-B (m,2): (n,m)"""
    d = B - A
    L = (d * d).sum(1)
    L[L == 0] = 1e-9
    t = np.clip(((P[:, None, :] - A[None]) * d[None]).sum(2) / L[None], 0, 1)
    C = A[None] + t[..., None] * d[None]
    return np.sqrt(((P[:, None, :] - C) ** 2).sum(2))


def seg_cross(P1, P2, A, B):
    """which of segments A-B the segment P1-P2 properly crosses; bool array, crossing points, segment directions"""
    r = P2 - P1
    s = B - A
    den = r[0] * s[:, 1] - r[1] * s[:, 0]
    with np.errstate(divide='ignore', invalid='ignore'):
        t = ((A[:, 0] - P1[0]) * s[:, 1] - (A[:, 1] - P1[1]) * s[:, 0]) / den
        u = ((A[:, 0] - P1[0]) * r[1] - (A[:, 1] - P1[1]) * r[0]) / den
    ok = (np.abs(den) > 1e-9) & (t > 0) & (t < 1) & (u > 0) & (u < 1)
    return ok, P1[None] + t[:, None] * r[None], s


def heading(p, q):
    return math.degrees(math.atan2(q[1] - p[1], q[0] - p[0]))


def tdiff(a, b):
    return abs((a - b + 180) % 360 - 180)


class Obstacles:
    """Every node and link of the tree, as arrays. segs: (p, q, a, b) pieces of every link (edge_segments)."""

    def __init__(self, pos, kinds, segs, node_gap, notable_gap, link_clear):
        self.P = np.array(pos, float)
        self.ids = np.arange(len(pos))
        self.notable = np.array([k in 'NKJS' for k in kinds])
        self.A = np.array([s[0] for s in segs], float)
        self.B = np.array([s[1] for s in segs], float)
        self.U = np.array([s[2] for s in segs])
        self.V = np.array([s[3] for s in segs])
        self.node_gap, self.notable_gap, self.link_clear = node_gap, notable_gap, link_clear

    def free_points(self, box):
        """grid points in box where a road node may sit; and whether a notable may sit there"""
        X0, X1, Y0, Y1 = box
        xs = np.arange(X0, X1, GRID)
        ys = np.arange(Y0, Y1, GRID)
        P = np.array([(x, y) for x in xs for y in ys])
        r = np.hypot(P[:, 0], P[:, 1])
        P = P[(r > 300) & (r < 1185)]
        nm = (self.P[:, 0] > X0 - 60) & (self.P[:, 0] < X1 + 60) & (self.P[:, 1] > Y0 - 60) & (self.P[:, 1] < Y1 + 60)
        N, NN = self.P[nm], self.notable[nm]
        sm = (np.maximum(self.A[:, 0], self.B[:, 0]) > X0 - 30) & (np.minimum(self.A[:, 0], self.B[:, 0]) < X1 + 30) & \
             (np.maximum(self.A[:, 1], self.B[:, 1]) > Y0 - 30) & (np.minimum(self.A[:, 1], self.B[:, 1]) < Y1 + 30)
        A, B = self.A[sm], self.B[sm]
        keep = np.ones(len(P), bool)
        dnot = np.full(len(P), 1e9)
        for k in range(0, len(P), 1000):
            Q = P[k:k + 1000]
            d = np.sqrt(((Q[:, None, :] - N[None]) ** 2).sum(2))
            keep[k:k + 1000] &= d.min(1) >= self.node_gap
            if NN.any():
                dnot[k:k + 1000] = d[:, NN].min(1)
        P, dnot = P[keep], dnot[keep]
        keep = np.ones(len(P), bool)
        for k in range(0, len(P), 400):
            keep[k:k + 400] &= pt_seg_dist(P[k:k + 400], A, B).min(1) >= self.link_clear
        return P[keep], dnot[keep] >= self.notable_gap


class SegCheck:
    """Whether a road link p-q keeps clear of nodes and crosses links cleanly (gates u and v excepted)."""

    def __init__(self, ob, u, v):
        mask = (ob.ids != u) & (ob.ids != v)
        self.nodes = ob.P[mask]
        sm = ~np.isin(ob.U, [u, v]) & ~np.isin(ob.V, [u, v])
        self.A, self.B = ob.A[sm], ob.B[sm]
        self.clear = ob.link_clear
        self.cache = {}

    def ok(self, p, q):
        """(ok, crossing points)"""
        key = (p, q)
        if key in self.cache:
            return self.cache[key]
        res = (False, [])
        P, Q = np.array(p), np.array(q)
        lo = np.minimum(P, Q) - 30
        hi = np.maximum(P, Q) + 30
        nm = (self.nodes[:, 0] > lo[0]) & (self.nodes[:, 0] < hi[0]) & (self.nodes[:, 1] > lo[1]) & (self.nodes[:, 1] < hi[1])
        N = self.nodes[nm]
        if len(N) and pt_seg_dist(N, P[None], Q[None]).min() < self.clear:
            self.cache[key] = res
            return res
        sm = (np.maximum(self.A[:, 0], self.B[:, 0]) > lo[0]) & (np.minimum(self.A[:, 0], self.B[:, 0]) < hi[0]) & \
             (np.maximum(self.A[:, 1], self.B[:, 1]) > lo[1]) & (np.minimum(self.A[:, 1], self.B[:, 1]) < hi[1])
        okc, cp, s = seg_cross(P, Q, self.A[sm], self.B[sm])
        r = Q - P
        xs = []
        for j in np.nonzero(okc)[0]:
            c = cp[j]
            if len(N) and np.sqrt(((N - c) ** 2).sum(1)).min() < CROSS_CLEAR:
                self.cache[key] = res
                return res
            if min(math.dist(c, p), math.dist(c, q)) < CROSS_CLEAR:
                self.cache[key] = res
                return res
            ang = abs(math.degrees(math.atan2(r[0] * s[j][1] - r[1] * s[j][0], r[0] * s[j][0] + r[1] * s[j][1])))
            if min(ang, 180 - ang) < CROSS_ANGLE:
                self.cache[key] = res
                return res
            xs.append(tuple(c))
        res = (True, xs)
        self.cache[key] = res
        return res


def find_road(ob, pu, pv, u, v, n, notables, turn=24.0, corridor=0.3, beam=250, first=35.0, tol=0.22):
    """n road nodes between gates u (at pu) and v (at pv); the nodes at the 0-based positions in `notables` are
    notables. Returns (points, crossings) of the straightest clean road found, or None."""
    pu, pv = np.array(pu, float), np.array(pv, float)
    L = float(np.linalg.norm(pv - pu))
    d = (pv - pu) / L
    nrm = np.array([-d[1], d[0]])
    pad = corridor * L + 40
    box = (min(pu[0], pv[0]) - pad, max(pu[0], pv[0]) + pad, min(pu[1], pv[1]) - pad, max(pu[1], pv[1]) + pad)
    FP, FN = ob.free_points(box)
    rel = FP - pu
    along, perp = rel @ d, rel @ nrm
    sel = (along > 30) & (along < L - 30) & (np.abs(perp) < corridor * L + 20)
    C, CN, Ca = FP[sel], FN[sel], along[sel]
    if not len(C):
        return None
    sc = SegCheck(ob, u, v)
    nots = set(notables)
    h0 = heading(pu, pv)
    states = [(0.0, [tuple(pu)], None, [])]
    for k in range(n):
        nxt = []
        target = L * (k + 1) / (n + 1)
        isn, wasn = k in nots, (k - 1) in nots
        for score, path, h, xs in states:
            p = np.array(path[-1])
            dd = np.sqrt(((C - p) ** 2).sum(1))
            cand = np.nonzero((dd >= STEP[0]) & (dd <= STEP[1]) & (np.abs(Ca - target) < tol * L / (n + 1) + 15))[0]
            if isn:
                cand = cand[CN[cand]]
            for j in cand:
                q = tuple(C[j])
                hq = heading(p, q)
                if tdiff(hq, h0 if h is None else h) > (first if h is None else turn):
                    continue
                if any(math.dist(q, x) < ob.node_gap for x in path):
                    continue
                ok, x1 = sc.ok(tuple(p), q)
                if not ok:
                    continue
                if any(math.dist(c, q) < (NOTE_CROSS if isn else SMALL_CROSS) or
                       math.dist(c, p) < (NOTE_CROSS if wasn else SMALL_CROSS) for c in x1):
                    continue
                t = 0 if h is None else tdiff(hq, h)
                nxt.append((score + t + abs(Ca[j] - target) * 0.05, path + [q], hq, xs + x1))
        TRACE.append((k, len(nxt)))
        if not nxt:
            return None
        nxt.sort(key=lambda s: s[0])
        seen = set()
        states = []
        for s in nxt:
            key = (round(s[1][-1][0] / 20), round(s[1][-1][1] / 20))
            if key in seen:
                continue
            seen.add(key)
            states.append(s)
            if len(states) >= beam:
                break
    best = None
    why = [0, 0, 0, 0]
    for score, path, h, xs in states:
        p = path[-1]
        if not STEP[0] - 15 <= math.dist(p, pv) <= STEP[1] + 10:
            why[0] += 1
            continue
        hq = heading(p, pv)
        if tdiff(hq, h) > turn:
            why[1] += 1
            continue
        ok, x1 = sc.ok(tuple(p), tuple(pv))
        if not ok or any(math.dist(c, p) < (NOTE_CROSS if (n - 1) in nots else SMALL_CROSS) for c in x1):
            why[2 if not ok else 3] += 1
            continue
        s = score + tdiff(hq, h)
        if best is None or s < best[0]:
            best = (s, path[1:], xs + x1)
    if best is None:
        TRACE.append(('end', why))
        return None
    return [(float(x), float(y)) for x, y in best[1]], best[2]


# from strict to loose: (turn limit, corridor, first step off the straight line, along-spacing tolerance)
TRIES = [(24, 0.3, 35, 0.22), (24, 0.45, 35, 0.22), (30, 0.3, 35, 0.22), (30, 0.45, 35, 0.22),
         (30, 0.45, 60, 0.35), (36, 0.6, 60, 0.35), (36, 0.6, 80, 0.5)]


def search(ob, pu, pv, u, v, n, notables):
    """find_road from strict to loose (TRIES). Returns (points, crossings, the try used) or None."""
    for t in TRIES:
        got = find_road(ob, pu, pv, u, v, n, notables, t[0], t[1], first=t[2], tol=t[3])
        if got:
            return got[0], got[1], t
    return None
