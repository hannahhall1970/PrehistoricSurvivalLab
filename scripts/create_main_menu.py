"""Create /Game/Maps/L_MainMenu: the 3D main-menu scene behind the UMG menu.

Run (only under the harness build lock, no other Unreal job running):
    UnrealEditor-Cmd PrehistoricSurvivalLab.uproject -EnablePlugins=PythonScriptPlugin \
      -ExecutePythonScript=<abs>/scripts/create_main_menu.py -ScriptErrorsAreFatal -unattended -nosplash

Pure placement/framing checks run without `unreal`:  python3 scripts/create_main_menu.py --selftest

Contents (project assets only; nothing imported):
- ground plane with M_PSL_Terrain, golden-hour lighting (movable sun, SkyAtmosphere, real-time SkyLight, height fog,
  unbound post process with fixed exposure),
- a seeded HISM backdrop (treeline, distant rock cliffs, mid rocks, bushes, grass) from /Game/Environment/Nature,
- one APSLMenuPreviewActor right of the left-side UI panel, facing 3/4 toward it, and one APSLMenuCameraRig framing it,
- the world-settings GameMode override APSLMenuGameMode.
The existing map is replaced. Saves, reloads and verifies by real actor class; writes Saved/PSL/main-menu.json
{status, scriptSha256, counts, errors} and exits non-zero on any failure.
"""
import hashlib
import json
import math
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
os.environ["PSL_BUILD_IMPORT_ONLY"] = "1"
import build_lost_valley as B  # noqa: E402  (shared spawn/holder/transform/poisson helpers)

LOG = "PSL create_main_menu: "
ERRORS = []
unreal = None  # imported lazily so --selftest runs without the editor
REPORT = os.path.join(B.ROOT, "Saved", "PSL", "main-menu.json")

MAP_PATH = "/Game/Maps/L_MainMenu"
GAME_MODE_CLASS = "/Script/PrehistoricSurvivalLab.PSLMenuGameMode"
RIG_CLASS = "/Script/PrehistoricSurvivalLab.PSLMenuCameraRig"
PREVIEW_CLASS = "/Script/PrehistoricSurvivalLab.PSLMenuPreviewActor"
PLANE_MESH = "/Engine/BasicShapes/Plane.Plane"  # 100 x 100 uu, pivot at center, faces +Z
SEED = 5005

# ---- framing (uu; camera looks down +X, +Y is screen right) ----
# The menu panel covers screen x 0.05..0.50, so the dino sits right of it at about x 0.73.
CAMERA = (-1400.0, 0.0, 200.0)
CAMERA_PITCH = -4.0
FOV = 50.0  # horizontal degrees, 16:9
DINO = (0.0, 300.0)
DINO_YAW = 215.0  # facing the camera and the panel (3/4 view); APSLMenuPreviewActor sways +-20 deg around it
DINO_HEIGHT = 300.0  # APSLMenuPreviewActor::FitHeight
DOLLY_DISTANCE, DOLLY_PERIOD = 60.0, 30.0
GROUND_SCALE = 600.0  # 600 m square

# ---- backdrop budget (instances) ----
BUDGET = {"trees": 70, "cliffs": 6, "rocks": 12, "bushes": 50, "grass": 400}
TREES = ["CommonTree_1", "CommonTree_2", "CommonTree_3", "Pine_1", "Pine_2", "Pine_3"]
ROCKS = ["Rock_Medium_1", "Rock_Medium_2", "Rock_Medium_3"]
BUSHES = ["Bush_Common", "Bush_Common_Flowers", "Fern_1", "Plant_1_Big", "Plant_7_Big"]
GRASS = ["Grass_Common_Short", "Grass_Common_Tall", "Grass_Wispy_Tall", "Flower_3_Group", "Flower_4_Group"]
HOLDERS = ["PSL_Menu_Trees", "PSL_Menu_Cliffs", "PSL_Menu_Rocks", "PSL_Menu_Bushes", "PSL_Menu_Grass"]
LIGHTS = {"PSL_Menu_Sun": "DirectionalLight", "PSL_Menu_SkyAtmosphere": "SkyAtmosphere", "PSL_Menu_SkyLight": "SkyLight",
          "PSL_Menu_Fog": "ExponentialHeightFog", "PSL_Menu_PostProcess": "PostProcessVolume"}


# ---------------------------------------------------------------- pure helpers
def sight_distance(x, y):
    """Distance from (x, y) to the camera->dino segment (ground plane)."""
    return B._seg_dist(x, y, CAMERA[:2], DINO)


def clear_of_dino(x, y, radius):
    return math.hypot(x - DINO[0], y - DINO[1]) >= radius and sight_distance(x, y) >= radius


def screen_x(x, y):
    """Horizontal screen fraction (0 = left, 1 = right) of a ground point, ignoring the small pitch."""
    depth = x - CAMERA[0]
    return 0.5 + 0.5 * (y - CAMERA[1]) / (depth * math.tan(math.radians(FOV / 2.0)))


def screen_height_fraction(height, x):
    """Fraction of the 16:9 screen height an upright object of `height` at depth x - CAMERA[0] covers."""
    half_v = math.tan(math.radians(FOV / 2.0)) * 9.0 / 16.0
    return height / (2.0 * (x - CAMERA[0]) * half_v)


def placements():
    """{category: [(stem, x, y, z, yaw, scale)]}, deterministic for SEED, every category within BUDGET."""
    rng = random.Random(SEED)
    out = {}

    def pick(stems, pts, scale, z=lambda s: 0.0):
        rows = []
        for x, y in pts:
            s = rng.uniform(*scale)
            rows.append((rng.choice(stems), x, y, z(s), rng.uniform(0.0, 360.0), s))
        return rows

    def box(x0, x1, y0, y1):
        return lambda x, y: x0 <= x <= x1 and y0 <= y <= y1

    # Treeline: open meadow straight behind the dino (clean silhouette), trees beyond 3500 uu and framing both sides.
    in_tree_box = box(1200.0, 10000.0, -6500.0, 6500.0)
    tree_ok = lambda x, y: in_tree_box(x, y) and (x >= 3500.0 or abs(y - DINO[1]) >= 1800.0)
    out["trees"] = pick(TREES, B.poisson(rng, BUDGET["trees"], 650.0, tree_ok), (0.9, 1.5))
    # Distant cliffs: big sunk rocks on the horizon, softened by the height fog.
    cliffs = [(rng.uniform(17000.0, 26000.0), -15000.0 + i * 6000.0 + rng.uniform(-1500.0, 1500.0)) for i in range(BUDGET["cliffs"])]
    out["cliffs"] = pick(ROCKS, cliffs, (25.0, 45.0), z=lambda s: -30.0 * s)
    rock_ok = lambda x, y: box(600.0, 6000.0, -3500.0, 4000.0)(x, y) and clear_of_dino(x, y, 600.0)
    out["rocks"] = pick(ROCKS, B.scatter(rng, BUDGET["rocks"], rock_ok), (1.5, 4.0), z=lambda s: -10.0 * s)
    bush_ok = lambda x, y: box(-600.0, 5000.0, -3000.0, 3500.0)(x, y) and clear_of_dino(x, y, 450.0)
    out["bushes"] = pick(BUSHES, B.scatter(rng, BUDGET["bushes"], bush_ok), (0.8, 1.4))
    grass_ok = lambda x, y: x >= -900.0 and math.hypot(x - DINO[0], y - DINO[1]) <= 3500.0 and math.hypot(x - DINO[0], y - DINO[1]) >= 350.0
    out["grass"] = pick(GRASS, B.scatter(rng, BUDGET["grass"], grass_ok), (0.8, 1.3))
    return out


def selftest():
    p = placements()
    assert p == placements(), "deterministic"
    for k, rows in p.items():
        assert 0 < len(rows) <= BUDGET[k], (k, len(rows))
    assert len(p["trees"]) >= 50, len(p["trees"])
    assert sum(len(r) for r in p.values()) <= sum(BUDGET.values())
    for k in ("trees", "rocks", "bushes"):  # nothing tall in front of or on the dino
        for _, x, y, *_ in p[k]:
            assert clear_of_dino(x, y, 450.0), (k, x, y)
    for _, x, y, *_ in p["trees"]:
        assert x >= 3500.0 or abs(y - DINO[1]) >= 1800.0, (x, y)
    sx = screen_x(*DINO)
    assert 0.65 <= sx <= 0.80, "dino center screen x %.3f must sit right of the UI panel (0.05..0.50)" % sx
    hf = screen_height_fraction(DINO_HEIGHT, DINO[0])
    assert 0.35 <= hf <= 0.55, "dino height fraction %.3f" % hf
    facing = math.radians(DINO_YAW)
    assert math.cos(facing) < 0 and math.sin(facing) < 0, "dino faces the camera (-X) and the panel (-Y)"
    print(LOG + "selftest ok (%s; dino screen x %.3f, height %.3f)" % (
        ", ".join("%s %d" % (k, len(v)) for k, v in p.items()), sx, hf))


# ---------------------------------------------------------------- Unreal side
def write_report(status, counts=None):
    os.makedirs(os.path.dirname(REPORT), exist_ok=True)
    with open(os.path.abspath(__file__), "rb") as f:
        sha = hashlib.sha256(f.read()).hexdigest()
    with open(REPORT, "w") as f:
        json.dump({"status": status, "scriptSha256": sha, "map": MAP_PATH, "counts": counts or {}, "errors": ERRORS}, f, indent=2)


def load_cls(path):
    cls = unreal.load_class(None, path)
    if cls is None:
        B.fail("cannot load " + path + " (is the editor module built?)")
    return cls


def settings_override(settings, values):
    for prop, value in values:
        settings.set_editor_property("override_" + prop, True)
        settings.set_editor_property(prop, value)


def build_lighting():
    # Dynamic GI is off and nothing is baked: every light is movable. Rotator(roll, pitch, yaw).
    sun = B.spawn(unreal.DirectionalLight, "PSL_Menu_Sun", 0, 0, 5000, unreal.Rotator(0.0, -16.0, 40.0))
    sun.root_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    lc = sun.get_component_by_class(unreal.DirectionalLightComponent)
    lc.set_editor_property("intensity", 6.0)
    lc.set_editor_property("light_color", unreal.Color(255, 214, 170, 255))
    lc.set_editor_property("atmosphere_sun_light", True)
    lc.set_editor_property("contact_shadow_length", 0.05)
    B.spawn(unreal.SkyAtmosphere, "PSL_Menu_SkyAtmosphere")
    sky = B.spawn(unreal.SkyLight, "PSL_Menu_SkyLight", 0, 0, 5000)
    sky.root_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    sc = sky.get_component_by_class(unreal.SkyLightComponent)
    sc.set_editor_property("real_time_capture", True)
    sc.set_editor_property("intensity", 1.3)
    fog = B.spawn(unreal.ExponentialHeightFog, "PSL_Menu_Fog")
    fc = fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
    fc.set_editor_property("fog_density", 0.02)
    fc.set_editor_property("fog_height_falloff", 0.2)
    fc.set_editor_property("fog_inscattering_luminance", unreal.LinearColor(0.75, 0.6, 0.45, 1.0))
    fc.set_editor_property("start_distance", 2500.0)
    fc.set_editor_property("enable_volumetric_fog", False)
    pp = B.spawn(unreal.PostProcessVolume, "PSL_Menu_PostProcess")
    pp.set_editor_property("unbound", True)
    s = pp.get_editor_property("settings")
    # Fixed exposure: auto exposure adapting to the sky blew out the lit snout in the M2c3 menu.
    settings_override(s, (
        ("auto_exposure_min_brightness", 1.0), ("auto_exposure_max_brightness", 1.0), ("auto_exposure_bias", 0.0),
        ("bloom_intensity", 0.5), ("vignette_intensity", 0.4), ("motion_blur_amount", 0.0),
        ("color_saturation", unreal.Vector4(1.1, 1.1, 1.1, 1.0)), ("color_contrast", unreal.Vector4(1.05, 1.05, 1.05, 1.0)),
        ("white_temp", 5800.0),
    ))
    pp.set_editor_property("settings", s)


def build_scene():
    plane = unreal.load_asset(PLANE_MESH)
    terrain = unreal.load_asset(B.MAT_DEST + "/M_PSL_Terrain")
    if plane is None or terrain is None:
        B.fail("cannot load %s or M_PSL_Terrain" % PLANE_MESH)
    ground = B.spawn(unreal.StaticMeshActor, "PSL_Menu_Ground")
    ground.set_actor_scale3d(unreal.Vector(GROUND_SCALE, GROUND_SCALE, 1.0))
    ground.static_mesh_component.set_static_mesh(plane)
    ground.static_mesh_component.set_material(0, terrain)

    counts = {}
    cull = {"trees": 0.0, "cliffs": 0.0, "rocks": 20000.0, "bushes": 12000.0, "grass": 8000.0}
    for label, (kind, rows) in zip(HOLDERS, placements().items()):
        groups = {}
        for stem, x, y, z, yaw, s in rows:
            groups.setdefault(stem, []).append(B.transform(x, y, z, yaw, s))
        counts[kind] = B.holder(label, sorted(groups.items()), False, cull[kind])
    build_lighting()

    B.spawn(load_cls(PREVIEW_CLASS), "MenuPreview", DINO[0], DINO[1], 0.0, unreal.Rotator(0.0, 0.0, DINO_YAW))
    rig = B.spawn(load_cls(RIG_CLASS), "MenuCameraRig", CAMERA[0], CAMERA[1], CAMERA[2], unreal.Rotator(0.0, CAMERA_PITCH, 0.0))
    rig.set_editor_property("dolly_distance", DOLLY_DISTANCE)
    rig.set_editor_property("dolly_period", DOLLY_PERIOD)
    cam = rig.get_editor_property("camera_component")
    cam.set_editor_property("field_of_view", FOV)
    cam.set_editor_property("constrain_aspect_ratio", False)  # no letterbox on non-16:9 windows
    return counts


def verify(levels, counts):
    if not levels.load_level(MAP_PATH):
        B.fail("reload failed for " + MAP_PATH)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    found = {a.get_actor_label(): a for a in actors}
    problems = []
    for name, path in (("MenuCameraRig", RIG_CLASS), ("MenuPreview", PREVIEW_CLASS)):
        n = len([a for a in actors if a.get_class() == load_cls(path)])
        if n != 1:
            problems.append("expected exactly 1 %s, got %d" % (name, n))
    for label, cls in LIGHTS.items():
        a = found.get(label)
        if a is None or not isinstance(a, getattr(unreal, cls)):
            problems.append("missing %s (%s)" % (label, cls))
    if "PSL_Menu_Ground" not in found:
        problems.append("missing PSL_Menu_Ground")
    for label, kind in zip(HOLDERS, BUDGET):
        a = found.get(label)
        n = sum(c.get_instance_count() for c in a.get_components_by_class(unreal.HierarchicalInstancedStaticMeshComponent)) if a else 0
        if n != counts.get(kind) or n > BUDGET[kind]:
            problems.append("%s has %d instances, built %s, budget %d" % (label, n, counts.get(kind), BUDGET[kind]))
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    gm = world.get_world_settings().get_editor_property("default_game_mode")
    if gm is None or "PSLMenuGameMode" not in gm.get_path_name():
        problems.append("world default game mode is not PSLMenuGameMode: %s" % (gm,))
    if problems:
        B.fail("; ".join(problems))


def main():
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    game_mode = load_cls(GAME_MODE_CLASS)
    if B.exists(MAP_PATH):  # this script owns L_MainMenu: replace it (new_level refuses an existing asset)
        unreal.EditorLoadingAndSavingUtils.new_blank_map(False)  # unload so the asset can be deleted
        if not unreal.EditorAssetLibrary.delete_asset(MAP_PATH):
            B.fail("cannot delete existing " + MAP_PATH)
    if not levels.new_level(MAP_PATH, False):
        B.fail("new_level failed for " + MAP_PATH)
    counts = build_scene()
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    world.get_world_settings().set_editor_property("default_game_mode", game_mode)
    if not levels.save_current_level():
        B.fail("save_current_level failed")
    verify(levels, counts)
    write_report("ok", counts)
    unreal.log(LOG + "saved and verified %s %s" % (MAP_PATH, json.dumps(counts)))


def run():
    global unreal
    try:
        import unreal as _u  # type: ignore  # only available inside the Unreal editor
        unreal = B.unreal = _u
        B.ERRORS = ERRORS  # B.fail() appends here and writes through our report
        B.write_report = write_report
        B.LOG = LOG
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
