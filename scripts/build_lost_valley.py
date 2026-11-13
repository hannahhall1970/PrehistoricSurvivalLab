"""Build /Game/Maps/L_LostValley: imports, materials, landscape, foliage, landmarks, gameplay, lighting.

Run (only under the harness build lock, no other Unreal job running):
    UnrealEditor-Cmd PrehistoricSurvivalLab.uproject -EnablePlugins=PythonScriptPlugin \
      -ExecutePythonScript=<abs>/scripts/build_lost_valley.py -ScriptErrorsAreFatal -unattended -nosplash

Pure helpers (heightfield, spline, Poisson sampling, exclusion zones) run without `unreal`:
    python3 scripts/build_lost_valley.py --selftest

Idempotent: if the map exists it is loaded and validated; if validation fails (e.g. a crashed run left the
empty map new_level saved) the map, this script's own output, is deleted and rebuilt. Imported assets are
skip-if-exists. L_TestArena is never touched.
Every failure writes Saved/PSL/lost-valley.json {status: 'failed', errors} and exits non-zero.
"""
import hashlib
import json
import math
import os
import random
import re
import sys

LOG = "PSL build_lost_valley: "
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
REPORT = os.path.join(ROOT, "Saved", "PSL", "lost-valley.json")
MAP_PATH = "/Game/Maps/L_LostValley"
SEED = 4004

# ---- geometry (cm, origin = valley center) ----
SIZE_VERTS = 253  # 4 x 4 components of 63 quads
VERT_STEP = 100.0
LAND_ORIGIN = -12600.0
CLEARING = (0.0, 0.0, 2500.0)
MEADOW = (5500.0, 3500.0, 2000.0)
PATH_POINTS = [(-8000, -6000), (-3000, -1500), (0, 0), (3500, 2000), (5500, 3500)]
PATH_FLAT_RADIUS = 250.0
PATH_BLEND_RADIUS = 650.0
PATH_EXCLUDE = 400.0
RIM_INNER, RIM_OUTER, RIM_HEIGHT = 9000.0, 12000.0, 1800.0
NOISE_AMP = 60.0
EXTENT = 12000.0  # foliage placement limit (inside landscape)
MOUND = (-2500.0, 2800.0, 300.0)  # dead-tree landmark mound: x, y, height


def height_to_u16(z_cm):
    return max(0, min(65535, int(round(z_cm / 100.0 * 128.0)) + 32768))


def u16_to_height(h):
    return (h - 32768) / 128.0 * 100.0


def smoothstep(a, b, x):
    t = max(0.0, min(1.0, (x - a) / (b - a)))
    return t * t * (3 - 2 * t)


def _lattice(ix, iy):
    h = hashlib.sha256(("%d,%d,%d" % (SEED, ix, iy)).encode()).digest()
    return h[0] / 255.0 * 2.0 - 1.0


def value_noise(x, y, cell=1500.0):
    fx, fy = x / cell, y / cell
    ix, iy = math.floor(fx), math.floor(fy)
    tx, ty = fx - ix, fy - iy
    tx, ty = tx * tx * (3 - 2 * tx), ty * ty * (3 - 2 * ty)
    a, b = _lattice(ix, iy), _lattice(ix + 1, iy)
    c, d = _lattice(ix, iy + 1), _lattice(ix + 1, iy + 1)
    return (a + (b - a) * tx) * (1 - ty) + (c + (d - c) * tx) * ty


def catmull_rom(points, per_segment=24):
    """Open Catmull-Rom through points (endpoints duplicated); returns a polyline through every point."""
    pts = [points[0]] + list(points) + [points[-1]]
    out = []
    for i in range(1, len(pts) - 2):
        p0, p1, p2, p3 = pts[i - 1], pts[i], pts[i + 1], pts[i + 2]
        for s in range(per_segment):
            t = s / per_segment
            t2, t3 = t * t, t * t * t
            out.append(tuple(
                0.5 * (2 * p1[k] + (-p0[k] + p2[k]) * t + (2 * p0[k] - 5 * p1[k] + 4 * p2[k] - p3[k]) * t2
                       + (-p0[k] + 3 * p1[k] - 3 * p2[k] + p3[k]) * t3)
                for k in (0, 1)))
    out.append(tuple(map(float, points[-1])))
    return out


def _seg_dist(px, py, a, b):
    dx, dy = b[0] - a[0], b[1] - a[1]
    l2 = dx * dx + dy * dy
    t = 0.0 if l2 == 0 else max(0.0, min(1.0, ((px - a[0]) * dx + (py - a[1]) * dy) / l2))
    return math.hypot(px - (a[0] + t * dx), py - (a[1] + t * dy))


def path_distance(x, y, poly):
    return min(_seg_dist(x, y, poly[i], poly[i + 1]) for i in range(len(poly) - 1))


def resample(poly, spacing):
    """Points along the polyline every `spacing` cm, with the unit tangent: [(x, y, tx, ty)]."""
    out, carry = [], 0.0
    for a, b in zip(poly, poly[1:]):
        seg = math.hypot(b[0] - a[0], b[1] - a[1])
        if seg == 0:
            continue
        tx, ty = (b[0] - a[0]) / seg, (b[1] - a[1]) / seg
        d = carry
        while d < seg:
            out.append((a[0] + tx * d, a[1] + ty * d, tx, ty))
            d += spacing
        carry = d - seg
    return out


def raw_height(x, y):
    """Terrain height in cm before clearing/meadow/path flattening."""
    r = math.hypot(x, y)
    rim = RIM_HEIGHT * smoothstep(RIM_INNER, RIM_OUTER, r)
    return rim + NOISE_AMP * value_noise(x, y)


def terrain_height(x, y, poly):
    z = raw_height(x, y)
    for cx, cy, rad in (CLEARING, MEADOW):
        d = math.hypot(x - cx, y - cy)
        z *= smoothstep(rad, rad + 800.0, d)  # 0 inside radius -> flat at 0
    d = path_distance(x, y, poly)
    z *= smoothstep(PATH_FLAT_RADIUS, PATH_BLEND_RADIUS, d)
    if d <= PATH_FLAT_RADIUS:
        z = max(-10.0, min(10.0, z))
    return z + MOUND[2] * (1.0 - smoothstep(200.0, 700.0, math.hypot(x - MOUND[0], y - MOUND[1])))


def build_heights(poly):
    """SIZE_VERTS^2 uint16 values, row-major (x fastest), plus the cm grid used for sampling."""
    grid = []
    for j in range(SIZE_VERTS):
        y = LAND_ORIGIN + j * VERT_STEP
        grid.append([terrain_height(LAND_ORIGIN + i * VERT_STEP, y, poly) for i in range(SIZE_VERTS)])
    heights = [height_to_u16(z) for row in grid for z in row]
    # Sample from the quantised values so instances sit on the actual landscape.
    quant = [[u16_to_height(heights[j * SIZE_VERTS + i]) for i in range(SIZE_VERTS)] for j in range(SIZE_VERTS)]
    return heights, quant


def sample_height(grid, x, y):
    fx = (x - LAND_ORIGIN) / VERT_STEP
    fy = (y - LAND_ORIGIN) / VERT_STEP
    fx = max(0.0, min(SIZE_VERTS - 1.001, fx))
    fy = max(0.0, min(SIZE_VERTS - 1.001, fy))
    i, j = int(fx), int(fy)
    tx, ty = fx - i, fy - j
    top = grid[j][i] * (1 - tx) + grid[j][i + 1] * tx
    bot = grid[j + 1][i] * (1 - tx) + grid[j + 1][i + 1] * tx
    return top * (1 - ty) + bot * ty


def excluded(x, y, poly, margin=0.0):
    """Clearing, meadow core, or within 400 cm of the path."""
    for cx, cy, rad in (CLEARING, MEADOW):
        if math.hypot(x - cx, y - cy) < rad + margin:
            return True
    return path_distance(x, y, poly) < PATH_EXCLUDE + margin


def poisson(rng, count, min_dist, accept, tries=60000):
    """Dart throwing on a hash grid; stops at `count` points or after `tries` rejected throws."""
    cell = min_dist / math.sqrt(2)
    cells, pts = {}, []
    for _ in range(tries):
        if len(pts) >= count:
            break
        x, y = rng.uniform(-EXTENT, EXTENT), rng.uniform(-EXTENT, EXTENT)
        if not accept(x, y):
            continue
        cx, cy = int(x // cell), int(y // cell)
        ok = True
        for ax in range(cx - 2, cx + 3):
            for ay in range(cy - 2, cy + 3):
                q = cells.get((ax, ay))
                if q and math.hypot(q[0] - x, q[1] - y) < min_dist:
                    ok = False
        if ok:
            cells[(cx, cy)] = (x, y)
            pts.append((x, y))
    return pts


def scatter(rng, count, accept, tries=200000):
    pts = []
    for _ in range(tries):
        if len(pts) >= count:
            break
        x, y = rng.uniform(-EXTENT, EXTENT), rng.uniform(-EXTENT, EXTENT)
        if accept(x, y):
            pts.append((x, y))
    return pts


def selftest():
    poly = catmull_rom(PATH_POINTS)
    for p in PATH_POINTS:
        assert path_distance(p[0], p[1], poly) < 1.0, p
    assert height_to_u16(0) == 32768 and abs(u16_to_height(height_to_u16(1800)) - 1800) < 1
    heights, grid = build_heights(poly)
    assert len(heights) == SIZE_VERTS ** 2 and all(0 <= h <= 65535 for h in heights)
    assert abs(sample_height(grid, 0, 0)) < 1 and abs(sample_height(grid, 5500, 3500)) < 1
    assert sample_height(grid, 11500, 0) > 1500, "rim ramp"
    assert abs(sample_height(grid, MOUND[0], MOUND[1]) - MOUND[2]) <= NOISE_AMP + 5.0, "mound"  # mound sits on noise
    for p in resample(poly, 250.0)[::5]:
        assert abs(sample_height(grid, p[0], p[1])) <= 12.0, p
    assert excluded(0, 0, poly) and excluded(5500, 3500, poly) and excluded(-3000, -1500, poly)
    assert not excluded(-6000, 6000, poly)
    rng = random.Random(SEED)
    pts = poisson(rng, 220, 700.0, lambda x, y: not excluded(x, y, poly))
    assert len(pts) >= 200, len(pts)
    assert all(not excluded(x, y, poly) for x, y in pts)
    assert all(math.hypot(a[0] - b[0], a[1] - b[1]) >= 700.0 for i, a in enumerate(pts) for b in pts[:i])
    assert pts == poisson(random.Random(SEED), 220, 700.0, lambda x, y: not excluded(x, y, poly)), "deterministic"
    print(LOG + "selftest ok (%d trees)" % len(pts))


# ---------------------------------------------------------------- Unreal side
unreal = None  # imported lazily so --selftest runs without the editor
ERRORS = []
NATURE_SRC = os.path.join(ROOT, "SourceArt", "Nature")
PREY_SRC = os.path.join(ROOT, "SourceArt", "Prey", "Parasaurolophus.fbx")
NATURE_DEST = "/Game/Environment/Nature"
TEX_DEST = NATURE_DEST + "/Textures"
MAT_DEST = "/Game/Environment/Materials"
PREY_DEST = "/Game/Characters/Prey"
PREY_CLASS = "/Script/PrehistoricSurvivalLab.PSLPreyCharacter"
GAME_MODE = "/Script/PrehistoricSurvivalLab.PSLGameMode"
PARA_CLIPS = {"Idle": "A_Para_Idle", "Walk": "A_Para_Walk", "Run": "A_Para_Run",
              "Attack": "A_Para_Attack", "Death": "A_Para_Death"}
PARA_ALL = ["SK_Parasaurolophus", "SKEL_Parasaurolophus"] + list(PARA_CLIPS.values())
COLLIDES_RE = re.compile(r"(?i)tree|pine|rock")
FOLIAGE_LABELS = ["PSL_Foliage_Trees", "PSL_Foliage_Rocks", "PSL_Foliage_Grass", "PSL_Foliage_Bushes", "PSL_PathStones"]
LIGHT_LABELS = ["PSL_Sun", "PSL_SkyAtmosphere", "PSL_SkyLight", "PSL_Fog", "PSL_PostProcess"]
PREY_LABELS = ["PSL_Prey_0%d" % i for i in (1, 2, 3)]


def write_report(status, counts=None):
    os.makedirs(os.path.dirname(REPORT), exist_ok=True)
    with open(os.path.abspath(__file__), "rb") as f:
        sha = hashlib.sha256(f.read()).hexdigest()
    with open(REPORT, "w") as f:
        json.dump({"status": status, "scriptSha256": sha, "map": MAP_PATH,
                   "counts": counts or {}, "errors": ERRORS}, f, indent=2)


def fail(message):
    ERRORS.append(message)
    if unreal is None:
        print(LOG + message, file=sys.stderr)
    else:
        unreal.log_error(LOG + message)
    write_report("failed")
    sys.exit(1)


def exists(path):
    return unreal.EditorAssetLibrary.does_asset_exist(path)


def import_task(filename, dest, options=None, name=None):
    task = unreal.AssetImportTask()
    task.filename = filename
    task.destination_path = dest
    if name:
        task.destination_name = name
    task.automated = True
    task.replace_existing = False
    task.save = True
    if options is not None:
        task.options = options
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    paths = [str(p) for p in task.imported_object_paths]
    if not paths:
        fail("import produced no assets: " + filename)
    return paths


def import_nature():
    if not os.path.isdir(NATURE_SRC):
        fail("missing " + NATURE_SRC + " (is Git LFS content pulled?)")
    fbx = sorted(f for f in os.listdir(NATURE_SRC) if f.lower().endswith(".fbx"))
    pngs = sorted(f for f in os.listdir(os.path.join(NATURE_SRC, "Textures")) if f.lower().endswith(".png"))
    if not fbx:
        fail("no FBX files in " + NATURE_SRC)
    for f in fbx:
        stem = os.path.splitext(f)[0]
        name = "SM_" + stem
        if exists(NATURE_DEST + "/" + name):
            continue
        opts = unreal.FbxImportUI()
        opts.import_mesh = True
        opts.import_as_skeletal = False
        opts.import_animations = False
        opts.import_materials = False
        opts.import_textures = False
        opts.static_mesh_import_data.set_editor_property("combine_meshes", True)
        opts.static_mesh_import_data.set_editor_property(
            "auto_generate_collision", bool(COLLIDES_RE.search(stem)))
        import_task(os.path.join(NATURE_SRC, f), NATURE_DEST, opts, name)
        if not exists(NATURE_DEST + "/" + name):
            fail("expected " + NATURE_DEST + "/" + name + " after import")
    for f in pngs:
        stem = os.path.splitext(f)[0]
        if not exists(TEX_DEST + "/" + stem):
            import_task(os.path.join(NATURE_SRC, "Textures", f), TEX_DEST, None, stem)
    return [os.path.splitext(f)[0] for f in fbx], [os.path.splitext(f)[0] for f in pngs]


def import_prey():
    present = [n for n in PARA_ALL if exists(PREY_DEST + "/" + n)]
    if len(present) == len(PARA_ALL):
        return
    if present:
        fail("partial prey set in %s: present=%s (resolve manually; nothing deleted)" % (PREY_DEST, present))
    if not os.path.isfile(PREY_SRC):
        fail("missing " + PREY_SRC + " (is Git LFS content pulled?)")
    opts = unreal.FbxImportUI()
    opts.import_mesh = True
    opts.import_as_skeletal = True
    opts.import_animations = True
    opts.import_materials = True
    opts.import_textures = False
    opts.create_physics_asset = False
    imported = import_task(PREY_SRC, PREY_DEST, opts)
    meshes, skels, anims = [], [], []
    for p in imported:
        a = unreal.load_asset(p)
        if isinstance(a, unreal.SkeletalMesh):
            meshes.append(p)
        elif isinstance(a, unreal.Skeleton):
            skels.append(p)
        elif isinstance(a, unreal.AnimSequence):
            anims.append(p)
    if len(meshes) != 1 or len(skels) != 1:
        fail("expected 1 SkeletalMesh and 1 Skeleton, got %d and %d" % (len(meshes), len(skels)))
    renames = [(meshes[0], "SK_Parasaurolophus"), (skels[0], "SKEL_Parasaurolophus")]
    for key, name in PARA_CLIPS.items():
        hits = [p for p in anims if p.rsplit("/", 1)[-1].split(".")[0].lower().endswith("_" + key.lower())]
        if len(hits) != 1:
            fail("expected exactly 1 AnimSequence ending _%s, got %s (all: %s)" % (key, hits, anims))
        renames.append((hits[0], name))
    for old, new in renames:
        if not unreal.EditorAssetLibrary.rename_asset(old, PREY_DEST + "/" + new):
            fail("rename %s -> %s failed" % (old, new))
    if not unreal.EditorAssetLibrary.save_directory(PREY_DEST, only_if_is_dirty=False, recursive=True):
        fail("failed to save " + PREY_DEST)


PALETTE = {"bark": (0.33, 0.22, 0.14), "rock": (0.42, 0.41, 0.38), "grass": (0.30, 0.48, 0.16),
           "flower": (0.85, 0.75, 0.35)}


def mel():
    return unreal.MaterialEditingLibrary


def new_material(name):
    path = MAT_DEST + "/" + name
    if exists(path):
        return unreal.load_asset(path)
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    mat = tools.create_asset(name, MAT_DEST, unreal.Material, unreal.MaterialFactoryNew())
    if mat is None:
        fail("cannot create material " + path)
    return mat


def expr(mat, cls, x, y):
    e = mel().create_material_expression(mat, cls, x, y)
    if e is None:
        fail("cannot create expression %s in %s" % (cls, mat.get_name()))
    return e


def finish_material(mat):
    mel().recompile_material(mat)
    if not unreal.EditorAssetLibrary.save_loaded_asset(mat):
        fail("cannot save material " + mat.get_name())


def build_flat_material():
    if exists(MAT_DEST + "/M_PSL_Flat"):
        return unreal.load_asset(MAT_DEST + "/M_PSL_Flat")
    mat = new_material("M_PSL_Flat")
    mat.set_editor_property("two_sided", True)
    color = expr(mat, unreal.MaterialExpressionVectorParameter, -400, 0)
    color.set_editor_property("parameter_name", "Color")
    color.set_editor_property("default_value", unreal.LinearColor(0.5, 0.5, 0.5, 1.0))
    rough = expr(mat, unreal.MaterialExpressionConstant, -400, 200)
    rough.set_editor_property("r", 0.85)
    mel().connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel().connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    finish_material(mat)
    return mat


def build_leaf_material():
    if exists(MAT_DEST + "/M_PSL_Leaf"):
        return unreal.load_asset(MAT_DEST + "/M_PSL_Leaf")
    mat = new_material("M_PSL_Leaf")
    mat.set_editor_property("two_sided", True)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    tex = expr(mat, unreal.MaterialExpressionTextureSampleParameter2D, -500, 0)
    tex.set_editor_property("parameter_name", "Tex")
    rough = expr(mat, unreal.MaterialExpressionConstant, -500, 300)
    rough.set_editor_property("r", 0.85)
    mel().connect_material_property(tex, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
    mel().connect_material_property(tex, "A", unreal.MaterialProperty.MP_OPACITY_MASK)
    mel().connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    finish_material(mat)
    return mat


def build_terrain_material():
    if exists(MAT_DEST + "/M_PSL_Terrain"):
        return unreal.load_asset(MAT_DEST + "/M_PSL_Terrain")
    mat = new_material("M_PSL_Terrain")
    grass = expr(mat, unreal.MaterialExpressionConstant3Vector, -1200, -200)
    grass.set_editor_property("constant", unreal.LinearColor(0.26, 0.42, 0.14, 1.0))
    dirt = expr(mat, unreal.MaterialExpressionConstant3Vector, -1200, 0)
    dirt.set_editor_property("constant", unreal.LinearColor(0.38, 0.30, 0.20, 1.0))
    # slope mask: saturate((cos(25 deg) - normal.z) * 20)
    normal = expr(mat, unreal.MaterialExpressionVertexNormalWS, -1400, 200)
    nz = expr(mat, unreal.MaterialExpressionComponentMask, -1200, 200)
    for ch, on in (("r", False), ("g", False), ("b", True), ("a", False)):
        nz.set_editor_property(ch, on)
    mel().connect_material_expressions(normal, "", nz, "")
    cos25 = expr(mat, unreal.MaterialExpressionConstant, -1200, 400)
    cos25.set_editor_property("r", math.cos(math.radians(25.0)))
    diff = expr(mat, unreal.MaterialExpressionSubtract, -1000, 250)
    mel().connect_material_expressions(cos25, "", diff, "A")
    mel().connect_material_expressions(nz, "", diff, "B")
    gain = expr(mat, unreal.MaterialExpressionConstant, -1000, 450)
    gain.set_editor_property("r", 20.0)
    scaled = expr(mat, unreal.MaterialExpressionMultiply, -800, 300)
    mel().connect_material_expressions(diff, "", scaled, "A")
    mel().connect_material_expressions(gain, "", scaled, "B")
    mask = expr(mat, unreal.MaterialExpressionSaturate, -600, 300)
    mel().connect_material_expressions(scaled, "", mask, "")
    blend = expr(mat, unreal.MaterialExpressionLinearInterpolate, -400, 0)
    mel().connect_material_expressions(grass, "", blend, "A")
    mel().connect_material_expressions(dirt, "", blend, "B")
    mel().connect_material_expressions(mask, "", blend, "Alpha")
    # low-contrast brightness variation: 1 + 0.08 * noise(worldpos * 0.002), noise in [-1, 1]
    wpos = expr(mat, unreal.MaterialExpressionWorldPosition, -1200, 700)
    noise = expr(mat, unreal.MaterialExpressionNoise, -1000, 700)
    noise.set_editor_property("scale", 0.002)
    noise.set_editor_property("output_min", -1.0)
    noise.set_editor_property("output_max", 1.0)
    mel().connect_material_expressions(wpos, "", noise, "Position")
    amp = expr(mat, unreal.MaterialExpressionConstant, -1000, 850)
    amp.set_editor_property("r", 0.08)
    nscaled = expr(mat, unreal.MaterialExpressionMultiply, -800, 750)
    mel().connect_material_expressions(noise, "", nscaled, "A")
    mel().connect_material_expressions(amp, "", nscaled, "B")
    one = expr(mat, unreal.MaterialExpressionConstant, -800, 900)
    one.set_editor_property("r", 1.0)
    bright = expr(mat, unreal.MaterialExpressionAdd, -600, 800)
    mel().connect_material_expressions(one, "", bright, "A")
    mel().connect_material_expressions(nscaled, "", bright, "B")
    final = expr(mat, unreal.MaterialExpressionMultiply, -200, 300)
    mel().connect_material_expressions(blend, "", final, "A")
    mel().connect_material_expressions(bright, "", final, "B")
    rough = expr(mat, unreal.MaterialExpressionConstant, -200, 500)
    rough.set_editor_property("r", 0.9)
    mel().connect_material_property(final, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel().connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    finish_material(mat)
    return mat


def material_instance(name, parent, vector=None, texture=None):
    path = MAT_DEST + "/" + name
    if exists(path):
        return unreal.load_asset(path)
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    mi = tools.create_asset(name, MAT_DEST, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    if mi is None:
        fail("cannot create material instance " + path)
    mi.set_editor_property("parent", parent)
    if vector is not None:
        mel().set_material_instance_vector_parameter_value(mi, "Color", unreal.LinearColor(vector[0], vector[1], vector[2], 1.0))
    if texture is not None:
        mel().set_material_instance_texture_parameter_value(mi, "Tex", texture)
    if not unreal.EditorAssetLibrary.save_loaded_asset(mi):
        fail("cannot save material instance " + path)
    return mi


def slot_kind(slot, stem):
    s = (slot + " " + stem).lower()
    for kind, keys in (("leaf", ("leaf", "leaves", "needle", "foliage")), ("bark", ("bark", "trunk", "wood", "stem")),
                       ("rock", ("rock", "stone")), ("flower", ("flower", "petal", "bloom")),
                       ("grass", ("grass", "fern", "plant", "bush", "clover"))):
        if any(k in s for k in keys):
            return kind
    return "bark" if re.search(r"(?i)tree|pine|log|stump", stem) else "grass"


def assign_materials(meshes, textures):
    """Create flat/leaf MIs and assign them per mesh slot; returns {mesh: [(slot, MI name)]}."""
    flat, leaf, terrain = build_flat_material(), build_leaf_material(), build_terrain_material()
    del terrain  # created for the landscape; loaded by path later
    flat_mis = {k: material_instance("MI_PSL_" + k.capitalize(), flat, vector=v) for k, v in PALETTE.items()}
    leaf_mis = {}
    for t in (t for t in textures if re.match(r"(?i)leaf", t)):
        tex = unreal.load_asset(TEX_DEST + "/" + t)
        if tex is None:
            fail("cannot load texture " + t)
        leaf_mis[t] = material_instance("MI_PSL_Leaf_" + t, leaf, texture=tex)
    report = {}
    for stem in meshes:
        mesh = unreal.load_asset(NATURE_DEST + "/SM_" + stem)
        if mesh is None:
            fail("cannot load mesh SM_" + stem)
        report[stem] = []
        for i, sm in enumerate(mesh.get_editor_property("static_materials")):
            slot = str(sm.get_editor_property("material_slot_name"))
            kind = slot_kind(slot, stem)
            if kind == "leaf":
                if not leaf_mis:
                    fail("leaf slot %s on SM_%s but no Leaves_* textures imported" % (slot, stem))
                words = [w for w in re.split(r"[^a-z0-9]+", (slot + " " + stem).lower()) if w]
                pick = next((t for t in sorted(leaf_mis) if any(w in t.lower() for w in words if len(w) > 2 and w not in ("leaf", "leaves"))), sorted(leaf_mis)[0])
                mi = leaf_mis[pick]
            else:
                mi = flat_mis[kind]
            mesh.set_material(i, mi)
            report[stem].append((slot, mi.get_name()))
        if not unreal.EditorAssetLibrary.save_loaded_asset(mesh):
            fail("cannot save SM_" + stem)
    return report


def pick(stems, pattern, required=True):
    hits = [s for s in stems if re.search(pattern, s)]
    if not hits and required:
        fail("no Nature mesh matching /%s/ among %s" % (pattern, stems))
    return hits


def transform(x, y, z, yaw=0.0, scale=1.0):
    return unreal.Transform(unreal.Vector(x, y, z), unreal.Rotator(0.0, 0.0, yaw), unreal.Vector(scale, scale, scale))


def spawn(cls, label, x=0.0, y=0.0, z=0.0, rot=None):
    actor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).spawn_actor_from_class(
        cls, unreal.Vector(x, y, z), rot or unreal.Rotator(0, 0, 0))
    if actor is None:
        fail("failed to spawn " + label)
    actor.set_actor_label(label)
    return actor


def holder(label, groups, collision, cull):
    """One holder actor per category; groups = [(mesh_stem, [Transform])], one HISM per mesh.

    The holder spawns without a root; the library roots the first HISM (identity), later ones attach to it."""
    actor = spawn(unreal.Actor, label)
    total = 0
    for stem, transforms in groups:
        if not transforms:
            continue
        mesh = unreal.load_asset(NATURE_DEST + "/SM_" + stem)
        if mesh is None:
            fail("cannot load SM_" + stem)
        if unreal.PSLLevelBuildLibrary.add_mesh_instances(actor, mesh, transforms, collision, cull) is None:
            fail("add_mesh_instances failed for %s / %s" % (label, stem))
        total += len(transforms)
    if total == 0:
        fail(label + " has no instances")
    return total


def split(stems, pts, rng, z_of, scale_range, yaw=True, z_off=0.0):
    groups = {s: [] for s in stems}
    for x, y in pts:
        s = rng.choice(stems)
        groups[s].append(transform(x, y, z_of(x, y) + z_off, rng.uniform(0, 360) if yaw else 0.0, rng.uniform(*scale_range)))
    return list(groups.items())


def build_foliage(grid, poly, stems):
    rng = random.Random(SEED)
    z_of = lambda x, y: sample_height(grid, x, y)
    free = lambda x, y: not excluded(x, y, poly)
    ring = lambda x, y: free(x, y) and (4000.0 <= math.hypot(x, y) <= 11000.0 or rng.random() < 0.35)
    trees = pick(stems, r"^(CommonTree|Pine)_")
    tree_pts = poisson(rng, 220, 700.0, ring)
    rocks = pick(stems, r"^Rock_Medium_[123]$")
    rock_pts = scatter(rng, 60, free)
    rock_groups = {s: [] for s in rocks}
    for x, y in rock_pts:
        rock_groups[rng.choice(rocks)].append(
            transform(x, y, z_of(x, y) + (-40.0 if rng.random() < 0.3 else 0.0), rng.uniform(0, 360), rng.uniform(1.0, 3.0)))
    bushes = pick(stems, r"^(Bush|Fern|Plant)")
    grass = pick(stems, r"^(Grass|Flower|Clover)")
    stone = pick(stems, r"^(Rock_Small|Pebble)", required=False) or rocks[:1]
    stones = []
    for i, (x, y, tx, ty) in enumerate(resample(poly, 180.0)):
        side = 1.0 if i % 2 == 0 else -1.0
        sx, sy = x - ty * 120.0 * side, y + tx * 120.0 * side
        stones.append(transform(sx, sy, z_of(sx, sy), rng.uniform(0, 360), rng.uniform(0.4, 0.8)))
    counts = {
        "trees": holder("PSL_Foliage_Trees", split(trees, tree_pts, rng, z_of, (0.9, 1.4)), True, 30000.0),
        "rocks": holder("PSL_Foliage_Rocks", list(rock_groups.items()), True, 30000.0),
        "bushes": holder("PSL_Foliage_Bushes", split(bushes, scatter(rng, 300, free), rng, z_of, (0.8, 1.3)), False, 12000.0),
        "grass": holder("PSL_Foliage_Grass", split(grass, scatter(rng, 2500, free), rng, z_of, (0.8, 1.3)), False, 6000.0),
        "pathStones": holder("PSL_PathStones", [(stone[0], stones)], False, 8000.0),
    }
    return counts


def build_landmarks(grid, stems):
    dead = pick(stems, r"^DeadTree_1$")[0]
    rock = pick(stems, r"^Rock_Medium_1$")[0]
    z = sample_height(grid, MOUND[0], MOUND[1])
    a = spawn(unreal.StaticMeshActor, "PSL_Landmark_DeadTree", MOUND[0], MOUND[1], z)
    a.set_actor_scale3d(unreal.Vector(3, 3, 3))
    a.static_mesh_component.set_static_mesh(unreal.load_asset(NATURE_DEST + "/SM_" + dead))
    cluster = spawn(unreal.Actor, "PSL_Landmark_Rocks")
    rng = random.Random(SEED + 1)
    cx, cy = 2600.0, -2600.0
    mesh = unreal.load_asset(NATURE_DEST + "/SM_" + rock)
    ts = []
    for k in range(5):
        ang = k * 2 * math.pi / 5 + rng.uniform(-0.3, 0.3)
        x, y = cx + 450.0 * math.cos(ang), cy + 450.0 * math.sin(ang)
        ts.append(transform(x, y, sample_height(grid, x, y), rng.uniform(0, 360), rng.uniform(4.0, 6.0)))
    if unreal.PSLLevelBuildLibrary.add_mesh_instances(cluster, mesh, ts, True, 0.0) is None:
        fail("landmark rocks failed")


def build_gameplay(grid, poly):
    sx, sy = PATH_POINTS[0][0] + 400.0, PATH_POINTS[0][1] + 300.0
    tx, ty = poly[1][0] - poly[0][0], poly[1][1] - poly[0][1]
    yaw = math.degrees(math.atan2(ty, tx))
    spawn(unreal.PlayerStart, "PSL_PlayerStart", sx, sy, sample_height(grid, sx, sy) + 120.0, unreal.Rotator(0.0, 0.0, yaw))
    nav = spawn(unreal.NavMeshBoundsVolume, "PSL_NavBounds", 0.0, 0.0, 1250.0)
    nav.set_actor_scale3d(unreal.Vector(100.0, 100.0, 17.5))  # default brush half-extent 100: +-10000 x +-10000 x -500..3000
    prey_class = unreal.load_class(None, PREY_CLASS)
    if prey_class is None:
        fail("cannot load " + PREY_CLASS + " (is the editor module built?)")
    rng = random.Random(SEED + 2)
    mx, my = MEADOW[0], MEADOW[1]
    for label in PREY_LABELS:
        x, y = mx + rng.uniform(-800, 800), my + rng.uniform(-800, 800)
        spawn(prey_class, label, x, y, sample_height(grid, x, y) + 100.0)


def build_lighting():
    sun = spawn(unreal.DirectionalLight, "PSL_Sun", 0, 0, 5000, unreal.Rotator(0.0, -38.0, 135.0))  # Rotator(roll, pitch, yaw)
    sun.root_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    lc = sun.get_component_by_class(unreal.DirectionalLightComponent)
    lc.set_editor_property("intensity", 6.0)
    lc.set_editor_property("light_color", unreal.Color(255, 237, 209, 255))
    lc.set_editor_property("atmosphere_sun_light", True)
    spawn(unreal.SkyAtmosphere, "PSL_SkyAtmosphere")
    sky = spawn(unreal.SkyLight, "PSL_SkyLight", 0, 0, 5000)
    sky.root_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    sc = sky.get_component_by_class(unreal.SkyLightComponent)
    sc.set_editor_property("real_time_capture", True)
    sc.set_editor_property("intensity", 1.0)
    fog = spawn(unreal.ExponentialHeightFog, "PSL_Fog", 0, 0, 0)
    fc = fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
    fc.set_editor_property("fog_density", 0.015)
    fc.set_editor_property("fog_height_falloff", 0.25)
    fc.set_editor_property("fog_inscattering_luminance", unreal.LinearColor(0.55, 0.65, 0.75, 1.0))
    fc.set_editor_property("start_distance", 2000.0)
    fc.set_editor_property("enable_volumetric_fog", False)
    pp = spawn(unreal.PostProcessVolume, "PSL_PostProcess")
    pp.set_editor_property("unbound", True)
    s = pp.get_editor_property("settings")
    for flag, prop, value in (
        ("override_auto_exposure_min_brightness", "auto_exposure_min_brightness", 1.0),
        ("override_auto_exposure_max_brightness", "auto_exposure_max_brightness", 1.0),
        ("override_bloom_intensity", "bloom_intensity", 0.4),
        ("override_color_saturation", "color_saturation", unreal.Vector4(1.08, 1.08, 1.08, 1.0)),
        ("override_color_contrast", "color_contrast", unreal.Vector4(1.05, 1.05, 1.05, 1.0)),
        ("override_white_temp", "white_temp", 6000.0),
        ("override_vignette_intensity", "vignette_intensity", 0.3),
    ):
        s.set_editor_property(flag, True)
        s.set_editor_property(prop, value)
    pp.set_editor_property("settings", s)


def editor_world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()


def check():
    """Inspect the loaded map by real actor class; returns (counts, problems) and never exits."""
    problems = []
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    found = {a.get_actor_label(): a for a in actors}
    counts = {}

    lands = [a for a in actors if isinstance(a, unreal.Landscape)]
    if len(lands) != 1:
        problems.append("expected 1 Landscape, got %d" % len(lands))
    else:
        counts["landscapeComponents"] = len(lands[0].get_components_by_class(unreal.LandscapeComponent))
        if counts["landscapeComponents"] != 16:
            problems.append("landscape component count %d, expected 16" % counts["landscapeComponents"])

    for label in FOLIAGE_LABELS:
        a = found.get(label)
        if a is None:
            problems.append("missing " + label)
            continue
        counts[label] = sum(c.get_instance_count()
                            for c in a.get_components_by_class(unreal.HierarchicalInstancedStaticMeshComponent))
        if counts[label] <= 0:
            problems.append(label + " has no instances")

    prey_class = unreal.load_class(None, PREY_CLASS)
    if prey_class is None:
        problems.append("cannot load " + PREY_CLASS)
    else:
        prey = [a for a in actors if a.get_class() == prey_class]
        counts["prey"] = len(prey)
        if len(prey) != len(PREY_LABELS):
            problems.append("expected %d PSLPreyCharacter actors, got %d" % (len(PREY_LABELS), len(prey)))

    typed = {"PSL_PlayerStart": unreal.PlayerStart, "PSL_NavBounds": unreal.NavMeshBoundsVolume,
             "PSL_Sun": unreal.DirectionalLight, "PSL_SkyAtmosphere": unreal.SkyAtmosphere,
             "PSL_SkyLight": unreal.SkyLight, "PSL_Fog": unreal.ExponentialHeightFog,
             "PSL_PostProcess": unreal.PostProcessVolume}
    for label, cls in typed.items():
        a = found.get(label)
        if a is None:
            problems.append("missing " + label)
        elif not isinstance(a, cls):
            problems.append("%s is %s, expected %s" % (label, a.get_class().get_name(), cls.__name__))
    for label in ("PSL_Landmark_DeadTree", "PSL_Landmark_Rocks"):
        if label not in found:
            problems.append("missing " + label)

    gm = editor_world().get_world_settings().get_editor_property("default_game_mode")
    if gm is None or "PSLGameMode" not in gm.get_path_name():
        problems.append("world default game mode is not PSLGameMode: %s" % (gm,))
    return counts, problems


def validate():
    counts, problems = check()
    if problems:
        fail("; ".join(problems))
    return counts


def build_map(levels):
    meshes, textures = import_nature()
    import_prey()
    assign_materials(meshes, textures)
    terrain = unreal.load_asset(MAT_DEST + "/M_PSL_Terrain")

    if not levels.new_level(MAP_PATH, False):  # saves the (empty) map immediately
        fail("new_level failed for " + MAP_PATH)
    poly = catmull_rom(PATH_POINTS)
    heights, grid = build_heights(poly)
    land = unreal.PSLLevelBuildLibrary.create_landscape(
        editor_world(), unreal.Vector(LAND_ORIGIN, LAND_ORIGIN, 0.0),
        unreal.Vector(100.0, 100.0, 100.0), SIZE_VERTS, heights, terrain)
    if land is None:
        fail("create_landscape failed")
    land.set_actor_label("PSL_Landscape")
    build_foliage(grid, poly, meshes)
    build_landmarks(grid, meshes)
    build_gameplay(grid, poly)
    build_lighting()
    gm = unreal.load_class(None, GAME_MODE)
    if gm is None:
        fail("cannot load " + GAME_MODE)
    editor_world().get_world_settings().set_editor_property("default_game_mode", gm)

    if not levels.save_current_level():
        fail("save_current_level failed")
    if not levels.load_level(MAP_PATH):
        fail("reload failed for " + MAP_PATH)


def main():
    if MAP_PATH.endswith("L_TestArena"):  # guard: this script never works on the test arena
        fail("refusing to touch L_TestArena")
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if exists(MAP_PATH):
        if not levels.load_level(MAP_PATH):
            fail("load_level failed for " + MAP_PATH)
        counts, problems = check()
        if not problems:
            write_report("ok", counts)
            log("already present, validated")
            return
        # a failed earlier run leaves an (empty) map behind: drop this script's own output and rebuild
        log("existing map invalid, rebuilding: " + "; ".join(problems))
        unreal.EditorLoadingAndSavingUtils.new_blank_map(False)  # unload so the asset can be deleted
        if not unreal.EditorAssetLibrary.delete_asset(MAP_PATH):
            fail("cannot delete invalid " + MAP_PATH)
    build_map(levels)
    counts = validate()
    write_report("ok", counts)
    log("done " + json.dumps(counts))


def log(msg):
    unreal.log(LOG + msg)


if "--selftest" in sys.argv:
    selftest()
else:
    try:
        import unreal  # type: ignore  # only available inside the Unreal editor
        main()
    except SystemExit:
        raise
    except ImportError as exc:
        fail("unreal module unavailable (run inside the editor, or use --selftest): %r" % (exc,))
    except Exception as exc:  # unreal raises plain Exception subclasses
        fail("unexpected error: %r" % (exc,))
