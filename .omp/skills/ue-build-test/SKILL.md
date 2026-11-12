---
name: ue-build-test
description: Build or test PrehistoricSurvivalLab (UE 5.8, Mac). Use harness gates, never raw Build.sh/Editor.
---

# UE build/test via harness gates

Canary: PSL-SKILL-UEBT

- Build: `bun harness/src/cli.ts gate build` (or Paseo script `ue-build`). Test: `bun harness/src/cli.ts gate test` (`ue-test`).
- Exit codes: 0 pass, 1 failure, 2 blocked (toolchain/disk). Read `failure.cls`, `failure.excerpt`, `logPath` from the JSON output; open the full log only if the excerpt is insufficient.
- Test verdict comes from `TestReports/<ts>/index.json` (`PSL_REPORT:` line). Zero tests or any non-`Success` state fails.
- One UE build/editor at a time: the gate holds `.harness/ue-build.lock`. `BUILD_LOCKED` means wait; never delete the lock while its pid is alive.
- Never delete `DerivedDataCache`, `Intermediate`, or engine caches to free disk; report the blocker.
- Never edit `.uasset`/`.umap` as text. New tests use the `PSL.` prefix.
- Implementer agents: do not run gates yourself; the harness runs them after your edit.
