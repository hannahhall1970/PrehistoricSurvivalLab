# Test Plan

## Automated (`PSL.`)

Run `./scripts/run_tests.sh` after installing UE 5.8. Tests cover:

- Initial health/stamina values.
- Sprint transitions and delegate transitions.
- One second sprint drain: stamina `100 → 80`; one second regeneration: `80 → 92`.
- Clamping at zero and maximum; no drain while not moving.
- Empty stamina stops sprinting and requires releasing and re-pressing sprint.
- Positive damage, health clamp, no negative health, zero/negative damage ignored.
- Death broadcasts exactly once; damage after death is ignored.

## Manual Editor checklist

- Walk speed is `300`; sprint speed is `600`.
- Hold sprint while moving: stamina empties after `5` seconds from full.
- Full stamina regeneration takes about `8.33` seconds (`100 / 12`).
- Releasing and re-pressing sprint is required after empty stamina.
- Damage reaches StatsComponent; health reaches zero once; character disables movement/input.
- HUD displays health, stamina, sprinting state, and `DEAD`.
- Test map loads with `BP_PSLCharacter` as the default pawn and damage volume behaves as configured.

Record actual engine/editor version and results. Until the required toolchain is installed, all checks are unverified.

## A6: `L_TestArena` map (status: AUTOMATED-PASS for create + load; PIE is A7)

`Content/Maps/L_TestArena.umap` was created by `scripts/create_test_arena.py` through a one-shot, human-approved, SHA256-pinned `UnrealEditor-Cmd` run under the harness build lock (the implementer shell still blocks `*UnrealEditor*`; that guard was not changed).

```
"$UE_ROOT/Engine/Binaries/Mac/UnrealEditor-Cmd" PrehistoricSurvivalLab.uproject \
  -EnablePlugins=PythonScriptPlugin -ExecutePythonScript=scripts/create_test_arena.py \
  -ScriptErrorsAreFatal -unattended -nullrhi -nosplash
```

`-EnablePlugins=PythonScriptPlugin` is required: the plugin is not enabled in the `.uproject` (left unchanged on purpose).

Load check (second approved read-only run): a script loads `/Game/Maps/L_TestArena`, asserts one each of `PlayerStart`, `DirectionalLight`, `SkyLight`, a `StaticMeshActor` floor using `/Engine/BasicShapes/Cube`, and `WorldSettings.DefaultGameMode == /Script/PrehistoricSurvivalLab.PSLGameMode`, without saving. Both runs exited 0 with no `LogPSL`, `LogPython`, map-check or save errors. The logs do contain unrelated engine lines: `LogTemp` "Error test" self-test entries at startup, a CoreAudio `Error querying Sample Rate` warning from the null audio device, and (creation run only) an auto-dismissed "Unable to Check Out From Revision Control" dialog for the new, untracked map; the save still succeeded.

Re-running the creation script fails by design when the map already exists (`new_level` rejects an existing package). The map is committed through Git LFS (`git lfs ls-files` lists `Content/Maps/L_TestArena.umap`, oid `304c6645d8`).

Manual creation if the script is ever unavailable:

1. Open the project in the Editor and build the module. File > New Level > Empty Level.
2. Add a Cube (Quick Add > Shapes > Cube) at `(0,0,-50)`, scale `(50,50,1)` as the floor.
3. Add a Player Start at `(0,0,100)`, a Directional Light and a Sky Light. Set both lights to Movable (Dynamic GI is off in `DefaultEngine.ini`).
4. World Settings > GameMode Override = `PSLGameMode`.
5. Save as `/Game/Maps/L_TestArena`, confirm `git lfs ls-files` lists it, and check the Output Log for `LogPSL` errors or warnings.

## A7: manual PIE checklist (status: MANUAL-PENDING)

Open `/Game/Maps/L_TestArena`, press Play (PIE). Record the engine version and the result of each line. Not claimed by automation.

- [ ] W/A/S/D (and arrow keys) move the character.
- [ ] Mouse rotates the camera.
- [ ] Holding Left Shift sprints (speed 600 vs. 300 walking).
- [ ] While sprinting and moving, stamina drains to 0 in about 5 s.
- [ ] At 0 stamina sprint stops; holding Shift does not resume it. Shift must be released and pressed again.
- [ ] Stamina regenerates from 0 to full in about 8.3 s.
- [ ] HUD health and stamina bars follow the values; stamina bar turns orange while exhausted (Shift still held at 0).
