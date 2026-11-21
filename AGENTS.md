# Agent Rules

## Project truth

- This is an Unreal Engine 5.8 third-person dinosaur survival vertical slice.
- The repository contains C++ gameplay code and configuration only; `.uasset`/`.umap` assets require the Unreal Editor.
- Treat code, builds, tests, screenshots, and runtime behavior as **unverified** until actually exercised on a machine with UE 5.8 and Xcode.

## Boundaries

- Keep gameplay rules in C++ (`Source/PrehistoricSurvivalLab`); keep authored meshes, input assets, maps, and Blueprint wiring in the Editor.
- Do not introduce GAS, Mass, multiplayer infrastructure, or empty placeholder classes in this slice.
- Use Git LFS for `.uasset`, `.umap`, `.fbx`, `.png`, `.wav`, and `.exr` files.
- Test names MUST use the `PSL.` prefix. Run `scripts/run_tests.sh` for automation tests.
- Run only one Unreal Editor/build job at a time; the target machine is memory constrained.
- Never fabricate binary assets, test results, compile results, or editor screenshots. State missing prerequisites plainly.
- Preserve unrelated working-tree changes and do not commit, push, or deploy without explicit instruction.

## Workflow

1. Read the relevant architecture and setup docs before changing cross-module behavior.
2. Prefer the smallest change consistent with the architecture contract.
3. Use `scripts/build.sh` and `scripts/run_tests.sh` once UE 5.8 is installed.
4. Record verification honestly: this project is currently unverified because the required engine/toolchain may be absent.

## Harness (default path for non-trivial requests)

- Any request that changes code or needs a build: run it through the harness, not ad hoc. `bun harness/src/cli.ts start "<objective>"`, then `status` / `resume <runId> [--approve]` / `stop <runId>` / `logs <runId>`.
- A task is done only when the harness records BUILD+TEST gate SUCCESS and an independent reviewer `approve`. Worker claims do not count.
- Never start a UE build outside the harness lock (`.harness/ue-build.lock`); use `bun harness/src/cli.ts gate build|test`.
- Protected operations (commit/push/merge/destructive/visibility) are released only by a human typing a one-time code at `resume <runId> --approve` on a real terminal. Agents cannot approve.
- Role tool limits live in `.omp/roles/*.yml` (OMP overlays via `PI_CONFIG_FILES`). Skills: `.omp/skills/ue-build-test`, `.omp/skills/ue-api-check`.
- Project skills: `ue58-editor-mcp`, `ue58-build-debug`, `ue58-automation-qa`, `ue58-pie-evidence`, `unreal-gameplay-architecture`, `independent-review`.
- Before committing, `scripts/check_secrets.sh` must pass. Never commit a literal `SecurityToken=` in `Config/*.ini`.
- Map: `harness/src/` (state machine `graph.ts`, SQLite `store.ts`, gates `gates.ts`/`classify.ts`, context `context.ts`, Paseo `paseo.ts`). Design: `docs/HARNESS.md`. Paseo scripts: `paseo.json`.
