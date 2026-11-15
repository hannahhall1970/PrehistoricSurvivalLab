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
Env PSL_GAMESHELL_STEPS=species,fonts,audio (comma list; unset/empty = all) limits the BUILD steps; verify always checks everything.

Audio step (M2c4): imports the 9 sha256-pinned WAVs in SourceArt/Audio (project-original, never regenerate) as SoundWaves in
/Game/Audio with no processing, creates SC_Master/SC_Music/SC_SFX and SM_PSL_Volume (no class effects), links the
hierarchy through UPSLAudioBuildLibrary (ParentClass is not python-editable), routes MUS_* -> SC_Music, others -> SC_SFX and
sets looping on MUS_Menu/MUS_Valley/AMB_Wind. verify() reloads and checks all of it.
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

BUILD_STEPS = ("species", "fonts", "audio")
FONT_DIR = "/Game/UI/Fonts"
FONT_ASSET = FONT_DIR + "/F_BeVietnamPro"
FONT_WEIGHTS = ("Regular", "SemiBold", "Bold")
FONT_SHA256 = {
    "Regular": "cd1ef6e9d7db28ad5cdb88a65ccbe693870e60d340b791f349d248342b4fe4c3",
    "SemiBold": "bd8e27eb02720b9d91e59e4f10a90878643219f25ce6a8d9a4f06a8a88d3bb71",
    "Bold": "7f738fe5c43c8872807b20e2d30d42163618de8a4daf7f48a939adac32c16847",
}
AUDIO_DIR = "/Game/Audio"
AUDIO_MIX = "SM_PSL_Volume"
SOUND_CLASSES = ("SC_Master", "SC_Music", "SC_SFX")
AUDIO_RATE = 44100
AUDIO_LOOPING = {"MUS_Menu", "MUS_Valley", "AMB_Wind"}
# name -> (channels, seconds); all 44100 Hz
AUDIO_EXPECTED = {
    "MUS_Menu": (2, 40.7), "MUS_Valley": (2, 48.9), "AMB_Wind": (1, 30.0), "SFX_UI_Click": (1, 0.12),
    "SFX_UI_Hover": (1, 0.08), "SFX_Bite": (1, 0.35), "SFX_Eat": (1, 0.6), "SFX_GameOver": (1, 2.8),
    "SFX_Starving": (1, 0.5),
}
AUDIO_SHA256 = {
    "MUS_Menu": "70ea3fadd3ea9d283aabcaad85b6c4a8bb58a50d6181264b8e0973c4cd7beb58",
    "MUS_Valley": "19d58d6bbf06f4a58052ef2b692c6a93b39b8d5a0d90e4a0851350bf8294082e",
    "AMB_Wind": "17fb1d48368d7f181d3a1e41153f4efb292cda7adf3521d5e66fb237dc3ae58c",
    "SFX_UI_Click": "128591f10a36895c534d4163500708ac0e41d6e892b05ab44f7807767d618d8f",
    "SFX_UI_Hover": "d457549e5cf2cb65677b46b2b9e4265eda6848cfe214e80db4a9f360c9d19d26",
    "SFX_Bite": "0ed0271964f79bcac281123726e7fed5bc3f3e91065d2ce654a6d95b6671193b",
    "SFX_Eat": "c255171e1ad2cc297300dd81b2756e775409f6336146815a836fa2b65b9d3572",
    "SFX_GameOver": "45a43391e73fd786d78a764570e1a752566ce3e2c82cc34613251d2297552444",
    "SFX_Starving": "8446a2da117558621a6dd54cc0e58c0580e0d157e43c3a32c492f035c654d488",
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


def audio_source(name):
    return os.path.join(ROOT, "SourceArt", "Audio", name + ".wav")


def audio_class_for(name):
    return "SC_Music" if name.startswith("MUS_") else "SC_SFX"


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
    assert BUILD_STEPS == ("species", "fonts", "audio")
    assert build_steps({"PSL_GAMESHELL_STEPS": "audio"}) == ("audio",)
    assert build_steps({}) == BUILD_STEPS and build_steps({"PSL_GAMESHELL_STEPS": ""}) == BUILD_STEPS
    assert build_steps({"PSL_GAMESHELL_STEPS": "fonts"}) == ("fonts",)
    assert build_steps({"PSL_GAMESHELL_STEPS": " fonts , species,fonts"}) == ("species", "fonts")
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
    names = sorted(AUDIO_SHA256)
    assert sorted(AUDIO_EXPECTED) == names and len(names) == 9
    assert AUDIO_LOOPING == {"MUS_Menu", "MUS_Valley", "AMB_Wind"} and AUDIO_LOOPING <= set(names)
    assert all(re.fullmatch(r"[0-9a-f]{64}", h) for h in AUDIO_SHA256.values())
    assert AUDIO_DIR == "/Game/Audio"
    for n in names:
        assert audio_class_for(n) == ("SC_Music" if n.startswith("MUS_") else "SC_SFX"), n
        assert audio_class_for(n) in SOUND_CLASSES
        assert audio_source(n) == os.path.join(ROOT, "SourceArt", "Audio", n + ".wav"), n
        assert OBJECT_PATH_RE.match(object_path(AUDIO_DIR + "/" + n)), n
    absent = missing_files([audio_source(n) for n in names])
    if absent:
        print(LOG + "selftest note: SourceArt audio wav absent, sha256 check skipped")
    else:
        for n in names:
            assert file_sha256(audio_source(n)) == AUDIO_SHA256[n], n
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


def build_audio(counts):
    problems = []
    for name in AUDIO_SHA256:
        src = audio_source(name)
        if not os.path.isfile(src):
            problems.append("missing audio source " + src)
        elif file_sha256(src) != AUDIO_SHA256[name]:
            problems.append("sha256 mismatch for %s: got %s expected %s" % (src, file_sha256(src), AUDIO_SHA256[name]))
    if problems:
        fail("; ".join(problems))
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    tasks = []
    for name in AUDIO_SHA256:
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", audio_source(name))
        task.set_editor_property("destination_path", AUDIO_DIR)
        task.set_editor_property("destination_name", name)
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("save", False)
        tasks.append(task)
    tools.import_asset_tasks(tasks)
    touched = []
    classes = {}
    for name in SOUND_CLASSES:
        pkg = AUDIO_DIR + "/" + name
        if unreal.EditorAssetLibrary.does_asset_exist(pkg):
            obj = unreal.EditorAssetLibrary.load_asset(pkg)
        else:
            obj = tools.create_asset(name, AUDIO_DIR, unreal.SoundClass, unreal.SoundClassFactory())
        if obj is None or not isinstance(obj, unreal.SoundClass):
            fail("cannot create or load %s as SoundClass (got %r)" % (pkg, obj))
        classes[name] = obj
        touched.append(obj)
    mix_pkg = AUDIO_DIR + "/" + AUDIO_MIX
    if unreal.EditorAssetLibrary.does_asset_exist(mix_pkg):
        mix = unreal.EditorAssetLibrary.load_asset(mix_pkg)
    else:
        mix = tools.create_asset(AUDIO_MIX, AUDIO_DIR, unreal.SoundMix, unreal.SoundMixFactory())
    if mix is None or not isinstance(mix, unreal.SoundMix):
        fail("cannot create or load %s as SoundMix (got %r)" % (mix_pkg, mix))
    if len(mix.get_editor_property("sound_class_effects")) != 0:
        fail("%s sound_class_effects must stay empty" % mix_pkg)
    touched.append(mix)
    master = classes["SC_Master"]
    for child in ("SC_Music", "SC_SFX"):
        if not unreal.PSLAudioBuildLibrary.set_sound_class_parent(classes[child], master):
            fail("set_sound_class_parent failed for " + child)
    if not unreal.PSLAudioBuildLibrary.set_sound_class_parent(master, None):
        fail("set_sound_class_parent failed for SC_Master")
    for name in AUDIO_SHA256:
        pkg = AUDIO_DIR + "/" + name
        wave = unreal.EditorAssetLibrary.load_asset(pkg)
        if wave is None or not isinstance(wave, unreal.SoundWave):
            fail("cannot import or load %s as SoundWave (got %r)" % (pkg, wave))
        wave.set_editor_property("sound_class_object", classes[audio_class_for(name)])
        wave.set_editor_property("looping", name in AUDIO_LOOPING)
        touched.append(wave)
    for obj in touched:
        if not unreal.EditorAssetLibrary.save_loaded_asset(obj, False):
            fail("save_loaded_asset failed for " + obj.get_path_name())
    counts["audioImported"] = len(AUDIO_SHA256)
    counts["soundClasses"] = len(SOUND_CLASSES)


def verify_audio(counts):
    def load(pkg, cls):
        obj = unreal.EditorAssetLibrary.load_asset(pkg)
        if obj is None or not isinstance(obj, cls):
            fail("missing or wrong class for %s (got %r)" % (pkg, obj))
        return obj

    def prop(obj, key):
        try:
            return obj.get_editor_property(key)
        except Exception as exc:
            fail("cannot read %s of %s: %s" % (key, obj.get_path_name(), exc))

    classes = {n: load(AUDIO_DIR + "/" + n, unreal.SoundClass) for n in SOUND_CLASSES}
    master_path = classes["SC_Master"].get_path_name()
    for name, sc in classes.items():
        parent = prop(sc, "parent_class")
        want = None if name == "SC_Master" else master_path
        if (parent.get_path_name() if parent else None) != want:
            fail("parent_class of %s is %s, expected %s" % (name, parent, want))
    kids = sorted(c.get_path_name() for c in prop(classes["SC_Master"], "child_classes"))
    want_kids = sorted(classes[n].get_path_name() for n in ("SC_Music", "SC_SFX"))
    if kids != want_kids:
        fail("SC_Master child_classes are %s, expected %s" % (kids, want_kids))
    mix = load(AUDIO_DIR + "/" + AUDIO_MIX, unreal.SoundMix)
    if len(prop(mix, "sound_class_effects")) != 0:
        fail("%s sound_class_effects is not empty" % AUDIO_MIX)
    for name, (channels, seconds) in AUDIO_EXPECTED.items():
        pkg = AUDIO_DIR + "/" + name
        wave = load(pkg, unreal.SoundWave)
        sc = prop(wave, "sound_class_object")
        want_sc = classes[audio_class_for(name)].get_path_name()
        if sc is None or sc.get_path_name() != want_sc:
            fail("sound_class_object of %s is %s, expected %s" % (pkg, sc, want_sc))
        if bool(prop(wave, "looping")) != (name in AUDIO_LOOPING):
            fail("looping of %s is %s, expected %s" % (pkg, prop(wave, "looping"), name in AUDIO_LOOPING))
        if prop(wave, "num_channels") != channels:
            fail("num_channels of %s is %s, expected %s" % (pkg, prop(wave, "num_channels"), channels))
        if abs(prop(wave, "duration") - seconds) > 0.02:
            fail("duration of %s is %s, expected %s +-0.02" % (pkg, prop(wave, "duration"), seconds))
        # sample rate may not be exposed to python; check it only when available, else record it
        rate = wave.get_sample_rate() if hasattr(wave, "get_sample_rate") else None
        if rate is None:
            try:
                rate = wave.get_editor_property("sample_rate")
            except Exception:
                rate = None
        if rate is None:
            counts.setdefault("audioSampleRateUnavailable", []).append(name)
        elif int(rate) != AUDIO_RATE:
            fail("sample_rate of %s is %s, expected %s" % (pkg, rate, AUDIO_RATE))
        if abs(prop(wave, "volume") - 1.0) > 1e-6:
            fail("volume of %s is %s, expected 1.0" % (pkg, prop(wave, "volume")))
        try:
            data = wave.get_editor_property("asset_import_data")
        except Exception:
            data = None
        if data is not None:
            got = os.path.basename(str(data.get_first_filename()).replace("\\", "/"))
            if got != name + ".wav":
                fail("import source of %s is %s, expected %s.wav" % (pkg, got, name))
    counts["audioVerified"] = len(AUDIO_EXPECTED)



def verify(counts):
    problems = ["missing " + p for p in missing_files(locres_paths(ROOT))]
    counts["locres"] = len(CULTURES) - len(problems)
    if problems:
        fail("; ".join(problems))
    verify_species(counts)
    verify_fonts(counts)
    verify_audio(counts)


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
        if "audio" in steps:
            build_audio(counts)
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
