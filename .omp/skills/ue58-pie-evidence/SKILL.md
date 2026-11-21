---
name: ue58-pie-evidence
description: Use for PIE gameplay checks, viewport captures, manual A7 acceptance, or evidence bundles.
---

# Purpose / trigger

Use for human-in-the-Editor Play-In-Editor (PIE) verification and screenshot-backed evidence. Screenshots show a view, not input delivery, damage, or stamina correctness.

# UE 5.8 workflow

- Start from the open test map with one Editor; use MCP `EditorAppToolset.StartPIE`, then poll `IsPIERunning`.
- Exercise the acceptance path manually: WASD moves, mouse rotates, hold Shift sprints, stamina drains to zero in about 5 s, release Shift to unlock sprint, then observe regeneration in about 8.3 s and bars.
- Capture only after the state is visible using `EditorAppToolset.CaptureViewport` (or `CaptureEditorImage` for editor context); call `StopPIE` in cleanup and confirm `IsPIERunning` is false.
- Record the screenshot source verbatim: tool name, run/map, visible state, and capture time. Never use an image as proof that a key reached C++ or that damage/stamina values are numerically correct.

# A7 manual record

Start as `MANUAL_PENDING` with this checklist:
`A7: [ ] move [ ] look [ ] Shift sprint [ ] drain≈5s [ ] release unlocks [ ] regen≈8.3s [ ] bars follow values`.
A human may change it to `MANUAL_PASS` only with a note naming the operator, map, run, and observed timings; use `FAIL` for a failed observation and `BLOCKED` for missing Editor/map/input. `STALE` invalidates evidence after a relevant source/map/settings change.

# Official API refs

Verified installed `/Users/Shared/Epic Games/UE_5.8/Engine/Plugins/Experimental/Toolsets/EditorToolset/Source/EditorToolset/Private/EditorAppToolset.h` declares `StartPIE`, `StopPIE`, `IsPIERunning`, `CaptureViewport`, and `CaptureEditorImage`. Official MCP guide: <https://dev.epicgames.com/documentation/unreal-engine/unreal-mcp-in-unreal-editor?application_version=5.8>

# Evidence / verification

`bun scripts/evidence.ts init --run <runId> --task <taskId>`; capture with `add-screenshot <dir> <png> --source <text>`; record the checklist with `manual <dir> A7 MANUAL_PASS --by <name> --note <text>` (or `FAIL`); finish with `verify <dir>` and `show <dir>`. Automated gates cannot close A7.

# Safety

Keep MCP loopback-only, one Editor, serialized mutating calls, and the build lock for gates. Do not fabricate or retouch a screenshot; preserve the original PNG and source metadata.
