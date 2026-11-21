---
name: unreal-gameplay-architecture
description: Use for PSL C++ gameplay boundaries, component seams, pure-function extraction, or architecture decisions.
---

# Purpose / trigger

Use when changing gameplay structure or deciding where new behavior belongs. Keep the vertical slice small and testable; read `AGENTS.md` and the task contract first.

# Boundaries

- Gameplay rules live in C++ under `Source/PrehistoricSurvivalLab`; authored meshes, maps, input assets, and Blueprint wiring belong in the Editor.
- Keep this slice free of GAS, Mass, multiplayer/replication infrastructure, and empty placeholders.
- Existing seams are `APSLCharacter`, `UPSLSurvivalComponent`, `UPSLStatsComponent`, `APSLHUD`, and `APSLGameMode`.
- `APSLCharacter` owns input/movement and delegates stamina/health to components; `UPSLSurvivalComponent` owns sprint request, drain, regen, and zero-stamina release lock; `UPSLStatsComponent` owns health, damage, and one death event; `APSLHUD` renders; `APSLGameMode` selects defaults.

# UE 5.8 workflow

- Preserve assigned `UPROPERTY` assets as the first choice; put deterministic fallback defaults in a narrow helper, not in unrelated components.
- Extract clamp/fraction/time rules as pure functions with no UObject/world access, then cover edge cases in `PSL.*` automation tests.
- Keep cross-module changes explicit: verify declarations in installed headers and add only the owning module dependency required by the API.
- Use `APSLCharacter`/components for gameplay state, not HUD or GameMode globals. Use delegates for state changes already exposed by the components.

# Official API refs

- Verified installed headers: `/Users/Shared/Epic Games/UE_5.8/Engine/Source/Runtime/Engine/Classes/GameFramework/Character.h`, `/Users/Shared/Epic Games/UE_5.8/Engine/Source/Runtime/Engine/Classes/Components/ActorComponent.h`, `/Users/Shared/Epic Games/UE_5.8/Engine/Source/Runtime/Engine/Classes/GameFramework/HUD.h`, `/Users/Shared/Epic Games/UE_5.8/Engine/Source/Runtime/Engine/Classes/GameFramework/GameModeBase.h`, and `/Users/Shared/Epic Games/UE_5.8/Engine/Source/Runtime/Core/Public/Misc/AutomationTest.h`. Official MCP guide: <https://dev.epicgames.com/documentation/unreal-engine/unreal-mcp-in-unreal-editor?application_version=5.8>

# Verification / evidence

Every new test name starts `PSL.`. Build/test proof comes only from fresh harness gates and a validated report; manual PIE remains separate. Use statuses `AUTOMATED_PASS`, `VISUAL_VERIFIED`, `MANUAL_PENDING`, `MANUAL_PASS`, `FAIL`, `BLOCKED`, `STALE`; bundle with `bun scripts/evidence.ts init --run <runId> --task <taskId>`, gate commands, and `verify <dir>`.

# Safety

Preserve unrelated dirty changes, never edit binary assets as text, and never claim an architecture seam is verified until its callers, tests, and gate evidence agree.
