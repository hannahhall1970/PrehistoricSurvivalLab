# Task 002: Two-Player Network Proof (M1)

Status: CONTRACT (not started). Depends on M0 being integrated into `main` (see `docs/design/ROADMAP.md` M0 progress). Required harness base: `--base <main commit containing 0926409>`; never the harness default `origin/main` (= `db6d2c2`, no Task 001). Run through `bun harness/src/cli.ts start`; one writer, one Unreal build, `gate build|test` only.

Architecture: `docs/design/MULTIPLAYER_ARCHITECTURE.md` §3–§4, §9. Roadmap: `docs/design/ROADMAP.md` (M1).

## Goal

Two separate clients in one live, server-authoritative world: each gets its own pawn, sees the other move, and sees correct Health/Stamina. Nothing else.

## Verified starting point (source read 2026-11-12 at `0926409`)

- `APSLGameMode : AGameModeBase`; default pawn `APSLCharacter`, HUD `APSLHUD`.
- `UPSLStatsComponent`: `Health`, `bIsDead`, `ApplyDamage`; **not replicated**.
- `UPSLSurvivalComponent`: `Stamina`, `bSprintRequested`, `bSprintLocked` (`IsSprintLocked()` added by Task 001), `bIsSprinting`; ticks on every machine; **not replicated**. `APSLHUD::DrawHUD` reads `GetStamina`, `IsSprinting`, `IsSprintLocked` (exhausted tint).
- `APSLCharacter::HandleSprintStateChanged` sets `MaxWalkSpeed` locally → client/server speed mismatch once networked. `PSL.Character.SprintSpeed` (`PSLInputTests.cpp:156`) asserts `MaxWalkSpeed`.
- `ApplyInputDefaults()` (`PSLInputDefaults::Resolve`) runs in `BeginPlay` and `SetupPlayerInputComponent`; the mapping context is still added in `BeginPlay` → on a remote client the controller may not exist yet.
- `scripts/verify_test_report.py` discovers only `IMPLEMENT_*_AUTOMATION_TEST` / `DEFINE_SPEC` names.
- `AGENTS.md` says "no multiplayer infrastructure in this slice". This task is the deliberate exception for M1 and MUST update that line to: "Multiplayer is limited to the M1 contract in docs/tasks/002; no EOS, backend, or dedicated-server targets."

## Scope

| # | Change | Files (expected) |
| --- | --- | --- |
| C1 | Replicate stats: `SetIsReplicatedByDefault(true)`; `Health`, `bIsDead` → `ReplicatedUsing=OnRep_Health/OnRep_Dead`; `ApplyDamage` returns 0 without authority; `OnRep_*` broadcast the same delegates | `PSLStatsComponent.{h,cpp}` |
| C2 | Replicate survival for presentation: `Stamina`, `bIsSprinting`, `bSprintLocked` (all clients; HUD tint needs the lock). `UPSLSurvivalComponent` stops ticking its own drain; it keeps tuning values, delegates, and `UpdateStamina` (still unit-tested by `PSL.Survival.*`; NOT pure today: it mutates `Stamina`/`bIsSprinting`/`bSprintLocked` and broadcasts, `PSLSurvivalComponent.cpp:33-67`) becomes a thin wrapper over a new `static FPSLStaminaStep PSLStamina::Step(float Stamina, bool bLocked, bool bWantsSprint, bool bMoving, float Dt, const FPSLStaminaTuning&)` returning stamina/lock/sprinting and clearing the lock when `!bWantsSprint` (today only `SetSprintRequested(false)` clears it, `:24-31`, outside the move stream); `UpdateStamina` and CMC `OnMovementUpdated` both call `Step`, only the component broadcasts; `bMoving` = the move's rounded `Acceleration` non-zero (sent to the server), not velocity. On the server the CMC writes results into it; `OnRep_*` broadcast the existing delegates on clients. `PSL.Character.SprintSpeed` is updated to assert `GetCharacterMovement()->GetMaxSpeed()` instead of `MaxWalkSpeed` | `PSLSurvivalComponent.{h,cpp}`, `PSLInputTests.cpp` |
| C3 | Sprint **and stamina** inside the CMC move stream. (a) `UPSLCharacterMovementComponent` holds `bWantsToSprint` (input), `SimStamina`, `bSimSprintLocked`; `OnMovementUpdated(DeltaSeconds, …)` runs `PSLStamina::Step` (C2) with the move's `DeltaSeconds` **only when `GetOwnerRole() != ROLE_SimulatedProxy`** (it is also called from `SimulateMovement`). (b) `GetMaxSpeed()` = `SprintSpeed` iff `bWantsToSprint ∧ ¬bSimSprintLocked ∧ SimStamina > 0`. (c) `FSavedMove_PSL`: sprint intent in `FLAG_Custom_0` via `GetCompressedFlags`/`UpdateFromCompressedFlags` (base `CanCombineWith` already rejects differing flags); save start `SimStamina`/`bSimSprintLocked` in a `SetInitialPosition` override (not only `SetMoveFor`: after `CombineWith` the engine calls `NewMove->SetInitialPosition`, `CharacterMovementComponent.cpp:8995`, and a combined move can be combined again), and in `CombineWith`, before `Super`, write `OldMove`'s saved start `SimStamina`/`bSimSprintLocked` back into the CMC (base `CombineWith`, `:13268-13286`, rewinds only position/velocity/base/floor, not CMC stamina; the following `SetInitialPosition` then records the correct start, otherwise the client double-applies the old move's stamina delta); record end `SimStamina`/lock in `PostUpdate(PostUpdate_Record)` (`:9026`). `PrepMoveFor` MUST NOT restore `SimStamina` (it would discard a correction before replay). (d) Correction: custom `FCharacterNetworkMoveData` sends the client's **end-of-move** `SimStamina`/`bSimSprintLocked` from `ClientFillNetworkMoveData` (`:9735`), because the server validates after `PerformMovement` (`:10062-10064`) (`SetNetworkMoveDataContainer`); server `ServerCheckClientError` (override the `FMovementBaseInterfaceData*` overload, `CharacterMovementComponent.h:2439`; the `h:2433` overload is deprecated; neither has a stamina parameter) reads the client values from `GetCurrentNetworkMoveData()` cast to the custom move-data type and returns `Super \|\| \|clientStamina − serverStamina\| > 1 \|\| lock differs`; custom `FCharacterMoveResponseDataContainer` (`SetMoveResponseDataContainer`) serializes server `SimStamina`/lock **only in the correction branch**; client applies them before the replay (`ClientHandleMoveResponse` override, then `Super`; override `ClientUpdatePositionAfterServerUpdate` to save/restore `bWantsToSprint` around the replay, as the base does for `bWantsToCrouch`, `:8655,8718`, since replay's `UpdateFromCompressedFlags` overwrites live intent). No threshold snap from the replicated `Stamina` property. (e) `ObjectInitializer.SetDefaultSubobjectClass<UPSLCharacterMovementComponent>(ACharacter::CharacterMovementComponentName)`; `FNetworkPredictionData_Client_PSL::AllocateNewMove` + `GetPredictionData_Client` override | new `PSLCharacterMovementComponent.{h,cpp}`, `PSLCharacter.{h,cpp}` |
| C4 | Input on possession: add mapping context in `NotifyControllerChanged()` for locally controlled pawns (keep Task 001 defaults) | `PSLCharacter.cpp` |
| C5 | Death on all machines: `OnRep_Dead` → character disables movement; owning client disables input | `PSLCharacter.cpp` |
| C6 | Spawn: two players never spawn overlapping (≥ 2 PlayerStarts in `L_TestArena` or `ChoosePlayerStart` picks an unused start) | `PSLGameMode.{h,cpp}` or map script |
| C7 | Tests (editor-only, guarded `#if ENABLE_PIE_NETWORK_TEST`): CQTest `NETWORK_TEST_CLASS(FPSLNetTests, "PSL.Net")` with `FNetworkComponentBuilder<…>().WithClients(2).AsDedicatedServer().WithGameMode(APSLGameMode::StaticClass())`, steps via `ThenServer/ThenClient/UntilClient(idx, …, Timeout)`. Build.cs: `CQTest` private dep (engine Developer module; no `.uproject` plugin entry needed) plus editor-only `UnrealEd`, `LevelEditor`, `EngineSettings` under `Target.bBuildEditor`, and `SetupIrisSupport(Target)` (header includes Iris headers) — mirror `Engine/Plugins/Tests/CQTest/Source/CQTestTests/CQTestTests.Build.cs` | `Private/Tests/PSLNetworkTests.cpp`, `PrehistoricSurvivalLab.Build.cs` |
| C8 | Test discovery: extend `MACRO_RE` in `scripts/verify_test_report.py`: for each `NETWORK_TEST_CLASS | TEST_CLASS*(Class, "Dir")` collect every `TEST_METHOD(Name)` / `TEST_METHOD_WITH_TAGS(Name, …)` until the next class macro and emit `Dir.Class.Name`, never bare`Dir`(`declared_tests` takes macro arg 2 literals, `:73-76`,`matches()` `:99-100`, so`PSL.Net` alone never matches `PSL.Net.FPSLNetTests.<Method>`); the fixture test asserts the N3–N8 names exactly | `scripts/verify_test_report.py` (+ its test) |
| C9 | Docs: `docs/ARCHITECTURE.md` net section, `docs/TEST_PLAN.md` manual 2-client checklist, `AGENTS.md` line above | docs |

Server RPCs: none required (sprint intent rides the saved move). Any server RPC added MUST be `Server, Reliable|Unreliable` and ALWAYS clamp/reject in `_Implementation`; no `WithValidation`.

## Acceptance criteria

| # | Criterion | Evidence |
| --- | --- | --- |
| N1 | `gate build` SUCCESS (Editor target) | Harness gate |
| N2 | All existing `PSL.*` tests + new ones pass; report validated by updated `verify_test_report.py` listing every `PSL.*` name | Harness gate test |
| N3 | `PSL.Net.FPSLNetTests.Spawn`: server has 2 `APlayerController`s, each possesses a distinct `APSLCharacter`; pawns ≥ 100 cm apart; each client has exactly 1 locally controlled pawn and sees 2 `APSLCharacter`s | CQTest |
| N4 | `PSL.Net.FPSLNetTests.Roles`: on client 0 its pawn is `ROLE_AutonomousProxy`, the other is `ROLE_SimulatedProxy`; on the server both are `ROLE_Authority` | CQTest |
| N5 | `PSL.Net.FPSLNetTests.MovementReplicates`: server moves pawn A by 500 cm; within 2 s client 1 sees A within 10 cm of server location | CQTest |
| N6 | `PSL.Net.FPSLNetTests.HealthReplicates`: server `TakeDamage(25)` on pawn A → both clients read Health 75 within 2 s; client-side `ApplyDamage(50)` changes nothing on server | CQTest |
| N7 | `PSL.Net.FPSLNetTests.DeathReplicates`: lethal server damage → both clients read `IsDead()==true`, `OnDeath` fires once per machine | CQTest |
| N8 | `PSL.Net.FPSLNetTests.StaminaAuthority`: owning client sprints (moving) for 1 s → server and owning client both read Stamina 80 ± 2. Then client sets its `SimStamina=100` while sprint continues; within 2 s the client's `SimStamina` equals the server's ± 1 (stamina error forces a CMC correction via C3d) | CQTest |
| N9 | `PSL.Movement.SavedMoveSprintFlag`: `FSavedMove_PSL` round-trips sprint intent through `GetCompressedFlags`/`UpdateFromCompressedFlags` | Unit test |
| N10 | Manual 2-client PIE (Net Mode *Play As Client*, 2 players, *Run Under One Process* off): both players move, see each other, sprint, stamina drains 5 s / regens ~8.3 s on each HUD; `p.NetShowCorrections 1` shows no persistent corrections while sprinting on flat ground; repeat with `NetEmulation.PktLag 150` and `NetEmulation.PktLoss 2` | **MANUAL**, screenshots/log in `TestReports/` |
| N11 | Manual separate processes: `UnrealEditor … /Game/Maps/L_TestArena -server -port=7777 -log -nullrhi` + 2× `UnrealEditor … 127.0.0.1:7777 -game -log -windowed`; server log shows 2 `Join succeeded`; record peak RSS of each process | **MANUAL** (memory-gated; skip only with stated RSS reason) |
| N12 | Independent reviewer `approve` with per-finding disposition | Review |

Final report lists N1–N12 each as AUTOMATED-PASS / MANUAL-PASS / MANUAL-PENDING / FAIL / BLOCKED. Compilation alone is never "network proof". CQTest full names are `<TestDir>.<ClassName>.<TEST_METHOD>` (`CQTest.h:269` passes `#_ClassName`; `CQTest.inl:214` `TestDir.TestName`; `AutomationTest.cpp:2020` appends the beautified method name), e.g. `PSL.Net.FPSLNetTests.Spawn`. C8's fixture test asserts that format. Record the emitted `fullTestPath` values verbatim.

## Test plan

1. Pure unit (N9) and existing tests first; must stay green.
2. CQTest PIE network suite (N3–N8), dedicated-server PIE, 2 clients, default replication (no Iris). Optional repeat with `WithPacketSimulationSettings` (150 ms, 2 % loss).
3. Manual PIE (N10), then separate processes (N11). Diagnostics: `stat net`, `p.NetShowCorrections 1`, Networking Insights via `-trace=net -NetTrace=1` if corrections appear.

## Technical risks

| Risk | Mitigation |
| --- | --- |
| CQTest PIE tests open a new map and spawn PIE worlds — heavy on 16 GB host | One Unreal job via harness lock; no other agent build concurrently |
| `NETWORK_TEST_CLASS` names not discovered → gate false-pass/false-fail | C8 + fixture test before relying on N2 |
| Stamina simulated per frame instead of per move → corrections at lock edge | C3 per-move simulation; N8 + N10 under 150 ms lag |
| Pawn spawn collision with one PlayerStart | C6; N3 distance check |
| Task 001 not on `main`; harness default base `origin/main` lacks it | Start only with explicit `--base` on a commit that contains `0926409`; worker verifies `git merge-base --is-ancestor 0926409 HEAD` |

## Out of scope (reject in review)

EOS/online subsystems, sessions, server browser, dedicated `Server.Target.cs`, source engine, Iris, Replication Graph, persistence/SaveGame, reconnect, party/friends, hunger/thirst, combat, AI, new species, UMG, harness changes.
