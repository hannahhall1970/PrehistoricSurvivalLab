# Task 001: Playable Third-Person Sprint & Stamina

Status: CONTRACT (not started). Harness v1 is frozen at commit `db6d2c2`; run through `bun harness/src/cli.ts start`.

## Verified starting point (read from source, not from memory)

Already implemented in C++ and covered by 4 `PSL.*` automation tests:

- `APSLCharacter`: spring arm + follow camera, `Move`/`Look`/`Jump`/`Sprint` handlers, `WalkSpeed=300`, `SprintSpeed=600`, death handling.
- `UPSLSurvivalComponent`: drain 20/s, regen 12/s, `MaxStamina=100`, lock at 0 until Shift is released.
- `UPSLStatsComponent`: health, damage, single death event.
- `APSLHUD`: text-only health/stamina/sprinting.
- `APSLGameMode`: default pawn and HUD; set as `GlobalDefaultGameMode`.

Not implemented (the real work):

1. `MoveAction`, `LookAction`, `JumpAction`, `SprintAction` and `DefaultMappingContext` are `UPROPERTY` slots with **no assets**, so there is no input today. No `.uasset` or `.umap` exists in `Content/`.
2. No openable map.
3. HUD is text only; no health/stamina bars.
4. Nothing proves WASD/Mouse/Shift reach the components end to end.

## Design (small, reusable, no binary-asset dependency where avoidable)

- **Input without Editor-authored assets.** Add a small C++ helper (`PSLInputDefaults`) that builds `UInputAction`/`UInputMappingContext` objects in code (W/A/S/D and arrows -> Move 2D, Mouse XY -> Look, Space -> Jump, Left Shift -> Sprint) and is used by `APSLCharacter` only when the matching UPROPERTY is unset. Assigned assets always win. This keeps input testable in automation and removes the need to hand-author `.uasset` files.
- **Reuse `UPSLSurvivalComponent` / `UPSLStatsComponent` unchanged** unless a test proves a defect.
- **HUD bars.** Extend `APSLHUD::DrawHUD` with `DrawRect` health and stamina bars (fill = value/max, clamped; locked/exhausted state tinted). Extract the fraction/clamp math into a pure function so it is unit-testable.
- **Test map.** Generate `/Game/Maps/L_TestArena` (floor, PlayerStart, light, GameMode override to `APSLGameMode`) with a Python Editor script run through `UnrealEditor-Cmd -ExecutePythonScript`, using engine basic shapes only. Commit the `.umap` via Git LFS (verify with `git lfs ls-files`). If scripted creation is unreliable, fall back to documenting the manual creation steps and mark the item MANUAL, never faked.
- No dinosaur assets, no asset packs, no downloads.

## Acceptance criteria

| # | Criterion | Evidence type |
| --- | --- | --- |
| A1 | C++ build passes via `gate build` | Automated (harness gate) |
| A2 | Existing 4 `PSL.*` tests still pass; new tests below pass; report is fresh, validated by `verify_test_report.py`, and lists every required `PSL.*` name | Automated |
| A3 | `PSL.Input.DefaultBindings`: default context maps W/A/S/D, mouse, Space, Shift to the right actions; assigned assets override defaults | Automated |
| A4 | `PSL.Character.SprintSpeed`: pawn spawned in a test world reaches `SprintSpeed` while sprinting and returns to `WalkSpeed` on release | Automated |
| A5 | `PSL.Hud.BarFraction`: fraction math clamps at 0/1 and handles `Max<=0` | Automated |
| A6 | `L_TestArena` opens in Editor without errors/warnings from our module | Automated if scripted open succeeds; otherwise MANUAL |
| A7 | PIE: WASD moves, mouse rotates camera, holding Shift sprints, stamina drains to 0 in about 5 s, must release Shift to sprint again, regens in about 8.3 s, bars follow values | **MANUAL (human in Editor)** |
| A8 | Independent reviewer approves the diff with disposition per finding | Review |

Rules for evidence: automated results come only from a fresh report validated by the harness; A7 is never claimed from automation. The final report must list A1-A8 each as AUTOMATED-PASS, MANUAL-PENDING, MANUAL-PASS, or FAIL. "C++ compiles" alone is never "gameplay complete".

## Constraints

- One writer, one Unreal build at a time, via `gate build|test` only; incremental builds only; preflight must exit 0 (20 GB floor).
- Do not touch `Config/DefaultEngine.ini` token-prone sections; `AndroidFileServer` stays disabled.
- New test names use the `PSL.` prefix and must be registered so `verify_test_report.py` derives them from `Source/`.
- LFS for `.umap`/`.uasset`. No commit/push without per-operation approval.

## Suggested work split (disjoint files)

- W1: `PSLInputDefaults.{h,cpp}` + `APSLCharacter` fallback wiring + `PSL.Input.*`, `PSL.Character.*` tests.
- W2: `APSLHUD` bars + pure fraction helper + `PSL.Hud.*` test.
- W3 (after W1/W2 merge): map-generation script + `docs/TEST_PLAN.md` update (manual checklist for A7).

## Out of scope

Dinosaurs, AI, hunger/thirst, UMG, replication, save game, new harness features.
