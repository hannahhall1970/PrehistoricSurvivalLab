"""Create /Game/Maps/L_TestArena from engine basic shapes only.

Run (only under the harness build lock, no other Unreal job running):
    UnrealEditor-Cmd PrehistoricSurvivalLab.uproject -ExecutePythonScript=scripts/create_test_arena.py -unattended -nullrhi -nosplash

Contents: cube floor, PlayerStart, DirectionalLight, SkyLight, and the
world-settings GameMode override set to APSLGameMode. Exits non-zero on failure.
"""
import sys

import unreal

MAP_PATH = "/Game/Maps/L_TestArena"
GAME_MODE_CLASS = "/Script/PrehistoricSurvivalLab.PSLGameMode"
CUBE_MESH = "/Engine/BasicShapes/Cube.Cube"  # 100 x 100 x 100 uu, pivot at center


def fail(message):
    unreal.log_error("PSL create_test_arena: " + message)
    sys.exit(1)


def spawn(actor_class, location, rotation=unreal.Rotator(0, 0, 0)):
    actor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).spawn_actor_from_class(
        actor_class, location, rotation
    )
    if actor is None:
        fail("failed to spawn " + str(actor_class))
    return actor


def make_movable(actor, component_class):
    # Dynamic GI is off in DefaultEngine.ini and no lighting is baked: static lights would render black.
    actor.get_component_by_class(component_class).set_mobility(unreal.ComponentMobility.MOVABLE)


def main():
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not levels.new_level(MAP_PATH):
        fail("new_level failed for " + MAP_PATH)

    mesh = unreal.load_asset(CUBE_MESH)
    game_mode = unreal.load_class(None, GAME_MODE_CLASS)
    if mesh is None:
        fail("cannot load " + CUBE_MESH)
    if game_mode is None:
        fail("cannot load " + GAME_MODE_CLASS + " (is the editor module built?)")

    # 5000 x 5000 uu floor, top surface at z = 0.
    floor = spawn(unreal.StaticMeshActor, unreal.Vector(0, 0, -50))
    floor.set_actor_label("Floor")
    floor.set_actor_scale3d(unreal.Vector(50, 50, 1))
    floor.static_mesh_component.set_static_mesh(mesh)

    spawn(unreal.PlayerStart, unreal.Vector(0, 0, 100))

    sun = spawn(unreal.DirectionalLight, unreal.Vector(0, 0, 500), unreal.Rotator(0, -45, 30))
    make_movable(sun, unreal.DirectionalLightComponent)

    sky = spawn(unreal.SkyLight, unreal.Vector(0, 0, 600))
    make_movable(sky, unreal.SkyLightComponent)

    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    world.get_world_settings().set_editor_property("default_game_mode", game_mode)

    if not levels.save_current_level():
        fail("save_current_level failed")
    unreal.log("PSL create_test_arena: saved " + MAP_PATH)


main()
