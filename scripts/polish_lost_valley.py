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
MASK_CORE, MASK_EDGE, MASK_JITTER, MASK_NOISE_CELL = 150.0, 950.0, 0.35, 700.0
LAND_SIZE = (B.SIZE_VERTS - 1) * B.VERT_STEP
GRASS_MAX, BUSH_MAX = 2500, 300  # budgets: never above the build script's counts
SPAWN_ROCKS = "PSL_Landmark_SpawnRocks"

# slot name keyword -> linear albedo. Luminance floor 0.15 (spec B1).
TREX_PALETTE = {"black": (0.24, 0.17, 0.11), "green": (0.30, 0.30, 0.12), "lightgreen": (0.42, 0.38, 0.18),
                "lightyellow": (0.62, 0.55, 0.38), "red": (0.40, 0.18, 0.09)}
TREX_DEFAULT = (0.33, 0.30, 0.14)
# R-B >= 0.25 on every entry (selftest) so prey read rust/ochre, not blue-grey, in the valley.
# black is R=0.38 (spec said 0.36 -> R-B 0.24, fails the spec's own >= 0.25 assert).
PARA_PALETTE = {"green": (0.70, 0.38, 0.14), "lightgreen": (0.78, 0.50, 0.22), "lightyellow": (0.86, 0.68, 0.40),
                "red": (0.62, 0.20, 0.08), "black": (0.38, 0.22, 0.12)}
PARA_DEFAULT = PARA_PALETTE["green"]


def luminance(c):
    return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]


def palette_color(palette, default, slot):
    return palette.get(re.sub(r"[^a-z]", "", slot.lower()), default)


# ---------------------------------------------------------------- pure helpers
def mask_pixels(poly, res=MASK_RES):
    """res*res bytes, row-major (v = y fastest-changing is x). 1 within MASK_CORE of the path, soft noise-broken falloff
    to 0 at MASK_EDGE (distance scaled by 1 + MASK_JITTER*noise, noise in [-1, 1], so the edge wanders instead of tracing a
    smooth ribbon), plus 0.35*noise inside the clearing. The mask has its own radii: B.PATH_FLAT_RADIUS/PATH_BLEND_RADIUS
    drive the height flattening and must not change. Pixel (i, j) covers world (origin + (i+.5)/res*size, ...)."""
    r_out = MASK_EDGE / (1.0 - MASK_JITTER)  # farthest a pixel can still be non-zero
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
            d = dist[j][i] * (1.0 + MASK_JITTER * B.value_noise(x, y, MASK_NOISE_CELL))  # 0 stays 0: path core stays solid
            v = 1.0 - B.smoothstep(MASK_CORE, MASK_EDGE, d)
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
    nz = sum(1 for v in px if v)
    assert 0 < nz < 128 * 128 // 4, "mask coverage implausible: %d px" % nz
    assert px[j * 128 + i] == 255 and max(px[0], px[-1]) == 0, "mask core/far values"
    decor = [s for s in ("Bush_Common", "Bush_Common_Flowers", "Fern_1", "Plant_1_Big", "Plant_7_Big", "Grass_Common_Tall")
             if re.search(B.DECOR_BUSH_RE, s)]
    assert decor == ["Bush_Common", "Fern_1", "Plant_1_Big", "Plant_7_Big"], decor
    assert B.DECOR_BUSH_SCALE[1] <= 1.05 and B.PATH_FLAT_RADIUS == 250.0 and B.PATH_BLEND_RADIUS == 650.0
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
    for c in list(PARA_PALETTE.values()) + [PARA_DEFAULT]:
        assert c[0] - c[2] >= 0.25, "prey colour not rust/ochre enough: %r" % (c,)
    def keep(u, v, depth):  # mirror of pawn_window + CameraFadeFloor
        d = math.hypot((u - PAWN_CENTER[0]) / PAWN_RADIUS[0], (v - PAWN_CENTER[1]) / PAWN_RADIUS[1])
        sat = lambda x: min(max(x, 0.0), 1.0)
        return max(sat((d - 1.0) / PAWN_SOFT), sat((depth - PAWN_FADE_DEPTH) / PAWN_DEPTH_RAMP), CAMERA_FADE_FLOOR)
    assert keep(*PAWN_CENTER, 600.0) == CAMERA_FADE_FLOOR and keep(0.425, 0.65, 660.0) < 0.5, "occluder over the pawn not thinned"
    assert keep(0.9, 0.3, 600.0) == 1.0 and keep(0.1, 0.8, 600.0) == 1.0, "pawn window leaks to the periphery"
    assert keep(*PAWN_CENTER, PAWN_FADE_DEPTH + PAWN_DEPTH_RAMP) == 1.0, "foliage behind the pawn faded"
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
        # Wipe from a snapshot and check the count: delete_all_material_expressions iterates the live expression array
        # (UE 5.8 MaterialEditingLibrary.cpp:559-568) and left an orphan PathRocks_Diffuse sample in C11 [INFERENCE: skipped entries].
        for e in list(mel().get_material_expressions(mat)):
            mel().delete_material_expression(mat, e)
        if mel().get_num_material_expressions(mat):
            B.fail("could not wipe %s: %d expressions left" % (path, mel().get_num_material_expressions(mat)))
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
    frac = const(mat, 0.7, -900, 90)
    desat = node(mat, unreal.MaterialExpressionDesaturation, -800, 100)
    link(color, "", desat, "")
    link(frac, "", desat, "Fraction")
    dark = mul(mat, desat, const(mat, 0.7, -900, 120), -650, 60)  # Dead: readable grey-brown, lighter than its shadow
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
    # soft, noise-broken edge: R + (n - 0.5) * 2 * R * (1 - R), so the solid core (R=1) and far grass (R=0) are untouched
    # and only the falloff band is eroded into a ragged edge instead of a smooth ribbon
    edge_n = node(mat, unreal.MaterialExpressionNoise, -1600, 700, scale=0.012, output_min=0.0, output_max=1.0)
    link(wpos, "", edge_n, "Position")
    edge_c = node(mat, unreal.MaterialExpressionSubtract, -1400, 700)
    link(edge_n, "", edge_c, "A")
    link(const(mat, 0.5, -1600, 800), "", edge_c, "B")
    omr = node(mat, unreal.MaterialExpressionSubtract, -1400, 450)
    link(const(mat, 1.0, -1600, 450), "", omr, "A")
    link(path, "R", omr, "B")
    shape = mul(mat, omr, path, -1200, 450, bo="R")
    pert = mul(mat, mul(mat, shape, edge_c, -1000, 700), const(mat, 2.0, -1200, 750), -800, 700)
    path_soft = node(mat, unreal.MaterialExpressionAdd, -800, 500)
    link(path, "R", path_soft, "A")
    link(pert, "", path_soft, "B")
    dirt_a = node(mat, unreal.MaterialExpressionSaturate, -800, 400)
    sum_a = node(mat, unreal.MaterialExpressionAdd, -1000, 400)
    link(path_soft, "", sum_a, "A")
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
    # large-scale tiling break: the SAME Rocks_Diffuse at 1/3000 (one tile = 30 m), luminance centred on 1 and scaled by
    # the MacroVariation parameter, so patches of lighter/darker ground read at camera distance (MI-tunable)
    macro_lum = node(mat, unreal.MaterialExpressionDotProduct, -1450, 1300)
    link(tex_at("Rocks_Diffuse", 1.0 / 3000.0, 1300), "RGB", macro_lum, "A")
    link(vec3(0.3, 0.59, 0.11, 1400), "", macro_lum, "B")
    macro_centred = node(mat, unreal.MaterialExpressionSubtract, -1250, 1300)
    link(macro_lum, "", macro_centred, "A")
    link(const(mat, 0.5, -1450, 1450), "", macro_centred, "B")
    macro = node(mat, unreal.MaterialExpressionAdd, -600, 1300)
    link(const(mat, 1.0, -800, 1300), "", macro, "A")
    link(mul(mat, mul(mat, macro_centred, const(mat, 2.0, -1250, 1450), -1050, 1350),
             scalar_param(mat, "MacroVariation", 0.18, -1050, 1500), -800, 1400), "", macro, "B")
    # gentle slope tint from ~8 degrees: warmer/darker earth tone on tilted ground (the hard rock switch stays at 25)
    tilt = node(mat, unreal.MaterialExpressionSaturate, -600, 1500)
    tdiff = node(mat, unreal.MaterialExpressionSubtract, -1000, 1500)
    link(const(mat, math.cos(math.radians(8.0)), -1200, 1500), "", tdiff, "A")
    link(nz, "", tdiff, "B")
    link(mul(mat, tdiff, const(mat, 6.0, -1000, 1600), -800, 1500), "", tilt, "")
    tint = node(mat, unreal.MaterialExpressionLinearInterpolate, -400, 1500)
    link(vec3(1.0, 1.0, 1.0, 1500), "", tint, "A")
    link(vec3(0.88, 0.82, 0.70, 1600), "", tint, "B")
    link(tilt, "", tint, "Alpha")
    final = mul(mat, mul(mat, mul(mat, surface, bright, -200, 0), macro, -100, 0), tint, 0, 0)
    mel().connect_material_property(final, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel().connect_material_property(const(mat, 0.9, -200, 300), "", unreal.MaterialProperty.MP_ROUGHNESS)
    B.finish_material(mat)
    return mat


FADE_START, FADE_RANGE = 250.0, 300.0  # camera fade: opaque beyond START+RANGE uu, gone at START uu
# Pawn window (ScreenPosition.ViewportUV; the pawn sits left of centre from SocketOffset Y=70: torso ~(0.425, 0.80) at 1280x720).
# Foliage inside the ellipse and in front of the pawn's far side (arm 650 -> pawn ~660 uu deep; M2f1 occluders sat at ~550-660)
# dithers down to CAMERA_FADE_FLOOR instead of vanishing: the plant still reads but no longer hides the dinosaur. Foliage
# beyond PAWN_FADE_DEPTH + PAWN_DEPTH_RAMP (behind the pawn) and everything outside the ellipse keep their full cutout.
PAWN_CENTER, PAWN_RADIUS, PAWN_SOFT = (0.425, 0.80), (0.15, 0.28), 0.3
PAWN_FADE_DEPTH, PAWN_DEPTH_RAMP, CAMERA_FADE_FLOOR = 680.0, 150.0, 0.15
PAWN_PARAMS = ("CameraFadeCenterX", "CameraFadeCenterY", "CameraFadeRadiusX", "CameraFadeRadiusY", "PawnFadeSoftness",
               "PawnFadeDepth", "PawnFadeDepthRamp", "CameraFadeFloor")
LEAF_GAIN = 1.05  # M2f1's 1.3 plus the brighter sky washed bushes to near-white (RGB ~245); 1.05 keeps them sage green
FOOD_FLOWER_MI = "MI_PSL_FoodFlower"
FOOD_FLOWER_COLOR = (0.82, 0.22, 0.32)  # warm berry/blossom, reads against green foliage
LEAF_CLIP = 0.3333  # M_PSL_Leaf's existing OpacityMaskClipValue (Material.h:536 default)


def foliage_opacity(mat, alpha=None, alpha_out="A"):
    """Wire OpacityMask for a masked + dithered foliage material that fades out near the camera.

    Engine math (MaterialTemplate.ush:4569-4572 + BlueNoiseCommon.ush:19-26): with DitherOpacityMask the pixel is kept when
    (Mask - ClipValue) + 0.83 * blueNoise(0..1) - 0.5 > 0. With ClipValue = 0.3333 that is deterministic at Mask = 1 (always
    kept: 0.1667 + 0.83n > 0) and Mask = 0 (never kept: max -0.0033), and a screen-door ramp in between. So:
      cut  = saturate((TexAlpha - 0.3333) * 1000)  -> the original hard leaf cutout (alpha > 0.3333), 0 or 1
      Mask = cut * min(Fade, Keep), Fade = saturate((PixelDepth - CameraFadeStart) / CameraFadeRange)
      Keep = max(OutsidePawn, BehindPawn, CameraFadeFloor)
        OutsidePawn = saturate((distance(UV / Radius, Center / Radius) - 1) / PawnFadeSoftness)  (0 inside the pawn ellipse)
        BehindPawn  = saturate((PixelDepth - PawnFadeDepth) / PawnFadeDepthRamp)              (1 beyond the pawn)
    Far foliage (Fade = 1, Keep = 1) keeps exactly the old cutout; near foliage dithers out; foliage between the camera and
    the pawn on screen keeps ~CameraFadeFloor coverage. ShadowReplace feeds the shadow pass `cut` alone
    (GetMaterialClippingShadowDepth clips on Mask - ClipValue, MaterialTemplate.ush:4542-4546; shadow flag Common.ush:2290-2297),
    so neither fade changes shadow depth. alpha=None (no texture) means cut = 1."""
    depth = node(mat, unreal.MaterialExpressionPixelDepth, -1400, 1600)
    rel = node(mat, unreal.MaterialExpressionSubtract, -1200, 1600)
    link(depth, "", rel, "A")
    link(scalar_param(mat, "CameraFadeStart", FADE_START, -1400, 1700), "", rel, "B")
    ratio = node(mat, unreal.MaterialExpressionDivide, -1000, 1600)
    link(rel, "", ratio, "A")
    link(scalar_param(mat, "CameraFadeRange", FADE_RANGE, -1200, 1800), "", ratio, "B")
    fade = node(mat, unreal.MaterialExpressionSaturate, -800, 1600)
    link(ratio, "", fade, "")
    keep = node(mat, unreal.MaterialExpressionMax, -800, 2000)
    link(pawn_window(mat, depth), "", keep, "A")
    link(scalar_param(mat, "CameraFadeFloor", CAMERA_FADE_FLOOR, -1000, 2200), "", keep, "B")
    vis = node(mat, unreal.MaterialExpressionMin, -600, 1700)
    link(fade, "", vis, "A")
    link(keep, "", vis, "B")
    if alpha is None:
        cut = const(mat, 1.0, -800, 1500)
    else:
        over = node(mat, unreal.MaterialExpressionSubtract, -1200, 1400)
        link(alpha, alpha_out, over, "A")
        link(const(mat, LEAF_CLIP, -1400, 1450), "", over, "B")
        cut = node(mat, unreal.MaterialExpressionSaturate, -800, 1400)
        link(mul(mat, over, const(mat, 1000.0, -1200, 1500), -1000, 1400), "", cut, "")
    shadow_switch = node(mat, unreal.MaterialExpressionShadowReplace, -400, 1500)
    link(mul(mat, cut, vis, -600, 1550), "", shadow_switch, "Default")
    link(cut, "", shadow_switch, "Shadow")
    mel().connect_material_property(shadow_switch, "", unreal.MaterialProperty.MP_OPACITY_MASK)


def pawn_window(mat, depth):
    """max(OutsidePawn, BehindPawn) from foliage_opacity's docstring; 0 only for foliage in front of the pawn on screen."""
    def vec2(px, py, *names_values):
        v = node(mat, unreal.MaterialExpressionAppendVector, px, py)
        for pin, (name, value) in zip(("A", "B"), names_values):
            link(scalar_param(mat, name, value, px - 200, py + (0 if pin == "A" else 100)), "", v, pin)
        return v
    radius = vec2(-1600, 2100, ("CameraFadeRadiusX", PAWN_RADIUS[0]), ("CameraFadeRadiusY", PAWN_RADIUS[1]))
    uv = node(mat, unreal.MaterialExpressionDivide, -1400, 1900)
    link(node(mat, unreal.MaterialExpressionScreenPosition, -1600, 1900), "ViewportUV", uv, "A")
    link(radius, "", uv, "B")
    centre = node(mat, unreal.MaterialExpressionDivide, -1400, 2000)
    link(vec2(-1600, 2000, ("CameraFadeCenterX", PAWN_CENTER[0]), ("CameraFadeCenterY", PAWN_CENTER[1])), "", centre, "A")
    link(radius, "", centre, "B")
    dist = node(mat, unreal.MaterialExpressionDistance, -1200, 1950)
    link(uv, "", dist, "A")
    link(centre, "", dist, "B")
    out_rel = node(mat, unreal.MaterialExpressionSubtract, -1100, 1950)
    link(dist, "", out_rel, "A")
    link(const(mat, 1.0, -1200, 2050), "", out_rel, "B")
    out_ratio = node(mat, unreal.MaterialExpressionDivide, -1000, 1950)
    link(out_rel, "", out_ratio, "A")
    link(scalar_param(mat, "PawnFadeSoftness", PAWN_SOFT, -1200, 2100), "", out_ratio, "B")
    outside = node(mat, unreal.MaterialExpressionSaturate, -900, 1950)
    link(out_ratio, "", outside, "")
    behind_rel = node(mat, unreal.MaterialExpressionSubtract, -1200, 2300)
    link(depth, "", behind_rel, "A")
    link(scalar_param(mat, "PawnFadeDepth", PAWN_FADE_DEPTH, -1400, 2300), "", behind_rel, "B")
    behind_ratio = node(mat, unreal.MaterialExpressionDivide, -1000, 2300)
    link(behind_rel, "", behind_ratio, "A")
    link(scalar_param(mat, "PawnFadeDepthRamp", PAWN_DEPTH_RAMP, -1200, 2400), "", behind_ratio, "B")
    behind = node(mat, unreal.MaterialExpressionSaturate, -900, 2300)
    link(behind_ratio, "", behind, "")
    window = node(mat, unreal.MaterialExpressionMax, -800, 2100)
    link(outside, "", window, "A")
    link(behind, "", window, "B")
    return window


def masked_dither_setup(mat):
    mat.set_editor_property("two_sided", True)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    mat.set_editor_property("dither_opacity_mask", True)
    mat.set_editor_property("opacity_mask_clip_value", LEAF_CLIP)


def build_leaf_material():
    """M_PSL_Leaf in place (the Leaf MIs keep their Tex override): Tex.RGB * LeafGain, camera-faded dithered mask."""
    mat = fresh_material("M_PSL_Leaf", B.MAT_DEST)
    masked_dither_setup(mat)
    tex = node(mat, unreal.MaterialExpressionTextureSampleParameter2D, -1700, 0, parameter_name="Tex")
    color = mul(mat, tex, scalar_param(mat, "LeafGain", LEAF_GAIN, -1700, 200), -1400, 0, ao="RGB")
    foliage_opacity(mat, tex)
    mel().connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel().connect_material_property(const(mat, 0.85, -1400, 300), "", unreal.MaterialProperty.MP_ROUGHNESS)
    B.finish_material(mat, instanced=True)
    return mat


def build_foliage_flat_material():
    """M_PSL_FoliageFlat: M_PSL_Flat (VectorParameter Color, roughness 0.85) with the same camera-faded dithered mask."""
    mat = fresh_material("M_PSL_FoliageFlat", B.MAT_DEST)
    masked_dither_setup(mat)
    color = node(mat, unreal.MaterialExpressionVectorParameter, -1400, 0, parameter_name="Color",
                 default_value=unreal.LinearColor(0.5, 0.5, 0.5, 1.0))
    foliage_opacity(mat)
    mel().connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel().connect_material_property(const(mat, 0.85, -1400, 300), "", unreal.MaterialProperty.MP_ROUGHNESS)
    B.finish_material(mat, instanced=True)
    return mat


def reparent_grass_flower(flat_foliage):
    """MI_PSL_Grass / MI_PSL_Flower move to M_PSL_FoliageFlat (their Color override is kept); bark/rock/stones stay on M_PSL_Flat."""
    for name in ("MI_PSL_Grass", "MI_PSL_Flower"):
        mi = asset_or_fail(B.MAT_DEST + "/" + name)
        mi.set_editor_property("parent", flat_foliage)
        if not unreal.EditorAssetLibrary.save_loaded_asset(mi):
            B.fail("cannot save " + name)


def build_food_flower_mi(flat_foliage):
    """Parent M_PSL_FoliageFlat (not M_PSL_Flat) so the blossoms fade with the leaves of the same plant."""
    path = B.MAT_DEST + "/" + FOOD_FLOWER_MI
    mi = unreal.load_asset(path) if B.exists(path) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        FOOD_FLOWER_MI, B.MAT_DEST, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    if mi is None:
        B.fail("cannot create material instance " + path)
    mi.set_editor_property("parent", flat_foliage)
    mel().set_material_instance_vector_parameter_value(mi, "Color", unreal.LinearColor(*FOOD_FLOWER_COLOR, 1.0))
    if not unreal.EditorAssetLibrary.save_loaded_asset(mi):
        B.fail("cannot save " + path)
    return mi


def food_plants(found):
    plant_class = unreal.load_class(None, B.FOOD_PLANT_CLASS)
    return [found[l] for l in B.FOOD_PLANT_LABELS if l in found and found[l].get_class() == plant_class]


def flower_slots(mesh):
    """Indices of the mesh slots whose slot name says flower (SM_Bush_Common_Flowers: 'Flowers' and 'Leaves_NormalTree', from
    the FBX material names). Same keyword table as B.slot_kind; the mesh stem is left out because it contains 'Flowers'."""
    return [i for i, sm in enumerate(mesh.get_editor_property("static_materials"))
            if B.slot_kind(str(sm.get_editor_property("material_slot_name")), "") == "flower"]


def apply_food_flower(found, mi):
    """Per-component override on each food plant's flower slot(s) only; leaves keep their normal material."""
    plants = food_plants(found)
    if len(plants) != len(B.FOOD_PLANT_LABELS):
        B.fail("expected %d food plants, found %d" % (len(B.FOOD_PLANT_LABELS), len(plants)))
    for plant in plants:
        comp = plant.get_editor_property("plant_mesh")
        slots = flower_slots(comp.get_editor_property("static_mesh"))
        if not slots:
            B.fail("%s: no flower slot among %s" % (plant.get_actor_label(), [
                str(sm.get_editor_property("material_slot_name")) for sm in comp.get_editor_property("static_mesh").get_editor_property("static_materials")]))
        for i in slots:
            comp.set_material(i, mi)
    return len(plants)


def expression_exists(mat, cls, name=None):
    return any(isinstance(e, cls) and (name is None or str(e.get_editor_property("parameter_name")) == name)
               for e in mel().get_material_expressions(mat))


def foliage_material_problems(mat):
    out, n = [], mat.get_name()
    if mat.get_editor_property("blend_mode") != unreal.BlendMode.BLEND_MASKED:
        out.append(n + " is not masked")
    if not mat.get_editor_property("dither_opacity_mask"):
        out.append(n + " lacks dither_opacity_mask")
    if not mat.get_editor_property("two_sided"):
        out.append(n + " is not two sided")
    for param in ("CameraFadeStart",) + PAWN_PARAMS:
        if not expression_exists(mat, unreal.MaterialExpressionScalarParameter, param):
            out.append("%s lacks %s param" % (n, param))
    for cls in (unreal.MaterialExpressionPixelDepth, unreal.MaterialExpressionScreenPosition, unreal.MaterialExpressionShadowReplace):
        if not expression_exists(mat, cls):
            out.append("%s lacks %s" % (n, cls.__name__))
    return out


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
    B.configure_sky(sky)
    sun = need(found, "PSL_Sun").get_component_by_class(unreal.DirectionalLightComponent)
    sun.set_editor_property("intensity", 7.0)
    sun.set_editor_property("contact_shadow_length", 0.05)
    pp = need(found, "PSL_PostProcess")
    s = pp.get_editor_property("settings")
    s.set_editor_property("override_auto_exposure_bias", True)
    s.set_editor_property("auto_exposure_bias", 0.0)
    s.set_editor_property("override_motion_blur_amount", True)
    s.set_editor_property("motion_blur_amount", 0.0)
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
    bushes = B.pick(stems, B.DECOR_BUSH_RE)
    grass = B.pick(stems, r"^(Grass|Flower|Clover)")
    counts = {
        "grass": B.holder("PSL_Foliage_Grass", B.split(grass, clustered_points(rng, GRASS_MAX, anchors, free), rng, z_of,
                                                       (0.8, 1.3)), False, 9000.0),
        "bushes": B.holder("PSL_Foliage_Bushes", B.split(bushes, clustered_points(rng, BUSH_MAX, anchors, free), rng, z_of,
                                                         B.DECOR_BUSH_SCALE), False, 12000.0),
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


def terrain_texture_names(mat):
    """{texture asset name: source} for every texture the live M_PSL_Terrain graph references: expression walk plus
    MaterialEditingLibrary.get_material_used_textures (get_used_textures alone is deprecated and was not trusted).
    The asset registry is not consulted (in C11 a PathRocks_Diffuse hit appeared that may have come from its stored
    dependencies, unconfirmed); the failure message names the source of any bad hit so it can be diagnosed."""
    found = {}
    base = lambda p: str(p).split(".")[0].split("/")[-1]
    lib = mel()
    for e in lib.get_material_expressions(mat):
        t = e.get_editor_property("texture") if isinstance(e, unreal.MaterialExpressionTextureBase) else None
        if t:
            found.setdefault(base(t.get_path_name()), "expression:" + e.get_name())
    for t in lib.get_material_used_textures(mat):
        if t:
            found.setdefault(base(t.get_path_name()), "used_textures")
    return found


def ensure_clouds(found):
    """Spawn PSL_Clouds only if missing (a rerun never duplicates it)."""
    if B.CLOUDS_LABEL not in found:
        B.spawn_clouds()


def foliage_verify(counts):
    """Readability-pass checks (kept out of B.check(): a failing check() makes build_lost_valley.py rebuild the map)."""
    problems = []
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    found = {a.get_actor_label(): a for a in actors}
    for name in ("M_PSL_Leaf", "M_PSL_FoliageFlat"):
        problems += foliage_material_problems(asset_or_fail(B.MAT_DEST + "/" + name))
    for name in ("MI_PSL_Grass", "MI_PSL_Flower"):
        par = asset_or_fail(B.MAT_DEST + "/" + name).get_editor_property("parent")
        if par is None or par.get_name() != "M_PSL_FoliageFlat":
            problems.append("%s parent is %s, expected M_PSL_FoliageFlat" % (name, par.get_name() if par else None))
    max_scale, flowering = 0.0, 0
    for c in need(found, "PSL_Foliage_Bushes").get_components_by_class(unreal.HierarchicalInstancedStaticMeshComponent):
        if c.get_editor_property("static_mesh").get_name() == "SM_" + B.FOOD_PLANT_MESH:
            flowering += c.get_instance_count()
        for i in range(c.get_instance_count()):
            s = c.get_instance_transform(i, False).scale3d
            max_scale = max(max_scale, s.x, s.y, s.z)
    if flowering:
        problems.append("%d decor bush instances use SM_%s" % (flowering, B.FOOD_PLANT_MESH))
    if max_scale > B.DECOR_BUSH_SCALE[1] + 1e-3:
        problems.append("decor bush scale %.3f > %.2f" % (max_scale, B.DECOR_BUSH_SCALE[1]))
    for plant in food_plants(found):
        mats = [m.get_name() if m else None for m in plant.get_editor_property("plant_mesh").get_materials()]
        if FOOD_FLOWER_MI not in mats:
            problems.append("%s lacks %s (materials %s)" % (plant.get_actor_label(), FOOD_FLOWER_MI, mats))
    clouds = [a for a in actors if a.get_actor_label() == B.CLOUDS_LABEL and isinstance(a, unreal.VolumetricCloud)]
    counts["clouds"], counts["decorBushMaxScale"], counts["decorBushFlowering"] = len(clouds), round(max_scale, 3), flowering
    if len(clouds) != 1 or sum(1 for a in actors if a.get_actor_label() == B.CLOUDS_LABEL) != 1:
        problems.append("%s count %d, expected exactly 1 VolumetricCloud" % (B.CLOUDS_LABEL, len(clouds)))
    for cloud in clouds:
        cc = cloud.get_component_by_class(unreal.VolumetricCloudComponent)
        scales = [round(cc.get_editor_property(p), 3) for p in ("view_sample_count_scale", "reflection_view_sample_count_scale_value")]
        counts["cloudSampleScales"] = scales
        if scales[0] > 0.376 or scales[1] > 0.11:
            problems.append("%s sample scales %s exceed (0.375, 0.11)" % (B.CLOUDS_LABEL, scales))
    if need(found, "PSL_SkyLight").get_component_by_class(unreal.SkyLightComponent).get_editor_property("lower_hemisphere_is_black"):
        problems.append("PSL_SkyLight lower hemisphere is black")
    return problems


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
    tex = terrain_texture_names(unreal.load_asset(B.MAT_DEST + "/M_PSL_Terrain"))
    if not tex:
        problems.append("M_PSL_Terrain references zero textures")
    for need_tex in ("Rocks_Diffuse", MASK_NAME):
        if need_tex not in tex:
            problems.append("M_PSL_Terrain does not sample " + need_tex)
    for bad in ("Grass", "PathRocks_Diffuse"):
        if bad in tex:
            problems.append("M_PSL_Terrain still samples %s (via %s; all: %s)" % (bad, tex[bad], sorted(tex)))
    problems += foliage_verify(counts)
    if not expression_exists(unreal.load_asset(B.MAT_DEST + "/M_PSL_Terrain"), unreal.MaterialExpressionScalarParameter, "MacroVariation"):
        problems.append("M_PSL_Terrain lacks MacroVariation")
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
    build_leaf_material()
    flat_foliage = build_foliage_flat_material()
    reparent_grass_flower(flat_foliage)
    food_flower = build_food_flower_mi(flat_foliage)
    if not levels.load_level(B.MAP_PATH):
        B.fail("load_level failed for " + B.MAP_PATH)
    found = actors_by_label()
    polish_lighting(found)
    poly = B.catmull_rom(B.PATH_POINTS)
    _, grid = B.build_heights(poly)
    stems = sorted({n.split(".")[0].split("/")[-1][3:] for n in unreal.EditorAssetLibrary.list_assets(B.NATURE_DEST, False, False)
                    if n.split("/")[-1].startswith("SM_")})
    counts = replace_vegetation(found, grid, poly, stems)
    counts["foodFlowerPlants"] = apply_food_flower(found, food_flower)
    ensure_clouds(found)
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
