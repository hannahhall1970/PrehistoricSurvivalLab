"""Create /Game/Maps/L_MainMenu: a minimal functional main-menu map.

Run (only under the harness build lock, no other Unreal job running):
    UnrealEditor-Cmd PrehistoricSurvivalLab.uproject -ExecutePythonScript=scripts/create_main_menu.py -unattended -nullrhi -nosplash

Contents: cube floor, DirectionalLight + SkyLight (movable), one APSLMenuCameraRig, one APSLMenuPreviewActor,
and the world-settings GameMode override set to APSLMenuGameMode. Saves, reloads the map and verifies it.
Exits non-zero on failure. Existing map is replaced.

This is the minimal functional map UPSLGameInstance::ReturnToMenu() and PSL.PIE.Menu.FullFlow need.
The art-directed menu scene (backdrop, lighting, camera framing) is M2c.
"""
import sys

import unreal

MAP_PATH = "/Game/Maps/L_MainMenu"
GAME_MODE_CLASS = "/Script/PrehistoricSurvivalLab.PSLMenuGameMode"
RIG_CLASS = "/Script/PrehistoricSurvivalLab.PSLMenuCameraRig"
PREVIEW_CLASS = "/Script/PrehistoricSurvivalLab.PSLMenuPreviewActor"
CUBE_MESH = "/Engine/BasicShapes/Cube.Cube"  # 100 x 100 x 100 uu, pivot at center


def fail(message):
    unreal.log_error("PSL create_main_menu: " + message)
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


def load_cls(path):
    cls = unreal.load_class(None, path)
    if cls is None:
        fail("cannot load " + path + " (is the editor module built?)")
    return cls


def count_of(actors, cls):
    return len([a for a in actors if a.get_class() == cls])


def main():
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    mesh = unreal.load_asset(CUBE_MESH)
    if mesh is None:
        fail("cannot load " + CUBE_MESH)
    game_mode = load_cls(GAME_MODE_CLASS)
    rig_class = load_cls(RIG_CLASS)
    preview_class = load_cls(PREVIEW_CLASS)

    if not levels.new_level(MAP_PATH):
        fail("new_level failed for " + MAP_PATH)

    # 5000 x 5000 uu floor, top surface at z = 0.
    floor = spawn(unreal.StaticMeshActor, unreal.Vector(0, 0, -50))
    floor.set_actor_label("Floor")
    floor.set_actor_scale3d(unreal.Vector(50, 50, 1))
    floor.static_mesh_component.set_static_mesh(mesh)

    sun = spawn(unreal.DirectionalLight, unreal.Vector(0, 0, 500), unreal.Rotator(0, -45, 30))
    make_movable(sun, unreal.DirectionalLightComponent)
    sky = spawn(unreal.SkyLight, unreal.Vector(0, 0, 600))
    make_movable(sky, unreal.SkyLightComponent)

    # Preview dino at the origin (feet on the floor, faces +X); the rig looks at it from 800 uu along -X, slightly above.
    # Rotator(roll, pitch, yaw): yaw 0 = looking down +X.
    preview = spawn(preview_class, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 180))
    preview.set_actor_label("MenuPreview")
    rig = spawn(rig_class, unreal.Vector(-800, 0, 160), unreal.Rotator(0, -5, 0))
    rig.set_actor_label("MenuCameraRig")

    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    world.get_world_settings().set_editor_property("default_game_mode", game_mode)

    if not levels.save_current_level():
        fail("save_current_level failed")
    if not levels.load_level(MAP_PATH):
        fail("reload failed for " + MAP_PATH)

    # Verify the reloaded map by real actor class.
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    problems = []
    for name, cls in (("MenuCameraRig", rig_class), ("MenuPreview", preview_class)):
        if count_of(actors, cls) != 1:
            problems.append("expected exactly 1 %s, got %d" % (name, count_of(actors, cls)))
    for name, cls in (("DirectionalLight", unreal.DirectionalLight), ("SkyLight", unreal.SkyLight)):
        if not any(isinstance(a, cls) for a in actors):
            problems.append("missing " + name)
    if not any(a.get_actor_label() == "Floor" for a in actors):
        problems.append("missing Floor")
    gm = world.get_world_settings().get_editor_property("default_game_mode")
    if gm is None or "PSLMenuGameMode" not in gm.get_path_name():
        problems.append("world default game mode is not PSLMenuGameMode: %s" % (gm,))
    if problems:
        fail("; ".join(problems))
    unreal.log("PSL create_main_menu: saved and verified " + MAP_PATH)


main()
