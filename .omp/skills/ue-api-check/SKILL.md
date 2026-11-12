---
name: ue-api-check
description: Verify any Unreal Engine API signature against installed UE 5.8 headers before using it in C++.
---

# Validate UE API against installed headers

Canary: PSL-SKILL-UEAPI

Engine headers: `/Users/Shared/Epic Games/UE_5.8/Engine/Source` (5.8.4). Plugins: `.../Engine/Plugins`.

1. Before calling/overriding an engine API, `grep` its declaration under the engine Source path (e.g. pattern `\bvoid SetupPlayerInputComponent\(` in `Runtime/Engine/Classes`).
2. Confirm: exact signature, `const`/`virtual`/`override`, `UFUNCTION`/`UPROPERTY` specifiers, owning module (`Build.cs` dependency), and `UE_DEPRECATED` markers.
3. Add the module to `PrehistoricSurvivalLab.Build.cs` only if the header lives in a module not yet listed.
4. Cite the header path:line in your handoff. If not found, say so; do not guess from memory of older UE versions.
