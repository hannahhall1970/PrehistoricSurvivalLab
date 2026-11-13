"""Add navmesh bounds, one prey and four cover cubes to /Game/Maps/L_TestArena.

Run (only under the harness build lock, no other Unreal job running):
    UnrealEditor-Cmd PrehistoricSurvivalLab.uproject -EnablePlugins=PythonScriptPlugin \
      -ExecutePythonScript=<abs>/scripts/setup_hunt_arena.py -ScriptErrorsAreFatal -unattended -nullrhi -nosplash

Non-destructive: loads the existing map, never deletes actors, never creates a level.
Idempotent by actor label: full set -> validate only; partial set -> fail listing missing labels.
Exits non-zero on failure.
"""
import sys

from typing import NoReturn

import unreal  # type: ignore  # only available inside the Unreal editor

MAP_PATH = "/Game/Maps/L_TestArena"
PREY_CLASS = "/Script/PrehistoricSurvivalLab.PSLPreyCharacter"
CUBE_MESH = "/Engine/BasicShapes/Cube.Cube"  # 100 x 100 x 100 uu, pivot at center

NAV_LABEL = "PSL_NavBounds"
PREY_LABEL = "PSL_Prey_01"
COVER_LABELS = ["PSL_Cover_0%d" % i for i in range(1, 5)]
COVER_XY = [(1200, 1200), (-1200, 1200), (-1200, -1200), (1200, -1200)]
ALL_LABELS = [NAV_LABEL, PREY_LABEL] + COVER_LABELS
MIN_NAV_EXTENT_XY = 2400  # floor is 5000 x 5000 (half extent 2500)


def fail(message) -> NoReturn:
    unreal.log_error("PSL setup_hunt_arena: " + message)
    sys.exit(1)


def actors_by_label():
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    return {a.get_actor_label(): a for a in actors}


def spawn(actor_class, location, label):
    actor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).spawn_actor_from_class(
        actor_class, location, unreal.Rotator(0, 0, 0)
    )
    if actor is None:
        fail("failed to spawn " + label)
    actor.set_actor_label(label)
    return actor


def validate(prey_class, mesh):
    found = actors_by_label()
    missing = [l for l in ALL_LABELS if l not in found]
    if missing:
        fail("missing after setup: " + ", ".join(missing))

    nav = found[NAV_LABEL]
    if not isinstance(nav, unreal.NavMeshBoundsVolume):
        fail(NAV_LABEL + " is not a NavMeshBoundsVolume: " + str(nav.get_class()))
    _origin, extent = nav.get_actor_bounds(False)
    if extent.x < MIN_NAV_EXTENT_XY or extent.y < MIN_NAV_EXTENT_XY:
        fail("%s extent too small: (%.0f, %.0f), need >= %d" % (NAV_LABEL, extent.x, extent.y, MIN_NAV_EXTENT_XY))

    if found[PREY_LABEL].get_class() != prey_class:
        fail(PREY_LABEL + " has wrong class: " + str(found[PREY_LABEL].get_class()))

    for label in COVER_LABELS:
        cover = found[label]
        if not isinstance(cover, unreal.StaticMeshActor):
            fail(label + " is not a StaticMeshActor: " + str(cover.get_class()))
        if cover.static_mesh_component.static_mesh != mesh:
            fail(label + " does not use " + CUBE_MESH)


def main():
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not levels.load_level(MAP_PATH):
        fail("load_level failed for " + MAP_PATH)

    mesh = unreal.load_asset(CUBE_MESH)
    prey_class = unreal.load_class(None, PREY_CLASS)
    if mesh is None:
        fail("cannot load " + CUBE_MESH)
    if prey_class is None:
        fail("cannot load " + PREY_CLASS + " (is the editor module built?)")

    existing = actors_by_label()
    missing = [l for l in ALL_LABELS if l not in existing]
    if not missing:
        validate(prey_class, mesh)
        unreal.log("PSL setup_hunt_arena: already present")
        return
    if len(missing) != len(ALL_LABELS):
        fail("partial set, refusing to modify; missing labels: " + ", ".join(missing))

    # Default brush is a 200 uu cube: scale (25, 25, 3) -> 5000 x 5000 x 600 centred on z = 0.
    nav = spawn(unreal.NavMeshBoundsVolume, unreal.Vector(0, 0, 0), NAV_LABEL)
    nav.set_actor_scale3d(unreal.Vector(25, 25, 3))

    spawn(prey_class, unreal.Vector(1800, 600, 100), PREY_LABEL)

    for label, (x, y) in zip(COVER_LABELS, COVER_XY):
        cover = spawn(unreal.StaticMeshActor, unreal.Vector(x, y, 100), label)
        cover.set_actor_scale3d(unreal.Vector(2, 2, 2))
        cover.static_mesh_component.set_static_mesh(mesh)

    validate(prey_class, mesh)
    if not levels.save_current_level():
        fail("save_current_level failed")

    if not levels.load_level(MAP_PATH):
        fail("reload failed for " + MAP_PATH)
    validate(prey_class, mesh)
    unreal.log("PSL setup_hunt_arena: done")


main()
