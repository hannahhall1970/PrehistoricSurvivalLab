"""Build/verify the game-shell content (M2c). Idempotent editor-Python script; spec section C1.

Run (only under the harness build lock, no other Unreal job running):
    UnrealEditor-Cmd PrehistoricSurvivalLab.uproject -EnablePlugins=PythonScriptPlugin \
      -ExecutePythonScript=<abs>/scripts/build_game_shell.py -ScriptErrorsAreFatal -unattended -nosplash -nullrhi
Verify-only (checks, modifies nothing): env PSL_GAMESHELL_VERIFY=1, or `--verify` in sys.argv.
Pure helpers run without `unreal`:  python3 scripts/build_game_shell.py --selftest
Writes Saved/PSL/game-shell.json {status, scriptSha256, counts, errors}. Never touches L_TestArena (byte-identical check)
or the imported Dino/Prey/Nature assets. Asset steps are added by later M2c slices.
"""
import hashlib
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
REPORT = os.path.join(ROOT, "Saved", "PSL", "game-shell.json")
TEST_ARENA = os.path.join(ROOT, "Content", "Maps", "L_TestArena.umap")
CULTURES = ("en", "vi")
LOG = "PSL build_game_shell: "
ERRORS = []
unreal = None  # imported lazily so --selftest runs without the editor


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


def verify(counts):
    problems = ["missing " + p for p in missing_files(locres_paths(ROOT))]
    counts["locres"] = len(CULTURES) - len(problems)
    if problems:
        fail("; ".join(problems))


def main(verify_only):
    arena_before = file_sha256(TEST_ARENA)
    counts = {"verifyOnly": verify_only}
    verify(counts)
    if file_sha256(TEST_ARENA) != arena_before:
        fail("L_TestArena.umap changed")
    write_report("ok", counts)
    unreal.log(LOG + "done " + json.dumps(counts))


def run():
    global unreal
    try:
        import unreal as _u  # type: ignore  # only available inside the Unreal editor
        unreal = _u
        main(is_verify_mode(sys.argv, os.environ))
    except SystemExit:
        raise
    except ImportError as exc:
        fail("unreal module unavailable (run inside the editor, or use --selftest): %r" % (exc,))
    except Exception as exc:  # unreal raises plain Exception subclasses
        fail("unexpected error: %r" % (exc,))


if "--selftest" in sys.argv:
    selftest()
else:
    run()
