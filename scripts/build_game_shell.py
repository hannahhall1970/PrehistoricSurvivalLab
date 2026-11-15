"""Build/verify the game-shell content (M2c). Idempotent editor-Python script; spec section C1.

Run (only under the harness build lock, no other Unreal job running):
    UnrealEditor-Cmd PrehistoricSurvivalLab.uproject -EnablePlugins=PythonScriptPlugin \
      -ExecutePythonScript=<abs>/scripts/build_game_shell.py -ScriptErrorsAreFatal -unattended -nosplash -nullrhi
Verify-only (checks, modifies nothing): env PSL_GAMESHELL_VERIFY=1, or `--verify` in sys.argv.
Pure helpers run without `unreal`:  python3 scripts/build_game_shell.py --selftest
Writes Saved/PSL/game-shell.json {status, scriptSha256, counts, errors}. Never touches L_TestArena (byte-identical check)
or the imported Dino/Prey/Nature assets.

Species step (M2c2b): creates or updates /Game/Data/Species/DA_Species_TRex and DA_Species_Para (UPSLDinoSpecies data
assets) from the SPECIES table, whose values are copied from UPSLDinoSpecies::MakeBuiltin. display_name/description are
string-table references (table "PSLSpecies", registered by the game module) and are never assigned from a python str,
which would make them culture-invariant. Mesh/anim soft refs are set from loaded objects. verify() reloads each asset and
checks every field, text identity (table id + key) and soft-ref path.

Fonts step (M2c3): imports SourceArt/Fonts/BeVietnamPro-{Regular,SemiBold,Bold}.ttf (sha256-pinned in FONT_SHA256) as
UFontFace assets /Game/UI/Fonts/FF_BeVietnamPro_<W> (LoadingPolicy Inline, Hinting Default; the factory never sets the
policy) and builds the UFont /Game/UI/Fonts/F_BeVietnamPro (Runtime cache, typefaces Regular/SemiBold/Bold) through
UPSLFontBuildLibrary, since python cannot reach FCompositeFont or UFont::FontCacheType. verify() reloads and checks all of it.
Env PSL_GAMESHELL_STEPS=species,fonts (comma list; unset/empty = all) limits the BUILD steps; verify always checks everything.
Further asset steps are added by later M2c slices.
"""
import hashlib
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
REPORT = os.path.join(ROOT, "Saved", "PSL", "game-shell.json")
TEST_ARENA = os.path.join(ROOT, "Content", "Maps", "L_TestArena.umap")
CULTURES = ("en", "vi")
LOG = "PSL build_game_shell: "
ERRORS = []
unreal = None  # imported lazily so --selftest runs without the editor

SPECIES_DIR = "/Game/Data/Species"
TEXT_TABLE = "PSLSpecies"
FLOAT_FIELDS = (
    "capsule_radius", "capsule_half_height", "mesh_yaw", "mesh_scale", "max_health", "max_stamina",
    "walk_speed", "sprint_speed", "bite_damage", "bite_radius", "bite_cooldown",
    "max_hunger", "hunger_drain_per_second", "food_per_bite",
)
ANIM_FIELDS = ("idle", "walk", "run", "attack", "death")
TEXT_FIELDS = ("display_name", "description")
TEXT_KEYS = {"SpeciesTRexName", "SpeciesTRexDesc", "SpeciesParaName", "SpeciesParaDesc"}
OBJECT_PATH_RE = re.compile(r"^/Game/(?:[A-Za-z0-9_]+/)+([A-Za-z0-9_]+)\.\1$")

BUILD_STEPS = ("species", "fonts")
FONT_DIR = "/Game/UI/Fonts"
FONT_ASSET = FONT_DIR + "/F_BeVietnamPro"
FONT_WEIGHTS = ("Regular", "SemiBold", "Bold")
FONT_SHA256 = {
    "Regular": "cd1ef6e9d7db28ad5cdb88a65ccbe693870e60d340b791f349d248342b4fe4c3",
    "SemiBold": "bd8e27eb02720b9d91e59e4f10a90878643219f25ce6a8d9a4f06a8a88d3bb71",
    "Bold": "7f738fe5c43c8872807b20e2d30d42163618de8a4daf7f48a939adac32c16847",
}

# Values copied from UPSLDinoSpecies::MakeBuiltin (Private/PSLDinoSpecies.cpp). mesh_scale 0 = fit 90% of the capsule.
SPECIES = {
    "TRex": {
        "asset": SPECIES_DIR + "/DA_Species_TRex",
        "diet": "CARNIVORE",
        "text_keys": {"display_name": "SpeciesTRexName", "description": "SpeciesTRexDesc"},
        "mesh": "/Game/Characters/Dino/SK_Trex.SK_Trex",
        "anims": {
            "idle": "/Game/Characters/Dino/A_Trex_Idle.A_Trex_Idle",
            "walk": "/Game/Characters/Dino/A_Trex_Walk.A_Trex_Walk",
            "run": "/Game/Characters/Dino/A_Trex_Run.A_Trex_Run",
            "attack": "/Game/Characters/Dino/TrexArmature_TRex_Attack.TrexArmature_TRex_Attack",
            "death": "/Game/Characters/Dino/TrexArmature_TRex_Death.TrexArmature_TRex_Death",
        },
        "floats": {
            "capsule_radius": 60.0, "capsule_half_height": 120.0, "mesh_yaw": -90.0, "mesh_scale": 1.0,
            "max_health": 100.0, "max_stamina": 100.0, "walk_speed": 300.0, "sprint_speed": 600.0,
            "bite_damage": 25.0, "bite_radius": 120.0, "bite_cooldown": 1.0,
            "max_hunger": 100.0, "hunger_drain_per_second": 0.55, "food_per_bite": 1.0,
        },
    },
    "Para": {
        "asset": SPECIES_DIR + "/DA_Species_Para",
        "diet": "HERBIVORE",
        "text_keys": {"display_name": "SpeciesParaName", "description": "SpeciesParaDesc"},
        "mesh": "/Game/Characters/Prey/SK_Parasaurolophus.SK_Parasaurolophus",
        "anims": {
            "idle": "/Game/Characters/Prey/A_Para_Idle.A_Para_Idle",
            "walk": "/Game/Characters/Prey/A_Para_Walk.A_Para_Walk",
            "run": "/Game/Characters/Prey/A_Para_Run.A_Para_Run",
            "attack": "/Game/Characters/Prey/A_Para_Attack.A_Para_Attack",
            "death": "/Game/Characters/Prey/A_Para_Death.A_Para_Death",
        },
        "floats": {
            "capsule_radius": 50.0, "capsule_half_height": 110.0, "mesh_yaw": -90.0, "mesh_scale": 0.0,
            "max_health": 70.0, "max_stamina": 140.0, "walk_speed": 340.0, "sprint_speed": 760.0,
            "bite_damage": 12.0, "bite_radius": 100.0, "bite_cooldown": 0.7,
            "max_hunger": 100.0, "hunger_drain_per_second": 0.40, "food_per_bite": 0.6,
        },
    },
}


def file_sha256(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def is_verify_mode(argv, environ):
    return environ.get("PSL_GAMESHELL_VERIFY") == "1" or "--verify" in argv


def locres_paths(root):
    return [os.path.join(root, "Content", "Localization", "Game", c, "Game.locres") for c in CULTURES]


def missing_files(paths):
    return [p for p in paths if not os.path.isfile(p)]


def report_payload(status, script_sha, counts, errors):
    return {"status": status, "scriptSha256": script_sha, "counts": dict(counts), "errors": list(errors)}


def asset_name(species_id):
    return "DA_Species_" + species_id


def build_steps(environ):
    """Build steps selected by PSL_GAMESHELL_STEPS (comma list); unset/empty = all. Unknown name -> ValueError."""
    raw = [s.strip() for s in environ.get("PSL_GAMESHELL_STEPS", "").split(",") if s.strip()]
    unknown = [s for s in raw if s not in BUILD_STEPS]
    if unknown:
        raise ValueError("unknown PSL_GAMESHELL_STEPS entries %s (valid: %s)" % (unknown, ", ".join(BUILD_STEPS)))
    return tuple(s for s in BUILD_STEPS if s in raw) if raw else BUILD_STEPS


def font_face_package(weight):
    return FONT_DIR + "/FF_BeVietnamPro_" + weight


def font_source(weight):
    return os.path.join(ROOT, "SourceArt", "Fonts", "BeVietnamPro-%s.ttf" % weight)


def object_path(package):
    return package + "." + package.rsplit("/", 1)[1]


def selftest():
    import tempfile
    assert is_verify_mode(["x", "--verify"], {})
    assert is_verify_mode(["x"], {"PSL_GAMESHELL_VERIFY": "1"})
    assert not is_verify_mode(["x"], {"PSL_GAMESHELL_VERIFY": "0"})
    assert not is_verify_mode(["x", "--selftest"], {})
    paths = locres_paths("/r")
    assert paths == ["/r/Content/Localization/Game/en/Game.locres", "/r/Content/Localization/Game/vi/Game.locres"], paths
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, "a.bin")
        with open(p, "wb") as f:
            f.write(b"psl")
        assert file_sha256(p) == hashlib.sha256(b"psl").hexdigest()
        assert missing_files([p, os.path.join(d, "nope")]) == [os.path.join(d, "nope")]
    r = report_payload("ok", "abc", {"n": 1}, ())
    assert json.loads(json.dumps(r)) == {"status": "ok", "scriptSha256": "abc", "counts": {"n": 1}, "errors": []}
    assert missing_files(locres_paths(ROOT)) == [], "repo .locres missing"
    assert sorted(SPECIES) == ["Para", "TRex"], sorted(SPECIES)
    assert object_path("/Game/Data/Species/DA_Species_TRex") == "/Game/Data/Species/DA_Species_TRex.DA_Species_TRex"
    for sid, s in SPECIES.items():
        assert sorted(s["floats"]) == sorted(FLOAT_FIELDS), (sid, sorted(s["floats"]))
        for k, v in s["floats"].items():
            assert isinstance(v, float) and not isinstance(v, bool), (sid, k, v)
        assert s["diet"] in ("CARNIVORE", "HERBIVORE"), (sid, s["diet"])
        assert s["asset"] == SPECIES_DIR + "/" + asset_name(sid), (sid, s["asset"])
        assert s["asset"].rsplit("/", 1)[1] == asset_name(sid)
        assert sorted(s["text_keys"]) == sorted(TEXT_FIELDS), (sid, s["text_keys"])
        assert set(s["text_keys"].values()) <= TEXT_KEYS, (sid, s["text_keys"])
        assert sorted(s["anims"]) == sorted(ANIM_FIELDS), (sid, sorted(s["anims"]))
        for path in [object_path(s["asset"]), s["mesh"], *s["anims"].values()]:
            assert OBJECT_PATH_RE.match(path), (sid, path)
    assert {k for s in SPECIES.values() for k in s["text_keys"].values()} == TEXT_KEYS
    assert BUILD_STEPS == ("species", "fonts")
    assert build_steps({}) == BUILD_STEPS and build_steps({"PSL_GAMESHELL_STEPS": ""}) == BUILD_STEPS
    assert build_steps({"PSL_GAMESHELL_STEPS": "fonts"}) == ("fonts",)
    assert build_steps({"PSL_GAMESHELL_STEPS": " fonts , species,fonts"}) == BUILD_STEPS
    try:
        build_steps({"PSL_GAMESHELL_STEPS": "fonts,bogus"})
        raise AssertionError("unknown step accepted")
    except ValueError:
        pass
    assert FONT_DIR == "/Game/UI/Fonts" and FONT_ASSET == FONT_DIR + "/F_BeVietnamPro"
    assert FONT_WEIGHTS == ("Regular", "SemiBold", "Bold") and sorted(FONT_SHA256) == sorted(FONT_WEIGHTS)
    assert all(re.fullmatch(r"[0-9a-f]{64}", h) for h in FONT_SHA256.values())
    for w in FONT_WEIGHTS:
        assert font_face_package(w) == FONT_DIR + "/FF_BeVietnamPro_" + w
        assert os.path.basename(font_source(w)) == "BeVietnamPro-%s.ttf" % w
        assert OBJECT_PATH_RE.match(object_path(font_face_package(w))), w
    assert OBJECT_PATH_RE.match(object_path(FONT_ASSET))
    absent = missing_files([font_source(w) for w in FONT_WEIGHTS])
    if absent:
        print(LOG + "selftest note: SourceArt font ttf absent, sha256 check skipped")
    else:
        for w in FONT_WEIGHTS:
            assert file_sha256(font_source(w)) == FONT_SHA256[w], w
    print(LOG + "selftest ok")


def write_report(status, counts=None):
    os.makedirs(os.path.dirname(REPORT), exist_ok=True)
    with open(REPORT, "w") as f:
        json.dump(report_payload(status, file_sha256(os.path.abspath(__file__)), counts or {}, ERRORS), f, indent=2)


def fail(message):
    ERRORS.append(message)
    if unreal is None:
        print(LOG + message, file=sys.stderr)
    else:
        unreal.log_error(LOG + message)
    write_report("failed")
    raise SystemExit(1)


def load_species_asset(sid):
    return unreal.EditorAssetLibrary.load_asset(SPECIES[sid]["asset"])


def load_ref(path, what):
    obj = unreal.load_asset(path)
    if obj is None:
        fail("cannot load %s %s" % (what, path))
    return obj


def build_species(counts):
    for sid, s in SPECIES.items():
        pkg = s["asset"]
        if unreal.EditorAssetLibrary.does_asset_exist(pkg):
            asset = unreal.EditorAssetLibrary.load_asset(pkg)
        else:
            factory = unreal.DataAssetFactory()
            factory.set_editor_property("data_asset_class", unreal.PSLDinoSpecies)
            asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
                asset_name(sid), SPECIES_DIR, unreal.PSLDinoSpecies, factory)
        if asset is None or not isinstance(asset, unreal.PSLDinoSpecies):
            fail("cannot create or load %s as PSLDinoSpecies (got %r)" % (pkg, asset))
        asset.set_editor_property("species_id", unreal.Name(sid))
        asset.set_editor_property("diet", getattr(unreal.PSLDiet, s["diet"]))
        for field in TEXT_FIELDS:  # string-table refs only: a python str would become culture-invariant
            asset.set_editor_property(field, unreal.TextLibrary.text_from_string_table(TEXT_TABLE, s["text_keys"][field]))
        asset.set_editor_property("mesh", load_ref(s["mesh"], "mesh"))
        for a in ANIM_FIELDS:
            asset.set_editor_property(a + "_anim", load_ref(s["anims"][a], a + " anim"))
        for k, v in s["floats"].items():
            asset.set_editor_property(k, v)
        if not unreal.EditorAssetLibrary.save_asset(pkg, only_if_is_dirty=False):
            fail("save_asset failed for " + pkg)
    counts["speciesBuilt"] = len(SPECIES)


def text_identity(text):
    """(table_id, key) strings of a string-table FText, or None."""
    if not unreal.TextLibrary.text_is_from_string_table(text):
        return None
    res = unreal.TextLibrary.string_table_id_and_key_from_text(text)
    if res is None:
        return None
    if isinstance(res, (tuple, list)):
        if len(res) == 3 and isinstance(res[0], bool):  # (success, table_id, key)
            if not res[0]:
                return None
            res = res[1:]
        if len(res) != 2:
            return None
        return str(res[0]), str(res[1])
    return None


def verify_species(counts):
    for sid, s in SPECIES.items():
        pkg = s["asset"]
        asset = load_species_asset(sid)
        if asset is None:
            fail("missing " + pkg)
        got = str(asset.get_editor_property("species_id"))
        if got != sid:
            fail("species_id mismatch for %s: got %s expected %s" % (sid, got, sid))
        want_diet = getattr(unreal.PSLDiet, s["diet"])
        got_diet = asset.get_editor_property("diet")
        if got_diet != want_diet:
            fail("diet mismatch for %s: got %s expected %s" % (sid, got_diet, want_diet))
        for k, v in s["floats"].items():
            g = asset.get_editor_property(k)
            if abs(g - v) >= 1e-4:
                fail("%s mismatch for %s: got %s expected %s" % (k, sid, g, v))
        for field in TEXT_FIELDS:
            want = (TEXT_TABLE, s["text_keys"][field])
            ident = text_identity(asset.get_editor_property(field))
            if ident != want:
                fail("%s mismatch for %s: got %s expected %s" % (field, sid, ident, want))
        refs = [("mesh", s["mesh"])] + [(a + "_anim", s["anims"][a]) for a in ANIM_FIELDS]
        for name, want in refs:
            unreal.load_asset(want)  # resolve the soft pointer before reading it
            prop = asset.get_editor_property(name)
            got = prop.get_path_name() if prop is not None else None
            if got != want:
                fail("%s mismatch for %s: got %s expected %s" % (name, sid, got, want))
        counts[asset_name(sid)] = "ok"


def build_fonts(counts):
    problems = []
    for w in FONT_WEIGHTS:
        src = font_source(w)
        if not os.path.isfile(src):
            problems.append("missing font source " + src)
        elif file_sha256(src) != FONT_SHA256[w]:
            problems.append("sha256 mismatch for %s: got %s expected %s" % (src, file_sha256(src), FONT_SHA256[w]))
    if problems:
        fail("; ".join(problems))
    tasks = []
    for w in FONT_WEIGHTS:
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", font_source(w))
        task.set_editor_property("destination_path", FONT_DIR)
        task.set_editor_property("destination_name", font_face_package(w).rsplit("/", 1)[1])
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("save", False)
        tasks.append(task)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    faces = []
    for w in FONT_WEIGHTS:
        pkg = font_face_package(w)
        face = unreal.EditorAssetLibrary.load_asset(pkg)
        if face is None or not isinstance(face, unreal.FontFace):
            fail("cannot import or load %s as FontFace (got %r)" % (pkg, face))
        # Python enum names: UENUM EFontLoadingPolicy -> unreal.FontLoadingPolicy ("E" stripped, PyGenUtil.cpp:3054),
        # entries upper-cased (PyGenUtil.cpp:3161) -> INLINE; EFontHinting -> FontHinting.DEFAULT.
        face.set_editor_property("loading_policy", unreal.FontLoadingPolicy.INLINE)
        face.set_editor_property("hinting", unreal.FontHinting.DEFAULT)
        if not unreal.EditorAssetLibrary.save_asset(pkg, only_if_is_dirty=False):
            fail("save_asset failed for " + pkg)
        faces.append(face)
    if unreal.EditorAssetLibrary.does_asset_exist(FONT_ASSET):
        font = unreal.EditorAssetLibrary.load_asset(FONT_ASSET)
    else:
        font = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            FONT_ASSET.rsplit("/", 1)[1], FONT_DIR, unreal.Font, unreal.FontFactory())
    if font is None or not isinstance(font, unreal.Font):
        fail("cannot create or load %s as Font (got %r)" % (FONT_ASSET, font))
    if not unreal.PSLFontBuildLibrary.set_runtime_typefaces(font, [unreal.Name(w) for w in FONT_WEIGHTS], faces):
        fail("set_runtime_typefaces failed for " + FONT_ASSET)
    if not unreal.EditorAssetLibrary.save_asset(FONT_ASSET, only_if_is_dirty=False):
        fail("save_asset failed for " + FONT_ASSET)
    counts["fontFaces"] = len(FONT_WEIGHTS)
    counts["fontAssets"] = [object_path(font_face_package(w)) for w in FONT_WEIGHTS] + [object_path(FONT_ASSET)]


def verify_fonts(counts):
    for w in FONT_WEIGHTS:
        pkg = font_face_package(w)
        face = unreal.EditorAssetLibrary.load_asset(pkg)
        if face is None or not isinstance(face, unreal.FontFace):
            fail("missing or wrong class for %s (got %r)" % (pkg, face))
        if face.get_editor_property("loading_policy") != unreal.FontLoadingPolicy.INLINE:
            fail("loading_policy of %s is %s, expected INLINE" % (pkg, face.get_editor_property("loading_policy")))
        want = "BeVietnamPro-%s.ttf" % w
        got = os.path.basename(str(face.get_editor_property("source_filename")).replace("\\", "/"))
        if got != want:
            fail("source_filename of %s is %s, expected %s" % (pkg, got, want))
    font = unreal.EditorAssetLibrary.load_asset(FONT_ASSET)
    if font is None or not isinstance(font, unreal.Font):
        fail("missing or wrong class for " + FONT_ASSET)
    # bool return + out params: UE Python returns None on false, else (names, faces) (PyGenUtil.cpp:1161)
    res = unreal.PSLFontBuildLibrary.get_runtime_typefaces(font)
    if not isinstance(res, (tuple, list)) or len(res) != 2:
        fail("get_runtime_typefaces failed for %s: %r" % (FONT_ASSET, res))
    names = [str(n) for n in res[0]]
    if names != list(FONT_WEIGHTS):
        fail("typeface names of %s are %s, expected %s" % (FONT_ASSET, names, list(FONT_WEIGHTS)))
    for w, face in zip(FONT_WEIGHTS, res[1]):
        got = face.get_path_name() if face is not None else None
        if got != object_path(font_face_package(w)):
            fail("typeface %s of %s is %s, expected %s" % (w, FONT_ASSET, got, object_path(font_face_package(w))))
    counts["fontFacesVerified"] = len(FONT_WEIGHTS)



def verify(counts):
    problems = ["missing " + p for p in missing_files(locres_paths(ROOT))]
    counts["locres"] = len(CULTURES) - len(problems)
    if problems:
        fail("; ".join(problems))
    verify_species(counts)
    verify_fonts(counts)


def main(verify_only):
    arena_before = file_sha256(TEST_ARENA)
    try:
        steps = build_steps(os.environ)
    except ValueError as exc:
        fail(str(exc))
    counts = {"verifyOnly": verify_only}
    if not verify_only:
        counts["buildSteps"] = list(steps)
        if "species" in steps:
            build_species(counts)
        if "fonts" in steps:
            build_fonts(counts)
    verify(counts)
    if file_sha256(TEST_ARENA) != arena_before:
        fail("L_TestArena.umap changed")
    write_report("ok", counts)
    unreal.log(LOG + "done " + json.dumps(counts))


def run():
    global unreal
    try:
        import unreal as _u  # type: ignore  # only available inside the Unreal editor
    except ImportError as exc:
        fail("unreal module unavailable (run inside the editor, or use --selftest): %r" % (exc,))
    unreal = _u
    try:
        main(is_verify_mode(sys.argv, os.environ))
    except SystemExit:
        raise
    except Exception as exc:  # unreal raises plain Exception subclasses
        fail("unexpected error: %s: %s" % (type(exc).__name__, exc))


if "--selftest" in sys.argv:
    selftest()
else:
    run()
