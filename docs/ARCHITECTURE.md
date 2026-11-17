# Architecture

## Module

`PrehistoricSurvivalLab` depends on Core, CoreUObject, Engine, InputCore, EnhancedInput, AIModule, NavigationSystem, GameplayTasks, UMG, Slate, SlateCore, and Landscape (plus UnrealEd in editor builds). No GAS, Mass, plugin, or multiplayer infrastructure is required for this slice.

## Responsibilities

| Class | Responsibility |
| --- | --- |
| `UPSLStatsComponent` | Owns MaxHealth/Health; applies clamped positive damage; broadcasts health changes and one death delegate. No tick. |
| `UPSLSurvivalComponent` | Owns stamina and sprint request state; pure `UpdateStamina` drains while requested and moving, regenerates otherwise, clamps values, and broadcasts state/value transitions. |
| `APSLCharacter` | Owns stats, survival, spring arm, and camera; consumes Enhanced Input; maps sprint state to movement speed; routes `TakeDamage`; disables movement/input on death. |
| `APSLGameMode` | Uses `APSLHUD` and falls back to `APSLCharacter`; Blueprint configuration supplies `BP_PSLCharacter` as the default pawn. |
| `APSLHUD` | Creates `UPSLHudWidget` in `BeginPlay` and adds it to the viewport; keeps the pure `BarFraction` helper. |
| `UPSLHudWidget` | UMG HUD built entirely in C++ (no UMG asset): `Initialize()` constructs the widget tree (icon + label + `value / max` rows for health/stamina, sprint state, DEAD text, a `PREY DOWN` kill banner and a `HIT` marker) when the root is null, so it works without a PlayerContext; `NativeTick` reads the owning `APSLCharacter` and rebinds `OnBiteHit`; the banner binds `APSLPreyCharacter::OnAnyPreyKilled` in `NativeConstruct` (after removing any earlier handle) and unbinds in `NativeDestruct`. |
| `UPSLLevelBuildLibrary` | Editor-only (`WITH_EDITOR`) helpers for level-building scripts: `CreateLandscape` and `AddMeshInstances` (HISM). Not available in packaged builds. |
| `UPSLDinoSpecies` | Playable species data (`UPrimaryDataAsset`, type `PSLSpecies`): diet, body, speeds, bite, hunger. `MakeBuiltin(TRex/Para)` builds code defaults so tests need no assets; `APSLCharacter::ApplySpecies` copies them onto the pawn. |
| `APSLFoodPlant` | Herbivore food: a query-only `BiteVolume` sphere (WorldDynamic, overlap) is what the bite finds; the mesh is visual only. `Consume()` yields `FoodAmount`, hides, regrows after `RegrowSeconds`. `L_LostValley` places six (`PSL_FoodPlant_00..05`) beside the path 15-45 m from the PlayerStart (`FOOD_PLANT_RANGE` 1500-4500 cm). Herbivore `APSLCharacter` polls (0.2 s timer, `UpdateProximityHint`) for an available plant within `BiteRadius + 120` of the bite point; when it finds one it clears any stale transient bite hint (e.g. "cannot eat meat") and `GetFoodHint()` returns "Bite to eat"; carnivores never get it. Valley foliage materials (`M_PSL_Leaf`, `M_PSL_FoliageFlat`) dither near the camera and, inside a screen-space ellipse around the pawn and in front of it, down to `CameraFadeFloor` so leaves never hide the dinosaur; the valley cloud runs reduced view/reflection sample scales (0.375 / 0.104). |
| `UPSLSurvivalComponent` (hunger) | `Hunger` drains per second (2x while sprinting); at 0 it damages the owner through `UPSLStatsComponent`; `AddFood` refills. `APSLCharacter::TryEat` feeds it from carcasses (carnivore) or plants (herbivore). |
| `UPSLGameInstance` | Session state across maps: selected species, continue request, run stats; `StartNewGame`, `ContinueGame`, `ReturnToMenu`, `QuitGame`. Set as `GameInstanceClass`. |
| `UPSLSaveGame` / `UPSLSaveSubsystem` | Versioned checkpoints (user 0) with a CRC32 checksum in two ping-pong pairs off the base `SlotName` (default `PSL_Slot0`): manual (Pause, Save and Exit) `PSL_Slot0` / `PSL_Slot0_B`, autosave `PSL_Slot0_Auto0` / `PSL_Slot0_Auto1`. A write goes to the invalid or older member of its pair, never over the newest valid one, then is read back (checksum must equal what was written, else `PSL save verify failed` and false), so a torn write cannot destroy the last good save. Continue (`LoadAndValidate`, `HasValidSave`) takes the newest valid `SavedAtUtc` of all four slots and skips invalid ones. `Validate` rejects bad version, species, map, transform, health, checksum; `CanCheckpoint` refuses saves while dead or starving at low health. Autosave additionally needs `SecondsToLive` (idle `Hunger / drain + Health / starveDamage`) >= `MinAutosaveSecondsToLive` (60 s), so a doomed state is never the Continue target. Prey are not saved. |
| `APSLGameMode` (flow) | State `Playing / Paused / GameOver` (pause via `SetGamePaused`, Game Over 2 s after the player dies), applies the session species and the continue save to the new pawn, 60 s autosave, run stats, and the prey respawner. |

## Data flow

- Input → `SetSprintRequested` → `UPSLSurvivalComponent::UpdateStamina` → `OnSprintStateChanged` → character movement `MaxWalkSpeed` (`300` walk / `600` sprint).
- `AActor::TakeDamage` override → `UPSLStatsComponent::ApplyDamage` → `OnHealthChanged`; reaching zero broadcasts `OnDeath` once → character disables input and movement and logs once.
- Survival tick derives movement from owner velocity (`Size2D`) but the pure update method remains directly testable.

## Extension points

- Add hunger/thirst as separate survival state and delegates; do not overload stamina semantics.
- Add a combat component whose damage output enters through `TakeDamage`.
- Add dinosaur AI with Behavior Trees or StateTree after the player loop is stable.
- For replication, make authoritative health/stamina changes server-owned, replicate state or attributes, and multicast only presentation events; first establish a networking contract before adding infrastructure.

## Level build order

Run the build script first, then the polish script. `scripts/polish_lost_valley.py` does `sys.path.insert(0, HERE)` before `import build_lost_valley`.
