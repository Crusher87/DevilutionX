"""Death burst: at the killing blow, parts of a rigged, animated monster fly off.

Takes a rigged + animated .glb (e.g. a UniMate death move), and at a chosen frame
cuts chosen body parts (helmet/head, sword arm, ...) off the animated body and
throws them with a simple physics sim (gravity, spin, bounce, slide). Optional
blood chunks spray from the cut. Saves a .blend (for the sprite renderer) and a
preview strip PNG.

Run with Blender (4.x or 5.x):
  blender -b -P death_burst.py -- --input death.glb --out D:/HDTools/tests/burst/knight
Options:
  --break 0.35        killing blow, as a fraction of the clip (or a frame number if > 1)
  --parts head,arm_r  which parts fly: head, arm_r, arm_l, or bone names (piece = bone + children)
  --blood 10          number of blood chunks (0 = none)
  --seed 1            change for a different burst
  --power 1.0         how hard pieces are thrown
  --right -x          which side is the right hand at rest (-x or +x)
  --frames 16         frames in the preview strip
"""
import bpy, bmesh, sys, math, random, argparse, os
from mathutils import Vector, Euler, Matrix

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
ap = argparse.ArgumentParser()
ap.add_argument("--input", required=True)
ap.add_argument("--out", required=True)
ap.add_argument("--break", dest="brk", type=float, default=0.35)
ap.add_argument("--parts", default="head,arm_r")
ap.add_argument("--blood", type=int, default=10)
ap.add_argument("--seed", type=int, default=1)
ap.add_argument("--power", type=float, default=1.0)
ap.add_argument("--right", default="-x")
ap.add_argument("--frames", type=int, default=16)
ap.add_argument("--no_render", action="store_true")
ap.add_argument("--engine", default="WORKBENCH", help="WORKBENCH (fast) or CYCLES (no GPU display needed)")
A = ap.parse_args(argv)
rng = random.Random(A.seed)

# ---------------------------------------------------------------- load
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=A.input)
scene = bpy.context.scene
arm = next(o for o in scene.objects if o.type == "ARMATURE")
meshes = [o for o in scene.objects if o.type == "MESH" and
          any(m.type == "ARMATURE" for m in o.modifiers)]
if not meshes:
    sys.exit("no skinned mesh found")

act = arm.animation_data.action if arm.animation_data else None
if act:
    f0, f1 = (int(round(x)) for x in act.frame_range)
else:
    f0, f1 = scene.frame_start, scene.frame_end
scene.frame_start, scene.frame_end = f0, f1
fps = scene.render.fps / scene.render.fps_base
brk = int(A.brk) if A.brk > 1 else int(round(f0 + A.brk * (f1 - f0)))
brk = max(f0 + 1, min(f1 - 2, brk))
print(f"clip {f0}-{f1} at {fps:.0f} fps, killing blow at frame {brk}")

# ---------------------------------------------------------------- find parts
bones = arm.data.bones
def wpos(b):  # rest head in world
    return arm.matrix_world @ b.head_local

def chain(b):
    out = [b.name]
    for c in b.children:
        out += chain(c)
    return out

root = next(b for b in bones if b.parent is None)
# spine: from root, keep following the child that goes most upward
spine = [root]
while True:
    kids = [c for c in spine[-1].children]
    if not kids:
        break
    up = max(kids, key=lambda c: wpos(c).z)
    if wpos(up).z <= wpos(spine[-1]).z + 1e-4:
        break
    spine.append(up)
head = spine[-1]
if len(spine) >= 2 and len(chain(spine[-2])) <= 3:  # neck->head
    head = spine[-1]
side = -1 if A.right.startswith("-") else 1
# arms: branches off the upper spine that go sideways
arm_roots = []
for s in spine[1:]:
    for c in s.children:
        if c in spine:
            continue
        if abs(wpos(c).x - wpos(s).x) > 1e-3 or any(abs(wpos(bones[n]).x - wpos(s).x) > 0.15 for n in chain(c)):
            # skip legs (they go down from low spine)
            if wpos(c).z > wpos(spine[0]).z + 0.05:
                arm_roots.append(c)
def arm_tip_x(c):
    return sum(wpos(bones[n]).x for n in chain(c)) / len(chain(c))
arm_r = min(arm_roots, key=lambda c: side * -arm_tip_x(c)) if arm_roots else None
arm_l = max(arm_roots, key=lambda c: side * -arm_tip_x(c)) if arm_roots else None
named = {"head": head, "arm_r": arm_r, "arm_l": arm_l}
pieces = []
for p in A.parts.split(","):
    p = p.strip()
    if not p:
        continue
    b = named.get(p) or bones.get(p)
    if b is None:
        print(f"WARNING: part '{p}' not found, skipped")
        continue
    pieces.append((p, set(chain(b))))
    print(f"part {p}: {b.name} + {len(chain(b)) - 1} child bones")

# ---------------------------------------------------------------- cut
def evaluated_copy(ob, frame, name):
    scene.frame_set(frame)
    dg = bpy.context.evaluated_depsgraph_get()
    me = bpy.data.meshes.new_from_object(ob.evaluated_get(dg), depsgraph=dg)
    me.transform(ob.matrix_world)
    o = bpy.data.objects.new(name, me)
    scene.collection.objects.link(o)
    return o

def dominant(ob):
    names = {g.index: g.name for g in ob.vertex_groups}
    dom = []
    for v in ob.data.vertices:
        best, bw = None, 0.0
        for g in v.groups:
            if g.weight > bw:
                best, bw = names.get(g.group), g.weight
        dom.append(best)
    return dom

flyers = []  # (object, centre at break, velocity at break)
cut_points = []
for ob in meshes:
    dom = dominant(ob)
    gone = ob.vertex_groups.new(name="burst_gone")
    for pname, bset in pieces:
        idx = [i for i, d in enumerate(dom) if d in bset]
        if not idx:
            continue
        gone.add(idx, 1.0, "REPLACE")
        sel = set(idx)
        # piece geometry, posed at break and one frame before (for its velocity)
        prev = evaluated_copy(ob, brk - 1, "tmp_prev")
        piece = evaluated_copy(ob, brk, f"burst_{pname}_{ob.name}")
        for o in (prev, piece):
            bm = bmesh.new(); bm.from_mesh(o.data)
            bm.verts.ensure_lookup_table()
            bmesh.ops.delete(bm, geom=[v for v in bm.verts if v.index not in sel], context="VERTS")
            bm.to_mesh(o.data); bm.free()
        if len(piece.data.vertices) == 0:
            bpy.data.objects.remove(prev); bpy.data.objects.remove(piece); continue
        c_now = sum((v.co for v in piece.data.vertices), Vector()) / len(piece.data.vertices)
        c_prev = sum((v.co for v in prev.data.vertices), Vector()) / max(1, len(prev.data.vertices))
        bpy.data.objects.remove(prev)
        piece.data.transform(Matrix.Translation(-c_now))
        piece.location = c_now
        # cut point = piece vertices nearest the body (for blood)
        flyers.append((piece, c_now, (c_now - c_prev) * fps))
        cut_points.append(c_now)
    # hide the cut parts on the body from the killing blow on
    mk = ob.modifiers.new("burst_mask", "MASK")
    mk.vertex_group = "burst_gone"; mk.invert_vertex_group = True
    for f, on in ((brk - 1, False), (brk, True)):
        mk.show_viewport = mk.show_render = on
        mk.keyframe_insert("show_viewport", frame=f)
        mk.keyframe_insert("show_render", frame=f)
    for fc in (ob.animation_data.action.fcurves if hasattr(ob.animation_data.action, "fcurves") else []):
        for k in fc.keyframe_points:
            k.interpolation = "CONSTANT"

# floor = lowest point of the body over the clip start
scene.frame_set(f0)
dg = bpy.context.evaluated_depsgraph_get()
floor = min((ob.matrix_world @ v.co).z for ob in meshes
            for v in ob.evaluated_get(dg).to_mesh().vertices)
body_c = sum((ob.matrix_world.translation for ob in meshes), Vector()) / len(meshes)
height = max((ob.matrix_world @ v.co).z for ob in meshes for v in ob.evaluated_get(dg).to_mesh().vertices) - floor
print(f"floor z {floor:.3f}, height {height:.2f}")

# ---------------------------------------------------------------- physics
G = -9.81 * height / 1.9  # scale gravity to the monster's size (1.9 m human)
def throw(ob, c0, v0, extra, spin, rest_r):
    dt = 1.0 / fps
    p, v = c0.copy(), v0 * 0.5 + extra
    rot = Vector((0, 0, 0)); w = spin.copy()
    resting = False
    ob.keyframe_insert("location", frame=brk); ob.keyframe_insert("rotation_euler", frame=brk)
    for f in range(brk + 1, f1 + 1):
        if not resting:
            v.z += G * dt
            p += v * dt
            rot += w * dt
            if p.z - rest_r < floor:
                p.z = floor + rest_r
                if abs(v.z) < 0.6 * height / 1.9:
                    v.z = 0; v.xy *= 0.5; w *= 0.4
                    if v.length < 0.15:
                        resting = True
                else:
                    v.z = -v.z * 0.3; v.xy *= 0.6; w *= 0.6
        ob.location = p; ob.rotation_euler = Euler(rot)
        ob.keyframe_insert("location", frame=f); ob.keyframe_insert("rotation_euler", frame=f)
    ob.location = c0; ob.rotation_euler = Euler((0, 0, 0))
    ob.keyframe_insert("location", frame=brk - 1)

for ob, c0, vel in flyers:
    out = (c0 - body_c); out.z = 0
    out = out.normalized() if out.length > 1e-4 else Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), 0)).normalized()
    s = A.power * height / 1.9
    extra = out * rng.uniform(0.8, 1.6) * s + Vector((0, 0, rng.uniform(1.6, 2.6) * s))
    spin = Vector([rng.uniform(-12, 12) for _ in range(3)])
    rest_r = max(0.05, min(d for d in ob.dimensions if d > 0) * 0.5)
    # hidden until the blow
    ob.hide_render = ob.hide_viewport = True
    ob.keyframe_insert("hide_render", frame=brk - 1); ob.keyframe_insert("hide_viewport", frame=brk - 1)
    ob.hide_render = ob.hide_viewport = False
    ob.keyframe_insert("hide_render", frame=brk); ob.keyframe_insert("hide_viewport", frame=brk)
    throw(ob, c0, vel, extra, spin, rest_r)

# blood chunks from each cut
if A.blood and cut_points:
    mat = bpy.data.materials.new("burst_blood")
    mat.diffuse_color = (0.25, 0.0, 0.0, 1.0)
    try:
        mat.use_nodes = True
        bsdf = mat.node_tree.nodes.get("Principled BSDF")
        bsdf.inputs["Base Color"].default_value = (0.25, 0.0, 0.0, 1.0)
        bsdf.inputs["Roughness"].default_value = 0.3
    except Exception:
        pass
    for i in range(A.blood):
        c0 = cut_points[i % len(cut_points)]
        r = height * rng.uniform(0.008, 0.02)
        bm = bmesh.new(); bmesh.ops.create_icosphere(bm, subdivisions=1, radius=r)
        me = bpy.data.meshes.new(f"blood{i}"); bm.to_mesh(me); bm.free()
        me.materials.append(mat)
        ob = bpy.data.objects.new(f"burst_blood{i}", me); scene.collection.objects.link(ob)
        d = Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(0.3, 1.5))).normalized()
        s = A.power * height / 1.9
        ob.hide_render = ob.hide_viewport = True
        ob.keyframe_insert("hide_render", frame=brk - 1); ob.keyframe_insert("hide_viewport", frame=brk - 1)
        ob.hide_render = ob.hide_viewport = False
        ob.keyframe_insert("hide_render", frame=brk); ob.keyframe_insert("hide_viewport", frame=brk)
        throw(ob, c0.copy(), Vector(), d * rng.uniform(1.0, 2.6) * s, Vector(), r)

# ---------------------------------------------------------------- save + preview
os.makedirs(os.path.dirname(os.path.abspath(A.out)) or ".", exist_ok=True)
bpy.ops.wm.save_as_mainfile(filepath=os.path.abspath(A.out) + ".blend")
print("saved", A.out + ".blend")

if not A.no_render:
    cam_d = bpy.data.cameras.new("cam"); cam_d.type = "ORTHO"; cam_d.ortho_scale = height * 2.2
    cam = bpy.data.objects.new("cam", cam_d); scene.collection.objects.link(cam)
    tgt = Vector((body_c.x, body_c.y, floor + height * 0.4))
    off = Vector((1, -1, 0)).normalized() * 10
    off.z = 10 * math.tan(math.radians(30))
    cam.location = tgt + off
    cam.rotation_euler = (tgt - cam.location).to_track_quat("-Z", "Y").to_euler()
    scene.camera = cam
    sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN")); scene.collection.objects.link(sun)
    sun.rotation_euler = (math.radians(50), 0, math.radians(30))
    if A.engine.upper() == "CYCLES":
        scene.render.engine = "CYCLES"; scene.cycles.samples = 16; scene.cycles.device = "CPU"
        sun.data.energy = 4
        world_c = (0.35, 0.35, 0.4)
    else:
        scene.render.engine = "BLENDER_WORKBENCH"
        scene.display.shading.light = "STUDIO"; scene.display.shading.color_type = "MATERIAL"
        world_c = None
    scene.render.resolution_x = scene.render.resolution_y = 256
    scene.render.film_transparent = False
    world = bpy.data.worlds.new("w"); scene.world = world
    if world_c:
        world.use_nodes = True
        world.node_tree.nodes["Background"].inputs[0].default_value = (*world_c, 1)
    frames = [int(round(f0 + i * (f1 - f0) / (A.frames - 1))) for i in range(A.frames)]
    if brk not in frames:
        frames[min(range(len(frames)), key=lambda i: abs(frames[i] - brk))] = brk
    tiles = []
    for f in frames:
        scene.frame_set(f)
        p = os.path.abspath(A.out) + f"_f{f:03d}.png"
        scene.render.filepath = p
        bpy.ops.render.render(write_still=True)
        tiles.append(p)
    # strip: 8 per row
    cols = 8; rows = (len(tiles) + cols - 1) // cols
    W = H = 256
    strip = bpy.data.images.new("strip", cols * W, rows * H, alpha=False)
    px = [0.12] * (cols * W * rows * H * 4)
    for i, t in enumerate(tiles):
        im = bpy.data.images.load(t); src = list(im.pixels)
        cx, cy = (i % cols) * W, (rows - 1 - i // cols) * H
        for y in range(H):
            a = ((cy + y) * cols * W + cx) * 4
            px[a:a + W * 4] = src[y * W * 4:(y + 1) * W * 4]
        bpy.data.images.remove(im); os.remove(t)
    strip.pixels = px
    strip.filepath_raw = os.path.abspath(A.out) + "_strip.png"; strip.file_format = "PNG"; strip.save()
    print("preview", strip.filepath_raw, "frames", frames)
