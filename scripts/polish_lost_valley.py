"""Polish /Game/Maps/L_LostValley in place (run AFTER build_lost_valley.py): character MIs, lighting, layered
terrain material + generated path mask, clustered grass/bushes, spawn landmark. Never rebuilds the map.

Run (only under the harness build lock, no other Unreal job running):
    UnrealEditor-Cmd PrehistoricSurvivalLab.uproject -EnablePlugins=PythonScriptPlugin \
      -ExecutePythonScript=<abs>/scripts/polish_lost_valley.py -ScriptErrorsAreFatal -unattended -nosplash

Pure helpers run without `unreal`:  python3 scripts/polish_lost_valley.py --selftest
Idempotent: materials/MIs are updated, the mask texture is regenerated, grass/bush holders and the spawn-rocks
landmark are destroyed and re-created. Writes Saved/PSL/lost-valley.json {status, scriptSha256, counts, errors}.
"""
import hashlib
import json
import math
import os
import random
import re
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
os.environ["PSL_BUILD_IMPORT_ONLY"] = "1"
import build_lost_valley as B  # noqa: E402

LOG = "PSL polish_lost_valley: "
ERRORS = []
unreal = None  # imported lazily so --selftest runs without the editor

CHAR_DEST = "/Game/Characters/Materials"
TREX_MI_DEST = "/Game/Characters/Dino/Materials"
PARA_MI_DEST = "/Game/Characters/Prey/Materials"
MASK_NAME = "T_PSL_PathMask"
MASK_PATH = B.TEX_DEST + "/" + MASK_NAME
MASK_RES = 512
LAND_SIZE = (B.SIZE_VERTS - 1) * B.VERT_STEP
GRASS_MAX, BUSH_MAX = 2500, 300  # budgets: never above the build script's counts
SPAWN_ROCKS = "PSL_Landmark_SpawnRocks"

# slot name keyword -> linear albedo. Luminance floor 0.15 (spec B1).
TREX_PALETTE = {"black": (0.24, 0.17, 0.11), "green": (0.30, 0.30, 0.12), "lightgreen": (0.42, 0.38, 0.18),
                "lightyellow": (0.62, 0.55, 0.38), "red": (0.40, 0.18, 0.09)}
TREX_DEFAULT = (0.33, 0.30, 0.14)
PARA_PALETTE = {"green": (0.62, 0.48, 0.30), "lightgreen": (0.72, 0.60, 0.40), "lightyellow": (0.80, 0.70, 0.50),
                "red": (0.55, 0.25, 0.12), "black": (0.30, 0.20, 0.12)}
PARA_DEFAULT = (0.62, 0.48, 0.30)


def luminance(c):
    return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]


def palette_color(palette, default, slot):
    return palette.get(re.sub(r"[^a-z]", "", slot.lower()), default)


# ---------------------------------------------------------------- pure helpers
def mask_pixels(poly, res=MASK_RES):
    """res*res bytes, row-major (v = y fastest-changing is x). 1 inside PATH_FLAT_RADIUS of the path, smooth to 0 at
    PATH_BLEND_RADIUS, plus 0.35*noise inside the clearing. Pixel (i, j) covers world (origin + (i+.5)/res*size, ...)."""
    r_out = B.PATH_BLEND_RADIUS
    dist = [[1e9] * res for _ in range(res)]
    cell = LAND_SIZE / res
    for a, b in zip(poly, poly[1:]):
        i0 = max(0, int((min(a[0], b[0]) - r_out - B.LAND_ORIGIN) / cell))
        i1 = min(res - 1, int((max(a[0], b[0]) + r_out - B.LAND_ORIGIN) / cell))
        j0 = max(0, int((min(a[1], b[1]) - r_out - B.LAND_ORIGIN) / cell))
        j1 = min(res - 1, int((max(a[1], b[1]) + r_out - B.LAND_ORIGIN) / cell))
        for j in range(j0, j1 + 1):
            y = B.LAND_ORIGIN + (j + 0.5) * cell
            row = dist[j]
            for i in range(i0, i1 + 1):
                d = B._seg_dist(B.LAND_ORIGIN + (i + 0.5) * cell, y, a, b)
                if d < row[i]:
                    row[i] = d
    out = bytearray(res * res)
    cx, cy, cr = B.CLEARING
    for j in range(res):
        y = B.LAND_ORIGIN + (j + 0.5) * cell
        for i in range(res):
            x = B.LAND_ORIGIN + (i + 0.5) * cell
            v = 1.0 - B.smoothstep(B.PATH_FLAT_RADIUS, r_out, dist[j][i])
            if math.hypot(x - cx, y - cy) < cr:
                v = max(v, 0.35 * (0.6 + 0.4 * B.value_noise(x, y, 400.0)))
            out[j * res + i] = int(round(max(0.0, min(1.0, v)) * 255))
    return bytes(out)


def png_gray(pixels, res):
    """Minimal 8-bit greyscale PNG."""
    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    raw = b"".join(b"\x00" + pixels[r * res:(r + 1) * res] for r in range(res))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", res, res, 8, 0, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def clustered_points(rng, total, anchors, accept, radius=350.0, size=(6, 15)):
    """Clusters of 6-15 points around random anchors (x, y); stops at `total` points or after 40*total throws."""
    pts, tries = [], 0
    while len(pts) < total and tries < total * 40:
        ax, ay = rng.choice(anchors)
        n = rng.randint(*size)
        for _ in range(n):
            tries += 1
            x, y = ax + rng.gauss(0, radius * 0.5), ay + rng.gauss(0, radius * 0.5)
            if abs(x) < B.EXTENT and abs(y) < B.EXTENT and accept(x, y) and len(pts) < total:
                pts.append((x, y))
    return pts


def edge_anchors(poly, tree_xy, rng):
    """Clearing edge, path shoulders (300-600 uu off the path) and tree bases."""
    cx, cy, cr = B.CLEARING
    anchors = [(cx + (cr + 150.0) * math.cos(a), cy + (cr + 150.0) * math.sin(a))
               for a in (k * math.pi / 12 for k in range(24))]
    for x, y, tx, ty in B.resample(poly, 300.0):
        off = rng.uniform(300.0, 600.0) * rng.choice((-1.0, 1.0))
        anchors.append((x - ty * off, y + tx * off))
    return anchors + list(tree_xy)


ROCK_R, ROCK_SCALE, ROCK_CLEAR = 220.0, (3.0, 4.5), 600.0  # rock radius per unit scale; min gap to path/PlayerStart


def rock_spot(poly, start_xy, clear):
    """Left of the first path bend (poly[0]->(0,0) direction turns at PATH_POINTS[1]), >= `clear` uu from the path and
    the PlayerStart; returns (x, y) of the cluster centre."""
    bx, by = B.PATH_POINTS[1]
    tx, ty = bx - B.PATH_POINTS[0][0], by - B.PATH_POINTS[0][1]
    n = math.hypot(tx, ty)
    for off in range(900, 3600, 100):
        x, y = bx - ty / n * off, by + tx / n * off  # left of travel direction
        if B.path_distance(x, y, poly) >= clear and math.hypot(x - start_xy[0], y - start_xy[1]) >= clear:
            return x, y
    raise ValueError("no spawn-rock spot found")


def place_rocks(poly, start_xy, rng):
    """3 rocks (x, y, yaw, scale) around the cluster centre; each has >= ROCK_CLEAR uu from the path and the
    PlayerStart measured at centre - ROCK_R * scale."""
    cx, cy = rock_spot(poly, start_xy, ROCK_CLEAR + ROCK_R * ROCK_SCALE[0] + ROCK_R)
    out = []
    for k in range(3):
        for _ in range(100):
            ang = k * 2 * math.pi / 3 + 0.4 + rng.uniform(-0.3, 0.3)
            x, y, s = cx + ROCK_R * math.cos(ang), cy + ROCK_R * math.sin(ang), rng.uniform(*ROCK_SCALE)
            if min(B.path_distance(x, y, poly), math.hypot(x - start_xy[0], y - start_xy[1])) - ROCK_R * s >= ROCK_CLEAR:
                out.append((x, y, rng.uniform(0, 360), s))
                break
        else:
            raise ValueError("no clear spot for spawn rock %d" % k)
    return out


def selftest():
    poly = B.catmull_rom(B.PATH_POINTS)
    px = mask_pixels(poly, 128)
    mid = B.PATH_POINTS[2]
    i = int((mid[0] - B.LAND_ORIGIN) / (LAND_SIZE / 128))
    j = int((mid[1] - B.LAND_ORIGIN) / (LAND_SIZE / 128))
    assert px[j * 128 + i] >= 250, "mask not ~1 on the path"
    assert px[0] == 0, "mask not 0 far from the path"
    assert 0 < sum(1 for v in px if v) < 128 * 128 // 2, "mask coverage implausible"
    png = png_gray(px, 128)
    assert png[:8] == b"\x89PNG\r\n\x1a\n" and zlib.decompress(png[png.index(b"IDAT") + 4:-16])[0] == 0
    rng = random.Random(1)
    free = lambda x, y: not B.excluded(x, y, poly)
    anchors = edge_anchors(poly, [(-3000.0, 3000.0)], rng)
    pts = clustered_points(rng, 600, anchors, free)
    assert len(pts) == 600 and all(free(x, y) for x, y in pts), "clustered points invalid"
    assert len(set(pts)) == len(pts)
    sx, sy = B.PATH_POINTS[0][0] + 400.0, B.PATH_POINTS[0][1] + 300.0
    rocks = place_rocks(poly, (sx, sy), rng)
    for rx, ry, _, s in rocks:
        assert min(B.path_distance(rx, ry, poly), math.hypot(rx - sx, ry - sy)) - ROCK_R * s >= ROCK_CLEAR
    rx, ry = rocks[0][:2]
    for pal in (TREX_PALETTE, {"x": TREX_DEFAULT}):
        assert all(luminance(c) >= 0.15 for c in pal.values()), "T-Rex albedo too dark"
    assert palette_color(TREX_PALETTE, TREX_DEFAULT, "Light Green") == TREX_PALETTE["lightgreen"]
    print(LOG + "selftest ok (%d mask px, %d cluster pts, rocks at %.0f,%.0f)" % (sum(1 for v in px if v), len(pts), rx, ry))


# ---------------------------------------------------------------- Unreal side
def mel():
    return unreal.MaterialEditingLibrary


def link(a, out, b, inp=""):
    mel().connect_material_expressions(a, out, b, inp)


def node(mat, cls, px, py, **props):
    e = B.expr(mat, cls, px, py)
    for k, v in props.items():
        e.set_editor_property(k, v)
    return e


def const(mat, v, px, py):
    return node(mat, unreal.MaterialExpressionConstant, px, py, r=v)


def scalar_param(mat, name, v, px, py):
    return node(mat, unreal.MaterialExpressionScalarParameter, px, py, parameter_name=name, default_value=v)


def mul(mat, a, b, px, py, ao="", bo=""):
    m = B.expr(mat, unreal.MaterialExpressionMultiply, px, py)
    link(a, ao, m, "A")
    link(b, bo, m, "B")
    return m


def asset_or_fail(path):
    a = unreal.load_asset(path)
    if a is None:
        B.fail("cannot load " + path)
    return a


def fresh_material(name, dest):
    """Create or reuse the material and wipe its graph so reruns converge."""
    path = dest + "/" + name
    if B.exists(path):
        mat = unreal.load_asset(path)
        mel().delete_all_material_expressions(mat)
        return mat
    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, dest, unreal.Material, unreal.MaterialFactoryNew())
    if mat is None:
        B.fail("cannot create material " + path)
    return mat


def build_character_material():
    mat = fresh_material("M_PSL_Character", CHAR_DEST)
    mat.set_editor_property("used_with_skeletal_mesh", True)
    color = node(mat, unreal.MaterialExpressionVectorParameter, -900, 0, parameter_name="Color",
                 default_value=unreal.LinearColor(*TREX_DEFAULT, 1.0))
    dead = scalar_param(mat, "Dead", 0.0, -900, 200)
    flash = scalar_param(mat, "HitFlash", 0.0, -900, 500)
    rim = scalar_param(mat, "Rim", 0.12, -900, 350)
    one = const(mat, 1.0, -900, 90)
    desat = node(mat, unreal.MaterialExpressionDesaturation, -800, 100)
    link(color, "", desat, "")
    link(one, "", desat, "Fraction")
    dark = mul(mat, desat, const(mat, 0.3, -900, 120), -650, 60)
    base = node(mat, unreal.MaterialExpressionLinearInterpolate, -450, 60)
    link(color, "", base, "A")
    link(dark, "", base, "B")
    link(dead, "", base, "Alpha")
    fres = node(mat, unreal.MaterialExpressionFresnel, -700, 350, exponent=3.0)
    rim_term = mul(mat, mul(mat, fres, rim, -500, 350), color, -300, 350)
    flash_term = mul(mat, flash, node(mat, unreal.MaterialExpressionConstant3Vector, -700, 550,
                                      constant=unreal.LinearColor(3.0, 1.0, 0.7, 1.0)), -500, 520)
    emissive = node(mat, unreal.MaterialExpressionAdd, -150, 400)
    link(rim_term, "", emissive, "A")
    link(flash_term, "", emissive, "B")
    rough = const(mat, 0.65, -300, 700)
    mel().connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel().connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel().connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    B.finish_material(mat)
    return mat


def character_mi(name, dest, parent, rgb):
    path = dest + "/" + name
    if B.exists(path):
        mi = unreal.load_asset(path)
    else:
        mi = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, dest, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        if mi is None:
            B.fail("cannot create material instance " + path)
    mi.set_editor_property("parent", parent)
    mel().set_material_instance_vector_parameter_value(mi, "Color", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
    for p, v in (("Rim", 0.12), ("HitFlash", 0.0), ("Dead", 0.0)):
        mel().set_material_instance_scalar_parameter_value(mi, p, v)
    if not unreal.EditorAssetLibrary.save_loaded_asset(mi):
        B.fail("cannot save material instance " + path)
    return mi


def assign_character_mis(mesh_path, prefix, dest, parent, palette, default):
    """One MI per mesh slot, assigned through skeletal_mesh.materials; returns [(slot, MI name)]."""
    mesh = asset_or_fail(mesh_path)
    mats, out = list(mesh.get_editor_property("materials")), []
    for sm in mats:
        slot = str(sm.get_editor_property("material_slot_name"))
        mi = character_mi("MI_%s_%s" % (prefix, re.sub(r"[^A-Za-z0-9]", "", slot) or "Slot"), dest, parent,
                          palette_color(palette, default, slot))
        sm.set_editor_property("material_interface", mi)
        out.append((slot, mi.get_name()))
    mesh.set_editor_property("materials", mats)
    if not unreal.EditorAssetLibrary.save_loaded_asset(mesh):
        B.fail("cannot save " + mesh_path)
    return out


def tex_sample(mat, tex, uv, px, py, sampler=None):
    t = node(mat, unreal.MaterialExpressionTextureSample, px, py, texture=tex)
    if sampler is not None:
        t.set_editor_property("sampler_type", sampler)
    link(uv, "", t, "UVs")
    return t


def import_mask():
    """Regenerate T_PSL_PathMask from the path polyline (linear greyscale, no mips needed for a 512^2 mask)."""
    png = os.path.join(B.ROOT, "Saved", "PSL", MASK_NAME + ".png")
    os.makedirs(os.path.dirname(png), exist_ok=True)
    with open(png, "wb") as f:
        f.write(png_gray(mask_pixels(B.catmull_rom(B.PATH_POINTS)), MASK_RES))
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", png)
    task.set_editor_property("destination_path", B.TEX_DEST)
    task.set_editor_property("destination_name", MASK_NAME)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", True)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    if not B.exists(MASK_PATH):
        B.fail("mask import failed: " + MASK_PATH)
    tex = unreal.load_asset(MASK_PATH)
    tex.set_editor_property("srgb", False)
    tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
    tex.set_editor_property("address_x", unreal.TextureAddress.TA_CLAMP)
    tex.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)
    if not unreal.EditorAssetLibrary.save_loaded_asset(tex):
        B.fail("cannot save " + MASK_PATH)
    return tex


def build_terrain_material(mask):
    """Rewrites M_PSL_Terrain's graph in place so the landscape's material reference stays valid."""
    mat = fresh_material("M_PSL_Terrain", B.MAT_DEST)
    wpos = node(mat, unreal.MaterialExpressionWorldPosition, -2400, 0)
    xy = node(mat, unreal.MaterialExpressionComponentMask, -2200, 0, r=True, g=True, b=False, a=False)
    link(wpos, "", xy, "")

    def scaled_uv(k, py):
        return mul(mat, xy, const(mat, k, -2200, py + 60), -2000, py)

    def tex_at(name, k, py):
        return tex_sample(mat, asset_or_fail(B.TEX_DEST + "/" + name), scaled_uv(k, py), -1700, py)

    def noise(scale, lo, hi, py):
        n = node(mat, unreal.MaterialExpressionNoise, -1700, py, scale=scale, output_min=lo, output_max=hi)
        link(wpos, "", n, "Position")
        return n

    def vec3(r, g, b, py):
        return node(mat, unreal.MaterialExpressionConstant3Vector, -1700, py, constant=unreal.LinearColor(r, g, b, 1.0))

    def tinted(lo, hi, scale, py):
        t = node(mat, unreal.MaterialExpressionLinearInterpolate, -1450, py)
        link(vec3(*lo, py), "", t, "A")
        link(vec3(*hi, py + 60), "", t, "B")
        link(noise(scale, 0.0, 1.0, py + 120), "", t, "Alpha")
        return t

    # shared micro-variation: luminance of Rocks_Diffuse at 1/300, remapped to 0.85..1.10
    lum = node(mat, unreal.MaterialExpressionDotProduct, -1450, -900)
    link(tex_at("Rocks_Diffuse", 1.0 / 300.0, -900), "RGB", lum, "A")
    link(vec3(0.3, 0.59, 0.11, -800), "", lum, "B")
    detail = node(mat, unreal.MaterialExpressionAdd, -1250, -900)
    link(const(mat, 0.85, -1450, -820), "", detail, "A")
    link(mul(mat, lum, const(mat, 0.25, -1450, -760), -1250, -800), "", detail, "B")

    def one_plus_noise(scale, amp, py):
        s = node(mat, unreal.MaterialExpressionAdd, -1250, py)
        link(const(mat, 1.0, -1450, py), "", s, "A")
        link(noise(scale, -amp, amp, py + 60), "", s, "B")
        return s

    grass = mul(mat, mul(mat, tinted((0.085, 0.16, 0.035), (0.20, 0.27, 0.07), 0.0015, -700), detail, -1100, -700),
                one_plus_noise(0.006, 0.1, -600), -950, -650)
    dirt = mul(mat, tinted((0.24, 0.17, 0.10), (0.36, 0.27, 0.16), 0.004, -450), detail, -1100, -450)
    desat = node(mat, unreal.MaterialExpressionDesaturation, -1450, 0)
    link(tex_at("Rocks_Diffuse", 1.0 / 600.0, 0), "RGB", desat, "")
    link(const(mat, 0.6, -1700, 100), "", desat, "Fraction")
    rock = mul(mat, desat, vec3(0.95, 0.9, 0.85, 150), -1250, 0)

    # path/clearing mask, sampled at (xy - LAND_ORIGIN) / LAND_SIZE
    off = node(mat, unreal.MaterialExpressionAdd, -2000, 300)
    link(xy, "", off, "A")
    link(node(mat, unreal.MaterialExpressionConstant2Vector, -2200, 330, r=-B.LAND_ORIGIN, g=-B.LAND_ORIGIN), "", off, "B")
    muv = mul(mat, off, const(mat, 1.0 / LAND_SIZE, -2000, 400), -1800, 300)
    path = tex_sample(mat, mask, muv, -1600, 300, unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)

    # large dirt patches: ~15% coverage from low-frequency noise
    pn = node(mat, unreal.MaterialExpressionNoise, -1600, 550, scale=0.0004, output_min=0.0, output_max=1.0)
    link(wpos, "", pn, "Position")
    pdiff = node(mat, unreal.MaterialExpressionSubtract, -1400, 550)
    link(pn, "", pdiff, "A")
    link(const(mat, 0.7, -1600, 650), "", pdiff, "B")
    patch = node(mat, unreal.MaterialExpressionSaturate, -1000, 550)
    link(mul(mat, pdiff, const(mat, 8.0, -1400, 650), -1200, 550), "", patch, "")
    dirt_a = node(mat, unreal.MaterialExpressionSaturate, -800, 400)
    sum_a = node(mat, unreal.MaterialExpressionAdd, -1000, 400)
    link(path, "R", sum_a, "A")
    link(patch, "", sum_a, "B")
    link(sum_a, "", dirt_a, "")

    ground = node(mat, unreal.MaterialExpressionLinearInterpolate, -600, -300)
    link(grass, "", ground, "A")
    link(dirt, "", ground, "B")
    link(dirt_a, "", ground, "Alpha")

    # rock above 25 deg: saturate((cos25 - normal.z) * 20)
    normal = node(mat, unreal.MaterialExpressionVertexNormalWS, -1400, 800)
    nz = node(mat, unreal.MaterialExpressionComponentMask, -1200, 800, r=False, g=False, b=True, a=False)
    link(normal, "", nz, "")
    sdiff = node(mat, unreal.MaterialExpressionSubtract, -1000, 800)
    link(const(mat, math.cos(math.radians(25.0)), -1200, 900), "", sdiff, "A")
    link(nz, "", sdiff, "B")
    slope = node(mat, unreal.MaterialExpressionSaturate, -600, 800)
    link(mul(mat, sdiff, const(mat, 20.0, -1000, 900), -800, 800), "", slope, "")
    surface = node(mat, unreal.MaterialExpressionLinearInterpolate, -400, -200)
    link(ground, "", surface, "A")
    link(rock, "", surface, "B")
    link(slope, "", surface, "Alpha")

    # macro brightness variation: 1 + 0.08 * noise(worldpos * 0.002), noise in [-1, 1]
    mn = node(mat, unreal.MaterialExpressionNoise, -1000, 1100, scale=0.002, output_min=-1.0, output_max=1.0)
    link(wpos, "", mn, "Position")
    bright = node(mat, unreal.MaterialExpressionAdd, -600, 1100)
    link(const(mat, 1.0, -800, 1050), "", bright, "A")
    link(mul(mat, mn, const(mat, 0.08, -800, 1200), -800, 1150), "", bright, "B")
    final = mul(mat, surface, bright, -200, 0)
    mel().connect_material_property(final, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel().connect_material_property(const(mat, 0.9, -200, 300), "", unreal.MaterialProperty.MP_ROUGHNESS)
    B.finish_material(mat)
    return mat


def write_report(status, counts=None):
    with open(os.path.abspath(__file__), "rb") as f:
        sha = hashlib.sha256(f.read()).hexdigest()
    os.makedirs(os.path.dirname(B.REPORT), exist_ok=True)
    with open(B.REPORT, "w") as f:
        json.dump({"status": status, "scriptSha256": sha, "map": B.MAP_PATH, "counts": counts or {},
                   "errors": ERRORS}, f, indent=2)


def actors_by_label():
    return {a.get_actor_label(): a for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()}


def need(found, label):
    if label not in found:
        B.fail("missing " + label)
    return found[label]


def polish_lighting(found):
    sky = need(found, "PSL_SkyLight").get_component_by_class(unreal.SkyLightComponent)
    sky.set_editor_property("intensity", 1.3)
    sun = need(found, "PSL_Sun").get_component_by_class(unreal.DirectionalLightComponent)
    sun.set_editor_property("intensity", 7.0)
    sun.set_editor_property("contact_shadow_length", 0.05)
    pp = need(found, "PSL_PostProcess")
    s = pp.get_editor_property("settings")
    s.set_editor_property("override_auto_exposure_bias", True)
    s.set_editor_property("auto_exposure_bias", 0.0)
    pp.set_editor_property("settings", s)


def tree_positions(found):
    out = []
    for c in need(found, "PSL_Foliage_Trees").get_components_by_class(unreal.HierarchicalInstancedStaticMeshComponent):
        for i in range(c.get_instance_count()):
            t = c.get_instance_transform(i, True).translation
            out.append((t.x, t.y))
    return out


def replace_vegetation(found, grid, poly, stems):
    """Destroy and re-create only the grass/bush holders and the spawn-rock landmark."""
    subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    trees = tree_positions(found)
    for label in ("PSL_Foliage_Grass", "PSL_Foliage_Bushes", SPAWN_ROCKS):
        if label in found:
            subsystem.destroy_actor(found[label])
    rng = random.Random(B.SEED + 9)
    free = lambda x, y: not B.excluded(x, y, poly)
    anchors = edge_anchors(poly, trees, rng)
    z_of = lambda x, y: B.sample_height(grid, x, y)
    bushes = B.pick(stems, r"^(Bush|Fern|Plant)")
    grass = B.pick(stems, r"^(Grass|Flower|Clover)")
    counts = {
        "grass": B.holder("PSL_Foliage_Grass", B.split(grass, clustered_points(rng, GRASS_MAX, anchors, free), rng, z_of,
                                                       (0.8, 1.3)), False, 9000.0),
        "bushes": B.holder("PSL_Foliage_Bushes", B.split(bushes, clustered_points(rng, BUSH_MAX, anchors, free), rng, z_of,
                                                         (0.8, 1.3)), False, 12000.0),
    }
    if counts["grass"] > GRASS_MAX or counts["bushes"] > BUSH_MAX:
        B.fail("vegetation over budget: %r" % (counts,))
    start = need(found, "PSL_PlayerStart").get_actor_location()
    rocks = place_rocks(poly, (start.x, start.y), rng)
    rock = B.pick(stems, r"^Rock_Medium_1$")[0]
    actor = B.spawn(unreal.Actor, SPAWN_ROCKS)
    mesh = asset_or_fail(B.NATURE_DEST + "/SM_" + rock)
    ts = []
    for rx, ry, yaw, s in rocks:
        ts.append(B.transform(rx, ry, z_of(rx, ry), yaw, s))
    if unreal.PSLLevelBuildLibrary.add_mesh_instances(actor, mesh, ts, True, 0.0) is None:
        B.fail("spawn rocks failed")
    return counts


def verify(counts):
    c, problems = B.check()
    counts.update(c)
    for label in ("PSL_Foliage_Grass", "PSL_Foliage_Bushes"):
        limit = GRASS_MAX if label.endswith("Grass") else BUSH_MAX
        if c.get(label, 0) > limit:
            problems.append("%s over budget: %d > %d" % (label, c[label], limit))
    if SPAWN_ROCKS not in actors_by_label():
        problems.append("missing " + SPAWN_ROCKS)
    if not B.exists(MASK_PATH):
        problems.append("missing " + MASK_PATH)
    trex = unreal.load_asset(B.PREY_DEST.replace("Prey", "Dino") + "/SK_Trex")
    for sm in trex.get_editor_property("materials"):
        mi = sm.get_editor_property("material_interface")
        if mi is None or not mi.get_name().startswith("MI_Trex_"):
            problems.append("SK_Trex slot %s not using MI_Trex_*" % sm.get_editor_property("material_slot_name"))
    chars = unreal.load_asset(CHAR_DEST + "/M_PSL_Character")
    if not chars.get_editor_property("used_with_skeletal_mesh"):
        problems.append("M_PSL_Character lacks used_with_skeletal_mesh")
    for d in (TREX_MI_DEST, PARA_MI_DEST):
        for p in unreal.EditorAssetLibrary.list_assets(d, False, False):
            mi = unreal.load_asset(p)
            par = mi.get_editor_property("parent")
            if par is None or par.get_name() != "M_PSL_Character":
                problems.append("%s parent is not M_PSL_Character" % mi.get_name())
    used = [t.get_name() for t in mel().get_used_textures(unreal.load_asset(B.MAT_DEST + "/M_PSL_Terrain"))]
    for bad in ("Grass", "PathRocks_Diffuse"):
        if bad in used:
            problems.append("M_PSL_Terrain still samples " + bad)
    if problems:
        B.fail("; ".join(problems))


def make_writable():
    """Content is checked out read-only; unlock only the files this script saves (never L_TestArena)."""
    content = os.path.join(B.ROOT, "Content")
    targets = ["Characters/Dino/SK_Trex.uasset", "Characters/Prey/SK_Parasaurolophus.uasset", "Maps/L_LostValley.umap",
               "Environment/Nature/Textures/%s.uasset" % MASK_NAME]
    dirs = ["Characters/Materials", "Characters/Dino/Materials", "Characters/Prey/Materials", "Environment/Materials"]
    for d in dirs:
        full = os.path.join(content, d)
        if os.path.isdir(full):
            targets += [os.path.join(d, f) for f in os.listdir(full)]
    for t in targets:
        p = os.path.join(content, t)
        if os.path.isfile(p):
            os.chmod(p, 0o644)


def main():
    if B.MAP_PATH.endswith("L_TestArena"):
        B.fail("refusing to touch L_TestArena")
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not B.exists(B.MAP_PATH):
        B.fail(B.MAP_PATH + " missing: run build_lost_valley.py first")
    make_writable()
    parent = build_character_material()
    trex = assign_character_mis("/Game/Characters/Dino/SK_Trex", "Trex", TREX_MI_DEST, parent, TREX_PALETTE, TREX_DEFAULT)
    para = assign_character_mis(B.PREY_DEST + "/SK_Parasaurolophus", "Para", PARA_MI_DEST, parent, PARA_PALETTE, PARA_DEFAULT)
    build_terrain_material(import_mask())
    if not levels.load_level(B.MAP_PATH):
        B.fail("load_level failed for " + B.MAP_PATH)
    found = actors_by_label()
    polish_lighting(found)
    poly = B.catmull_rom(B.PATH_POINTS)
    _, grid = B.build_heights(poly)
    stems = sorted({n.split(".")[0].split("/")[-1][3:] for n in unreal.EditorAssetLibrary.list_assets(B.NATURE_DEST, False, False)
                    if n.split("/")[-1].startswith("SM_")})
    counts = replace_vegetation(found, grid, poly, stems)
    if not levels.save_current_level():
        B.fail("save_current_level failed")
    counts.update({"trexMaterials": len(trex), "paraMaterials": len(para)})
    verify(counts)
    write_report("ok", counts)
    unreal.log(LOG + "done " + json.dumps(counts))


def run():
    global unreal
    try:
        import unreal as _u  # type: ignore  # only available inside the Unreal editor
        unreal = B.unreal = _u
        B.ERRORS = ERRORS  # B.fail() appends here and writes through our report (right sha, same json)
        B.write_report = write_report
        main()
    except SystemExit:
        raise
    except ImportError as exc:
        B.fail("unreal module unavailable (run inside the editor, or use --selftest): %r" % (exc,))
    except Exception as exc:  # unreal raises plain Exception subclasses
        B.fail("unexpected error: %r" % (exc,))


if "--selftest" in sys.argv:
    selftest()
else:
    run()
