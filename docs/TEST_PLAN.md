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

## Task 002 Dinosaur (status: MANUAL-PENDING; nothing below is verified)

Quaternius dinosaur mesh + Idle/Walk/Run locomotion. Licence: Quaternius "Animated Lowpoly Dinosaurs", CC0 (public domain), <https://quaternius.itch.io/animated-lowpoly-dinosaurs>.

Import (one-shot; run only under `.harness/ue-build.lock`, one Unreal process at a time):

```
"$UE_ROOT/Engine/Binaries/Mac/UnrealEditor-Cmd" PrehistoricSurvivalLab.uproject \
  -EnablePlugins=PythonScriptPlugin -ExecutePythonScript=<abs>/scripts/import_dino.py \
  -ScriptErrorsAreFatal -unattended -nullrhi -nosplash
```

`<abs>` is the absolute repository path. `-EnablePlugins=PythonScriptPlugin` is required (plugin is not enabled in the `.uproject`). See "Task 002 D3" below for the import result.

Manual PIE checklist (`/Game/Maps/L_TestArena`; record engine version and result per line):

- [ ] MANUAL-PENDING: Idle plays when the character is at rest.
- [ ] MANUAL-PENDING: Walk plays at walk speed (~300).
- [ ] MANUAL-PENDING: Run plays while sprinting (~600).
- [ ] MANUAL-PENDING: Idle/Walk/Run transitions are smooth (no pops or T-pose).
- [ ] MANUAL-PENDING: Feet rest on the floor; no capsule clipping (mesh not sunk or floating).
- [ ] MANUAL-PENDING: Camera framing is correct for the dinosaur's size.
- [ ] MANUAL-PENDING: Stamina, health and HUD behave unchanged versus A7.
- [ ] MANUAL-PENDING: On death, movement stops (no locomotion animation while dead).

### Task 002 D3: imported T-Rex assets (status: import AUTOMATED-RUN, in-editor behaviour MANUAL-PENDING)

Import command: the `UnrealEditor-Cmd` line above with `scripts/import_dino.py`, import scale `0.16`. It was run once by the trusted parent under `.harness/ue-build.lock` (exit 0, repo HEAD `449190782644a34c2ac24d42df52cf06d4a62ca6`); evidence in `Saved/PSL/dino-import.json` (gitignored) and `.harness/evidence/task002-dino/d3/import-trusted.log`. Existence, non-zero size and `git check-attr filter` = `lfs` were checked for every file below; nothing was opened in the Editor and nothing is claimed PASS.

Content/Characters/Dino (13 `.uasset`, all `filter: lfs`):

- Required: `SK_Trex`, `SKEL_Trex`, `A_Trex_Idle` (2.500 s), `A_Trex_Walk` (1.375 s), `A_Trex_Run` (0.917 s).
- Extra: materials `Black`, `Green`, `LightGreen`, `LightYellow`, `Red`; clips `TrexArmature_TRex_Attack`, `TrexArmature_TRex_Death`, `TrexArmature_TRex_Jump`.

Import log reports mesh bounds: origin `(0, -17.4, 121.7)`, box extent `(63.6, 248.2, 121.8)`. Use it as input for the capsule-clipping and framing checks below (capsule radius 60).

Manual PIE checklist (`/Game/Maps/L_TestArena`; record engine version and result per line):

- [ ] MANUAL-PENDING: Idle/Walk/sprint-run blend plays correctly (Idle at rest, Walk ~300, Run ~600, no pops or T-pose).
- [ ] MANUAL-PENDING: Feet rest on the floor (mesh not sunk or floating).
- [ ] MANUAL-PENDING: Tail and head do not clip through or poke far outside the capsule (r=60).
- [ ] MANUAL-PENDING: Camera framing is correct for the dinosaur's size.

## Task 003 hunt arena (status: script WRITTEN, NOT RUN; nothing below is verified)

`scripts/setup_hunt_arena.py` adds `PSL_NavBounds` (NavMeshBoundsVolume, scale `(25,25,3)`), `PSL_Prey_01` (`PSLPreyCharacter` at `(1800,600,100)`) and cover cubes `PSL_Cover_01..04` (`(±1200,±1200,100)`, scale `(2,2,2)`) to `/Game/Maps/L_TestArena`. It is non-destructive and idempotent by label (full set: validate only; partial set: fails listing missing labels). It saves, reloads and re-validates. Run once by the trusted parent under `.harness/ue-build.lock`:

```
"$UE_ROOT/Engine/Binaries/Mac/UnrealEditor-Cmd" PrehistoricSurvivalLab.uproject \
  -EnablePlugins=PythonScriptPlugin -ExecutePythonScript=<abs>/scripts/setup_hunt_arena.py \
  -ScriptErrorsAreFatal -unattended -nullrhi -nosplash
```

- [ ] MANUAL-PENDING: log contains `PSL setup_hunt_arena: done` (first run) or `already present` (re-run), exit 0.
- [ ] MANUAL-PENDING: `git lfs ls-files` still lists `Content/Maps/L_TestArena.umap` after the run.
