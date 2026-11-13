"""Import SourceArt/Dino/Trex.fbx into /Game/Characters/Dino as stable asset names.

Run (only under the harness build lock, no other Unreal job running):
    UnrealEditor-Cmd PrehistoricSurvivalLab.uproject -ExecutePythonScript=scripts/import_dino.py -EnablePlugins=PythonScriptPlugin -ScriptErrorsAreFatal -unattended -nullrhi -nosplash

Produces SK_Trex, SKEL_Trex, A_Trex_Idle, A_Trex_Walk, A_Trex_Run, the names
APSLCharacter's soft references expect. Exits non-zero on any failure.

The Quaternius pack ships all clips in one FBX; UE names them
TrexArmature_TRex_<Clip>. Only the exact suffixes _TRex_Idle/_Walk/_Run are
used (IdleD, Walkmb, ... are ignored).

Idempotent and non-destructive: never deletes assets or folders. A complete,
valid set is left untouched; a partial or invalid set fails loudly.
"""
import os
import re
import sys

import unreal

DEST = "/Game/Characters/Dino"
FBX = os.path.normpath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "SourceArt", "Dino", "Trex.fbx")
)
MESH = "SK_Trex"
SKEL = "SKEL_Trex"
CLIPS = {"Idle": "A_Trex_Idle", "Walk": "A_Trex_Walk", "Run": "A_Trex_Run"}
CLIP_RE = re.compile(r"(?i)_TRex_(Idle|Walk|Run)$")
IMPORT_SCALE = 0.16  # raw FBX is ~15 m tall; 0.16 gives bounds z-extent ~121.8
Z_EXTENT_RANGE = (100.0, 140.0)
ALL_NAMES = [MESH, SKEL] + list(CLIPS.values())


def fail(message):
    unreal.log_error("PSL import_dino: " + message)
    sys.exit(1)


def exists(name):
    return unreal.EditorAssetLibrary.does_asset_exist(DEST + "/" + name)


def asset_name(path):
    return path.rsplit("/", 1)[-1].split(".")[0]


def validate():
    """Fail unless the final set is on one skeleton with sane bounds; log bounds and clip lengths."""
    skeleton = unreal.load_asset(DEST + "/" + SKEL)
    mesh = unreal.load_asset(DEST + "/" + MESH)
    if not isinstance(skeleton, unreal.Skeleton):
        fail(SKEL + " is not a Skeleton")
    if not isinstance(mesh, unreal.SkeletalMesh) or mesh.skeleton != skeleton:
        fail(MESH + " missing or does not use " + SKEL)
    bounds = mesh.get_bounds()
    z_extent = bounds.box_extent.z
    unreal.log("PSL import_dino: bounds origin=%s box_extent=%s" % (bounds.origin, bounds.box_extent))
    if not Z_EXTENT_RANGE[0] <= z_extent <= Z_EXTENT_RANGE[1]:
        fail("bounds z-extent %.1f outside %s (import scale wrong?)" % (z_extent, Z_EXTENT_RANGE))
    for name in CLIPS.values():
        seq = unreal.load_asset(DEST + "/" + name)
        if not isinstance(seq, unreal.AnimSequence) or seq.get_editor_property("skeleton") != skeleton:
            fail(name + " missing or not on " + SKEL)
        unreal.log("PSL import_dino: %s length=%.3fs" % (name, seq.get_editor_property("sequence_length")))


def import_fbx():
    if not os.path.isfile(FBX):
        fail("missing " + FBX + " (is Git LFS content pulled?)")

    options = unreal.FbxImportUI()
    options.import_mesh = True
    options.import_as_skeletal = True
    options.import_animations = True
    options.import_materials = True
    options.import_textures = False
    options.create_physics_asset = False
    options.skeletal_mesh_import_data.set_editor_property("import_uniform_scale", IMPORT_SCALE)
    options.anim_sequence_import_data.set_editor_property("import_uniform_scale", IMPORT_SCALE)

    task = unreal.AssetImportTask()
    task.filename = FBX
    task.destination_path = DEST
    task.automated = True
    task.replace_existing = True
    task.replace_existing_settings = True
    task.save = True
    task.options = options

    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    paths = [str(p) for p in task.imported_object_paths]
    if not paths:
        fail("FBX import produced no assets")
    return paths


def rename(old_path, new_name):
    new_path = DEST + "/" + new_name
    if exists(new_name):
        fail("rename target already exists: " + new_path)
    if not unreal.EditorAssetLibrary.rename_asset(old_path, new_path):
        fail("rename " + old_path + " -> " + new_path + " failed")


def main():
    present = [n for n in ALL_NAMES if exists(n)]
    if len(present) == len(ALL_NAMES):
        validate()
        unreal.log("PSL import_dino: already present")
        return
    if present:
        missing = [n for n in ALL_NAMES if n not in present]
        fail("partial set in %s: present=%s missing=%s (resolve manually; nothing deleted)" % (DEST, present, missing))

    imported = import_fbx()
    unreal.log("PSL import_dino: imported " + ", ".join(imported))

    meshes, skeletons, anims = [], [], []
    for path in imported:
        asset = unreal.load_asset(path)
        if isinstance(asset, unreal.SkeletalMesh):
            meshes.append(path)
        elif isinstance(asset, unreal.Skeleton):
            skeletons.append(path)
        elif isinstance(asset, unreal.AnimSequence):
            anims.append(path)

    if len(meshes) != 1 or len(skeletons) != 1:
        fail("expected 1 SkeletalMesh and 1 Skeleton, got %d and %d" % (len(meshes), len(skeletons)))

    wanted = {}
    for key, name in CLIPS.items():
        hits = [p for p in anims if (m := CLIP_RE.search(asset_name(p))) and m.group(1).lower() == key.lower()]
        if len(hits) != 1:
            fail("expected exactly 1 AnimSequence named *_TRex_%s, got %s (all: %s)" % (key, hits, anims))
        wanted[name] = hits[0]

    rename(meshes[0], MESH)
    rename(skeletons[0], SKEL)
    for name, path in wanted.items():
        rename(path, name)

    validate()
    if not unreal.EditorAssetLibrary.save_directory(DEST, only_if_is_dirty=False, recursive=True):
        fail("failed to save " + DEST)
    unreal.log("PSL import_dino: done")


try:
    main()
except SystemExit:
    raise
except Exception as exc:  # unreal raises plain Exception subclasses
    fail("unexpected error: %r" % (exc,))
