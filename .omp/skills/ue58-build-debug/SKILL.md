---
name: ue58-build-debug
description: Use for UE 5.8 Mac builds, harness gate failures, UBT/Xcode diagnostics, or compile debugging.
---

# Purpose / trigger

Use when a UE 5.8 C++ build or harness gate fails. Read and reuse `ue-build-test` for the gate contract and `ue-api-check` before relying on an engine signature.

# UE 5.8 workflow

- Request/inspect `bun harness/src/cli.ts gate build`; implementer agents do not invoke the gate directly. Never invoke `Build.sh`, UnrealEditor, or `scripts/build.sh` directly.
- The gate owns `.harness/ue-build.lock`; one repository writer and one UE build at a time.
- UE 5.8.4 is installed at `/Users/Shared/Epic Games/UE_5.8`; this Mac requires Xcode 26.1.x. Xcode 26.4+ is unsupported by project preflight.
- Read JSON `failure.cls`, `failure.excerpt`, and `logPath` first. Full gate logs are in the reported `.harness/logs` path; UE build tooling’s Mac local-log root is `~/Library/Logs/Unreal Engine/LocalBuildLogs/`, and project runtime logs are under `Saved/Logs/` when present.

# Failure classification

`harness/src/classify.ts` defines: `TOOLCHAIN_MISSING`, `DISK_LOW`, `TEST_FAILURE`, `COMPILE_ERROR`, `LINK_ERROR`, and `UNKNOWN`.

- Toolchain: report the exact missing UE/Xcode/SDK message; do not repair global configuration.
- Disk: report `DISK_LOW` and free space; never delete DDC, Intermediate, or engine caches.
- Compile: fix the first actionable source/header/module error, then rerun the harness gate.
- Link: check module dependencies and duplicate/undefined symbols; use `ue-api-check` for ownership.
- Test/unknown: inspect the excerpt and the fresh report; do not infer success from Editor exit code.

# Official API refs

Verified installed UE source: `/Users/Shared/Epic Games/UE_5.8/Engine/Source/Runtime/Core/Public/Misc/AutomationTest.h`, `/Users/Shared/Epic Games/UE_5.8/Engine/Source/Runtime/Engine/Classes/GameFramework/Character.h`, and `/Users/Shared/Epic Games/UE_5.8/Engine/Source/Developer/FunctionalTesting/Public/AutomationBlueprintFunctionLibrary.h`.
Official MCP reference (for Editor-assisted diagnostics): <https://dev.epicgames.com/documentation/unreal-engine/unreal-mcp-in-unreal-editor?application_version=5.8>

# Verification / evidence

A successful gate is the only build proof. Preserve its log path, classifier, and timestamp; rerun `bun harness/src/cli.ts gate build` after the fix. Evidence statuses are `AUTOMATED_PASS`, `VISUAL_VERIFIED`, `MANUAL_PENDING`, `MANUAL_PASS`, `FAIL`, `BLOCKED`, `STALE`. Use `bun scripts/evidence.ts init --run <runId> --task <taskId>`, `gate <dir> build`, and `verify <dir>`; use `show <dir>` to inspect the bundle.

# Safety

Never bypass the harness lock, fabricate a compile result, or claim gameplay from compilation. Preserve unrelated dirty files and escalate toolchain/disk blockers plainly.
