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

## Task 003 hunt arena (status: script RUN by the trusted parent, hunt-arena.json exit 0; in-editor behaviour covered by the PSL.PIE tests below)

`scripts/setup_hunt_arena.py` adds `PSL_NavBounds` (NavMeshBoundsVolume, scale `(25,25,3)`), `PSL_Prey_01` (`PSLPreyCharacter` at `(1800,600,100)`) and cover cubes `PSL_Cover_01..04` (`(±1200,±1200,100)`, scale `(2,2,2)`) to `/Game/Maps/L_TestArena`. It is non-destructive and idempotent by label (full set: validate only; partial set: fails listing missing labels). It saves, reloads and re-validates. Run once by the trusted parent under `.harness/ue-build.lock`:

```
"$UE_ROOT/Engine/Binaries/Mac/UnrealEditor-Cmd" PrehistoricSurvivalLab.uproject \
  -EnablePlugins=PythonScriptPlugin -ExecutePythonScript=<abs>/scripts/setup_hunt_arena.py \
  -ScriptErrorsAreFatal -unattended -nullrhi -nosplash
```

- [x] `Saved/PSL/hunt-arena.json` records the trusted-parent run with exit 0.
- [x] `git lfs ls-files` still lists `Content/Maps/L_TestArena.umap` after the run.

## Task 003 tests (prey AI + bite; status: written, results come from the harness gate only)

Automated (`PSL.` prefix; `scripts/run_tests.sh`). Gate rule: `succeeded` + `succeededWithWarnings` (every `PSL.PIE.*` test, 13 in the last gate run, carries the known warnings `LogAudioMixerAudioUnit` + `LogCrowdFollowing`) must equal the full PSL test count; `PSL_REPORT succeeded` alone excludes the warning tests.

- `PSL.AI.PreyDecision`: pure `DecidePreyState` transitions (dead dominates, flee hysteresis, idle/wander).
- `PSL.Combat.BiteCooldown`: `CanBite` gate (cooldown, never while dead).
- `PSL.Combat.PreyDamageDeath`: default prey stats (60 HP, AI controller class) plus `UPSLStatsComponent::ApplyDamage(25)` on a transient component: three hits kill exactly once (the third is clamped to 10). It does not exercise a bite or `APSLPreyCharacter::TakeDamage`; bites are covered by `PSL.PIE.BiteCombat`.
- `PSL.PIE.PreyWander`: within 12 s prey is in `Wander` and moves more than 150 uu.
- `PSL.PIE.PreyFlee`: player teleported 500 uu from the prey (clear of the covers at +-1200,+-1200); samples until the prey-player distance is > 900 uu (max 6 s) and asserts the state is still `Flee` then.
- `PSL.PIE.PreyReacquire`: regression for `Threat` clearing while the player stays sensed. Prey held still and facing the player; player at 1400 uu for 1 s (sensed, outside FleeRadius 900), then 1750 uu for 1 s (beyond SafeRadius 1600, inside LoseSightRadius 2000), then prey released and player at 600 uu: expects `Flee` within 3 s.
- `PSL.PIE.BiteCombat`: player 180 uu from a held-still prey facing it; LeftMouseButton via `SendKey`: after 0.5 s health 60 -> 35, `BiteCount` 1, anim `ActiveAction` Attack; immediate second press ignored (cooldown); after cooldown two more bites -> `IsDead`, state `Dead`, prey `MovementMode == MOVE_None`, capsule object type `ECC_WorldDynamic`. Screenshots `after_bite`, `prey_dead` are requested only when rendering (`FApp::CanEverRender()`); the `-nullrhi` gate skips them, so screenshots come only from a rendered run.
  - Death pose (D1): `PreyBodyHeight` (`PSLPieTestSupport.h`) = Z of bone `Hips` minus the capsule bottom Z (alive ~50 uu, dead ~20 uu; the mesh is scaled to the capsule). After the killing bite the test takes frames `BiteCombat_death_f00..f15` (one per >= 0.1 s, at most 16 frames or 2.5 s, rendered runs only), then at >= 1.5 s and again at >= 3.5 s after death asserts `PreyBodyHeight <= 0.6 x alive`, anim `ActiveAction == Death` and capsule moved < 5 uu, and takes `prey_dead_hold`. The test sets `VisibilityBasedAnimTickOption = AlwaysTickPoseAndRefreshBones` on the prey mesh (`TrackPreyBones`) because the default `AlwaysTickPose` does not refresh bones under `-nullrhi`; gameplay code is unchanged.
  - D1 root cause: `FAnimInstanceProxy::PostEvaluate` calls `ClearObjects()` every frame and `InitializeObjects()` restored only Idle/Walk/Run, so the Attack/Death action nodes had a null sequence from the second frame and blended the reference (standing) pose at weight 1. Fix: `FPSLDinoAnimProxy::InitializeObjects` also sets `ActionNodes[0]=AttackSequence`, `ActionNodes[1]=DeathSequence` (this also fixes the T-Rex Attack clip).
- `PSL.PIE.BiteMiss`: no pawn in reach -> `BiteCount` 1, `LastBiteHitCount` 0 after the hit delay (`TryBite` sets it to -1 while pending, so 0 proves resolution ran), prey health unchanged.

Each PIE test reports `AddError` (and stops) when the prey is missing, not possessed by `APSLPreyAIController`, or `UNavigationSystemV1` has no nav data. `PSL.PIE.StaminaExhaustion` / `SprintRelease` fixture start moved to `(-2300,-1800)` yaw 45 because `PSL_Cover_03` at `(-1200,-1200)` blocks the old diagonal.

Map: `scripts/setup_hunt_arena.py` is run by the trusted parent, evidence in `Saved/PSL/hunt-arena.json` (gitignored). `Content/Maps/L_TestArena.umap` stays under Git LFS.

MANUAL-PENDING PIE checks (`/Game/Maps/L_TestArena`; not claimed by automation):

- [ ] MANUAL-PENDING: bite feel and reach (BiteReach 220, radius 120, hit delay 0.35 s lines up with the Attack clip).
- [ ] MANUAL-PENDING: prey flee readability (player can see and follow the fleeing prey; speed 450 vs sprint 600).
- [ ] MANUAL-PENDING: prey death pose (body cube rotated 90 deg reads as dead; corpse not a bite target).
- [ ] MANUAL-PENDING: nav coverage around the cover cubes (prey does not get stuck or leave the floor).
- [ ] MANUAL-PENDING: capsule clipping (dino mesh vs prey capsule at bite range).
- [ ] MANUAL-PENDING: camera framing during bite and chase.

## Task 004 Lost Valley (PSL.PIE.LostValley.*)

Source: `Private/Tests/PSLLostValleyPieTests.cpp` (helpers in `PSLPieTestSupport.h`), map `/Game/Maps/L_LostValley`. Screenshots and real perf numbers come from the rendered parent run; the `-nullrhi` gate does not wait for or produce screenshots and its perf numbers are not representative. Screenshots capture the game viewport only and include the UMG HUD.

- `PSL.PIE.LostValley.Loads`: map name, 1 landscape / 16 components, >= 5 HISM components with instances, 3 prey with `APSLPreyAIController`, nav data, a PlayerStart, and the `PSL_Sun/SkyLight/Fog/PostProcess/SkyAtmosphere` lighting actors.
- `PSL.PIE.LostValley.Spawn`: player within 600 uu (2D) of `PSL_PlayerStart` and moving on ground within 3 s; screenshot `after_spawn`.
- `PSL.PIE.LostValley.Explore`: teleport 2500 uu along the first path segment facing the clearing, on ground after 1 s; screenshot `after_explore`.
- `PSL.PIE.LostValley.Hunt`: player placed on ground 600 uu from prey 1 with a clear line of sight to it (distance 450..750); holds 1 s so the spring-arm camera lag settles, then screenshot `after_hunt`.
- `PSL.PIE.LostValley.MoveAndBite`: W moves > 300 uu in 2 s; three bites kill prey A within 6 s, screenshot `after_bite` is taken when prey A dies; prey B (held still and turned toward the player from a spot with a clear line of sight) flees at 500 uu within 4 s.
  - Before `death_side`, step `DeathPose` waits >= 1.5 s after prey A dies and asserts `PreyBodyHeight <= 0.6 x` the standing height measured between the first bite and death (also under `-nullrhi`).
- `PSL.PIE.LostValley.Perf`: three phases (spawn at the PlayerStart, explore at (-6142,-4328), hunt 600 uu from prey 1 with a sight line); each phase has a 1 s settle then 3 s of per-frame `FApp::GetDeltaTime()*1000` samples. Per phase and overall it writes frames, avgFps, avgMs, p50Ms, p95Ms, p99Ms, maxMs, over33ms, over50ms (nearest-rank percentiles; pure helper `ComputeFrameStats` in `PSLPieTestSupport.h`), plus map, RHI, canRender, viewport, `r.VSync`, `GAverageFPS/MS` and scalability levels to `Saved/Automation/PIE/lostvalley-perf.json`. Only when rendering (`FApp::CanEverRender()`) it asserts overall avgFps >= 20 and p95Ms <= 50.
- Polish screenshots (rendered run only; skipped under `-nullrhi`):
  - `MoveAndBite` adds `death_side` after prey A dies: the player stands 350 uu from the corpse on the side away from where the player was (near side if that has no ground), control yaw = bearing from the standing point to the corpse + 20 deg, pitch -30 deg, 1 s settle. Asserts the corpse is within 30 deg of the camera manager's forward, that an `ECC_Visibility` trace from the camera to the corpse bounds centre, ignoring the player pawn, hits the corpse or nothing, and that the camera-to-corpse-centre segment does not hit the player's real body: `LineTraceComponent` against the skeletal mesh (physics-asset bodies; without a physics asset only the capsule applies) OR'd with the capsule, not the AABB from `GetComponentsBoundingBox()`, which always covered the corpse. One retry at yaw -20 deg. Pitch is restored to 0 afterwards.
  - `Explore` adds a camera sweep `cam_450`, `cam_650`, `cam_800` (`USpringArmComponent::TargetArmLength` at the same spot, 1 s settle each, default restored afterwards).
  - `Spawn` runs `hud_1080` only when the PIE viewport is in its own window (not the editor root window; otherwise it logs `hud_1080 skipped: PIE not in new window` and passes). It stores the client size, corrects `GetClientSizeInScreen()` by the (1920,1080) minus viewport delta in up to 3 passes with a 0.5 s wait each, asserts exactly 1920x1080, takes `hud_1080` (`LostValley_Spawn_hud_1080.png`; shot names never carry a size suffix), and always restores the stored client size.
- Polish behaviour covered by existing tests: `PSL.Hud.Widget` asserts the `value / max` texts and that the kill banner (`PREY DOWN`, shown 2.5 s when the player kills prey) and the hit marker (`HIT`, 0.4 s on a landed bite) start collapsed. The prey `!` marker shows while the prey is in `Flee` (the state enum has no Alert state). The default camera is a 650 uu arm with socket offset (0,70,110).
- Every `PSL.PIE.LostValley.*` test calls `SetIgnoreLookInput(true)` on the player controller right after the pawn is usable and `ResetIgnoreLookInput()` in a step queued before `QueuePieEnd` (it runs even when an earlier step timed out). Physical mouse input in the GUI PIE window otherwise rotates the camera mid-capture (C10 `LostValley_Explore.json`). `SetControlRotation` calls in the tests are not look input and still apply.

Manual visual acceptance (MANUAL-PENDING; not claimed by automation):

- [ ] MANUAL-PENDING: terrain, foliage, path and lighting read as a lost valley at the default camera.
- [ ] MANUAL-PENDING: prey placement in the meadow and frame rate feel acceptable on the target machine.

## Task 004 M2a-core (species, hunger, save, flow; status: written, results come from the harness gate only)

Sources: `Private/Tests/PSLSpeciesHungerTests.cpp`, `PSLSaveTests.cpp`, `PSLFlowPieTests.cpp`, plus the Pause cases added to `PSL.Input.DefaultBindings`. Save tests use slot `PSL_Test` through `UPSLSaveSubsystem::SlotName`; the user's `PSL_Slot0` is never touched.

- `PSL.Species.Builtin`: both built-in species values; the T-Rex species equals the `APSLCharacter` constructor defaults.
- `PSL.Species.Apply`: `ApplySpecies(Para)` sets speeds, capsule, health, stamina, hunger and food multiplier; null is ignored.
- `PSL.Hunger.DrainAndStarve`: idle drain, 2x while sprinting, `AddFood` cap, starvation damage through `UPSLStatsComponent`, death, and no drain once dead.
- `PSL.Hunger.Diet`: `EatMeat` (live prey 0, clamped last bite, carcass lifespan), `APSLFoodPlant::Consume` and regrow gate, diet per species.
- `PSL.Hunger.RestoreSurvivesBeginPlay`: `RestoreState` clamps and `ResetVitalsForPlay` (BeginPlay) no longer overwrites restored stamina/hunger.
- `PSL.Save.RoundTrip`: write, load, every field equal, delete. The subsystem is owned by a real `UPSLGameInstance` (strongly held), never a transient-package Outer.
- `PSL.Save.DiskRoundTrip`: a writer GameInstance saves slot `PSL_Test` and is released; a fresh GameInstance and subsystem reads the slot from disk, every field equal, then deletes it.
- `PSL.Save.Validation`: unknown species, wrong map, NaN and out-of-bounds transform, health <= 0 or above max, version 0 and Current+1, checksum tamper; stamina and hunger are clamped, not rejected.
- `PSL.Save.NoDeadOverwrite`: `CanCheckpoint` gate (dead, zero health, starving below 10% HP); `SaveCheckpoint` without a live player leaves the slot unchanged.
- `PSL.PIE.Species.ParaInValley` / `PSL.PIE.Species.TRexInValley`: PIE starts on `L_TestArena`; the test calls `UPSLGameInstance::StartNewGame(species)` (the species-screen API, which opens `L_LostValley`), waits for the new world's pawn (`FPSLPieCtx::RequiredMap`), and compares mesh asset path, capsule radius/half-height, walk and sprint speed with `MakeBuiltin(species)`. The test never calls `ApplySpecies`.
- `PSL.PIE.Hunger.EatCarcass`: kill a prey in `L_TestArena`, bite the corpse, hunger rises and meat falls.
- `PSL.PIE.PauseGameOver`: the `P` key (same `PSL_DefaultPause` action as Escape) pauses the world; resume; zero hunger plus lethal damage; Game Over must be reached within 3.0 s of death (`GameOverDelay` is 2.0 s). Escape is not sent in editor PIE: it is the editor's Stop-Play-In-Editor chord and tore the session down (run 35928092). `PSL.Input.DefaultBindings` still verifies Escape and P are both mapped. `FPSLPieCtx` holds weak object pointers, so a lost PIE world fails the test instead of crashing it.
- `PSL.Hunger.RestoreBroadcasts`: `RestoreState` fires `OnStaminaChanged` / `OnHungerChanged` with the clamped restored values; `UPSLStatsComponent::SetHealth` clamps, broadcasts `OnHealthChanged` only on change and never marks the owner dead (save load must reach the HUD).
- `PSL.UI.Style`: palette constants equal the spec hex values; `Font()` honours the size and uses the project font only when `HasProjectFont()`, else the engine fallback.
- `PSL.UI.Screens`: MainMenu, SpeciesSelect, Pause and GameOver build their trees and expose the expected buttons (Settings is Collapsed until M2b2); species Next/Previous wrap and change the shown species; all six stat bars stay in [0,1] and the better species fills each bar; GameOver titles (`YOU STARVED` / `YOU DIED`) and `mm:ss` formatting.
- `PSL.PIE.Menu.Quit`: in `L_TestArena` PIE the main menu is added to the viewport; Continue is disabled without a save and enabled after `SaveCheckpoint` (test slot `PSL_Test_Shell`, never `PSL_Slot0`); a click on Quit calls `UPSLGameInstance::OnQuitRequested`. `PSLPie::ClickWidget` sends real Slate mouse events when rendering; under `-nullrhi` (zero geometry) it broadcasts `UButton::OnClicked` and logs `[fallback]`; a rendered run asserts the real path was used.
- `PSL.PIE.Menu.PauseGameOver`: `P` (never Escape) shows the Pause widget; clicking Resume returns to Playing and removes it; lethal starvation shows the Game Over widget titled `YOU STARVED`; clicking Retry loads `L_LostValley` with a live pawn at full hunger.

Prey respawner (`APSLGameMode`): in a world that placed prey, 45 s after a carcass is destroyed a replacement spawns on the NavMesh 3000-6000 uu from the player, so the living count returns to the placed count. `L_TestArena` PIE tests are unaffected: no carcass is destroyed within 47 s there. Not covered by an automated test (a 45 s wait); MANUAL-PENDING.
