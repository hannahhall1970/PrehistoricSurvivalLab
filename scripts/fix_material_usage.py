"""Set the InstancedStaticMeshes usage on the HISM foliage parents (M_PSL_Flat, M_PSL_Leaf) and resave their MIs.

The cooked game draws the default material on HISM foliage whose material lacks this usage
("missing usage flag InstancedStaticMeshes"). build_lost_valley.py now sets it when it creates the parents;
this script fixes the already-generated assets in place. Idempotent.

Run (only under the harness build lock, no other Unreal job running):
    UnrealEditor-Cmd PrehistoricSurvivalLab.uproject -EnablePlugins=PythonScriptPlugin \
      -ExecutePythonScript=<abs>/scripts/fix_material_usage.py -ScriptErrorsAreFatal -unattended -nullrhi -nosplash
Report: Saved/PSL/material-usage.json {status, materials: {path: usage}, errors}.
"""
import json
import os
import sys

import unreal  # type: ignore  # only available inside the Unreal editor

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
REPORT = os.path.join(ROOT, "Saved", "PSL", "material-usage.json")
MAT_DEST = "/Game/Environment/Materials"
PARENTS = {"M_PSL_Flat": ["MI_PSL_Rock", "MI_PSL_Grass", "MI_PSL_Bark", "MI_PSL_Flower"],
           "M_PSL_Leaf": ["MI_PSL_Leaf_Leaf_Pine_C"]}
USAGE = unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES
mel = unreal.MaterialEditingLibrary
errors = []


def report(status, materials):
    os.makedirs(os.path.dirname(REPORT), exist_ok=True)
    with open(REPORT, "w") as f:
        json.dump({"status": status, "materials": materials, "errors": errors}, f, indent=2)


def save(asset):
    # LFS `lockable` checks Content out read-only; unlock only the file being saved.
    rel = asset.get_path_name().split(".")[0].replace("/Game/", "Content/", 1) + ".uasset"
    os.chmod(os.path.join(ROOT, rel), 0o644)
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, False):
        errors.append("cannot save " + asset.get_path_name())


def load(name, cls):
    asset = unreal.load_asset(MAT_DEST + "/" + name)
    if not isinstance(asset, cls):
        errors.append("cannot load %s %s/%s" % (cls.__name__, MAT_DEST, name))
        return None
    return asset


def main():
    materials = {}
    for name, children in PARENTS.items():
        mat = load(name, unreal.Material)
        if mat is None:
            continue
        mel.set_base_material_usage(mat, USAGE, True)
        mel.recompile_material(mat)
        save(mat)
        materials[mat.get_path_name()] = mel.has_material_usage(mat, USAGE)
        for child in children:
            mi = load(child, unreal.MaterialInstanceConstant)
            if mi is None:
                continue
            if mi.get_editor_property("parent") != mat:
                errors.append("%s parent is not %s" % (child, name))
                continue
            mel.update_material_instance(mi)  # recomputes the inherited usage flags from the parent
            save(mi)
            materials[mi.get_path_name()] = mel.has_material_usage(mi, USAGE)
    errors.extend("%s lacks InstancedStaticMeshes" % p for p, ok in materials.items() if not ok)
    report("failed" if errors else "ok", materials)
    if errors:
        unreal.log_error("PSL fix_material_usage: " + "; ".join(errors))
        sys.exit(1)
    unreal.log("PSL fix_material_usage: ok " + json.dumps(materials))


main()
