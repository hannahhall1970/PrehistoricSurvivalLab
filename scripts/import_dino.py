"""Import SourceArt/Dino/Trex.fbx into /Game/Characters/Dino as stable asset names.

Run (only under the harness build lock, no other Unreal job running):
    UnrealEditor-Cmd PrehistoricSurvivalLab.uproject -ExecutePythonScript=scripts/import_dino.py -unattended -nullrhi -nosplash

Produces SK_Trex, SKEL_Trex, A_Trex_Idle, A_Trex_Walk, A_Trex_Run, the names
APSLCharacter's soft references expect. Exits non-zero on any failure.

STATUS: NOT EXECUTED. UnrealEditor was unavailable when this was written, so the
FBX import options and the animation-name matching below are unverified. The
Quaternius pack ships all clips in one FBX; if the clip names do not contain
"idle", "walk" and "run" the script fails loudly instead of guessing.
"""
import os
import sys

import unreal

DEST = "/Game/Characters/Dino"
FBX = os.path.normpath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "SourceArt", "Dino", "Trex.fbx")
)
CLIPS = {"idle": "A_Trex_Idle", "walk": "A_Trex_Walk", "run": "A_Trex_Run"}


def fail(message):
    unreal.log_error("PSL import_dino: " + message)
    sys.exit(1)


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
    options.skeletal_mesh_import_data.import_morph_targets = False

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
    if old_path.split(".")[0] == new_path:
        return
    if unreal.EditorAssetLibrary.does_asset_exist(new_path):
        unreal.EditorAssetLibrary.delete_asset(new_path)
    if not unreal.EditorAssetLibrary.rename_asset(old_path, new_path):
        fail("rename " + old_path + " -> " + new_path + " failed")


def main():
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
        hits = [p for p in anims if key in p.lower()]
        if len(hits) != 1:
            fail("expected exactly 1 AnimSequence matching '%s', got %s (all: %s)" % (key, hits, anims))
        wanted[name] = hits[0]

    rename(meshes[0], "SK_Trex")
    rename(skeletons[0], "SKEL_Trex")
    for name, path in wanted.items():
        rename(path, name)

    skeleton = unreal.load_asset(DEST + "/SKEL_Trex")
    mesh = unreal.load_asset(DEST + "/SK_Trex")
    if not isinstance(mesh, unreal.SkeletalMesh) or mesh.skeleton != skeleton:
        fail("SK_Trex does not use SKEL_Trex")
    for name in CLIPS.values():
        seq = unreal.load_asset(DEST + "/" + name)
        if not isinstance(seq, unreal.AnimSequence) or seq.get_editor_property("skeleton") != skeleton:
            fail(name + " missing or not on SKEL_Trex")

    if not unreal.EditorAssetLibrary.save_directory(DEST, only_if_is_dirty=False, recursive=True):
        fail("failed to save " + DEST)
    unreal.log("PSL import_dino: done")


try:
    main()
except SystemExit:
    raise
except Exception as exc:  # unreal raises plain Exception subclasses
    fail("unexpected error: %r" % (exc,))
