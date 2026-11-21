---
name: ue58-editor-mcp
description: Use for Unreal 5.8 MCP server setup, toolset allowlists, and MCP call discovery.
---

# Purpose / trigger

Use this for the official Unreal MCP server inside one UE 5.8 Editor. It is for connection, discovery, and safe editor-tool calls; use `ue58-pie-evidence` for PIE acceptance capture.

# UE 5.8 workflow

- Use only the installed `ModelContextProtocol`, `ToolsetRegistry`, `EditorToolset`, and `AutomationTestToolset` plugins.
- Launch one editor (verified 2026-11-12, UE 5.8.4), passing the toolset filter as `-ini:` overrides:
  `UnrealEditor PrehistoricSurvivalLab.uproject -EnablePlugins=ModelContextProtocol,ToolsetRegistry,EditorToolset,AutomationTestToolset -ModelContextProtocolStartServer -ModelContextProtocolPort=8000 "-ini:EditorPerProjectUserSettings:[/Script/ToolsetRegistry.ToolsetRegistrySettings]:+BlockedNames=Programmatic" "-ini:EditorPerProjectUserSettings:[/Script/ToolsetRegistry.ToolsetRegistrySettings]:+AllowedNames=EditorAppToolset"` (repeat `+AllowedNames=` for `LogsToolset`, `AutomationTestToolset`, `SceneTools`, `ActorTools`, `ObjectTools`).
- Do NOT rely on a `[/Script/ToolsetRegistry.ToolsetRegistrySettings]` section in `Saved/Config/MacEditor/EditorPerProjectUserSettings.ini`: the settings CDO read back empty and all 20 toolsets (incl. `ProgrammaticToolset`) were listed. The `-ini:` form populated the CDO and `list_toolsets` returned exactly the 6 allowed sets.
- Names are case-insensitive substrings matched against toolset and tool names (`NamePatternFilter.cpp`); block wins over allow.
- The endpoint is `http://127.0.0.1:8000/mcp`. It has no auth: keep loopback, never change `DefaultBindAddress`, and never port-forward. Verify with `lsof -nP -iTCP:8000 -sTCP:LISTEN` → `127.0.0.1:8000`; a non-loopback `Origin` header gets HTTP 403.
- Requires the Apple Metal Toolchain for the GUI Editor (see Failure).
- Keep one Editor and serialize mutating calls. Hold `.harness/ue-build.lock` for build/test operations; do not delete or bypass it.

# Discovery and commands

1. Run `/mcp enable unreal-editor`, then `/mcp test unreal-editor`.
2. Discover with MCP `tools/list` and call only `list_toolsets`.
3. Select a set, call `describe_toolset`, then call its named tool through `call_tool`.
4. Toolset ids are qualified: `EditorToolset.EditorAppToolset`, `EditorToolset.LogsToolset`, `AutomationTestToolset.AutomationTestToolset`, `editor_toolset.toolsets.{scene.SceneTools,actor.ActorTools,object.ObjectTools}`. Tool args follow `describe_toolset` `inputSchema`; struct params without defaults (e.g. `CaptureViewport.captureTransform`, `annotations`) must be passed explicitly. `CaptureViewport` returns PNG base64 in `returnValue.image.data`.
5. Filters are read at startup; changing them needs an Editor relaunch. `ModelContextProtocol.StopServer` / `StartServer [8000]` control the listener.

# Official API refs

Verified installed paths: `/Users/Shared/Epic Games/UE_5.8/Engine/Plugins/Experimental/ModelContextProtocol/Source/ModelContextProtocol/Public/ModelContextProtocolServer.h`, `/Users/Shared/Epic Games/UE_5.8/Engine/Plugins/Experimental/ModelContextProtocol/Source/ModelContextProtocolEngine/Public/ModelContextProtocolSettings.h`, `/Users/Shared/Epic Games/UE_5.8/Engine/Plugins/Experimental/Toolsets/EditorToolset/Source/EditorToolset/Private/EditorAppToolset.h`, and `/Users/Shared/Epic Games/UE_5.8/Engine/Plugins/Experimental/ToolsetRegistry/Source/ToolsetRegistry/Public/ToolsetRegistry/ToolsetRegistrySubsystem.h`.
Official guide: <https://dev.epicgames.com/documentation/unreal-engine/unreal-mcp-in-unreal-editor?application_version=5.8>

# Failure / verification

- Editor GUI hangs at startup with log ending at `MetalRHI` load: a modal "Xcode Metal Compiler error" (`MetalRHI.cpp:204`) is waiting. Check `xcrun -sdk macosx metal -v`; if it reports missing Metal Toolchain, run `xcodebuild -downloadComponent MetalToolchain` (host change: needs approval). Headless `-nullrhi` gates do not catch this.
- `not connected` or refused: confirm the single Editor owns port 8000, plugins are loaded, and endpoint is loopback; do not widen the bind.
- Missing tool: refresh, re-list, and verify allow/block filters; never enable `ProgrammaticToolset` to work around it.
- Record the exact tool name/arguments and returned result. Verification requires a successful `/mcp test`, a fresh editor log, and no overlapping mutating call.

# Evidence

Use statuses only: `AUTOMATED_PASS`, `VISUAL_VERIFIED`, `MANUAL_PENDING`, `MANUAL_PASS`, `FAIL`, `BLOCKED`, `STALE`. Start with `bun scripts/evidence.ts init --run <runId> --task <taskId>`, add gate evidence with `gate <dir> <preflight|build|test>`, then `verify <dir>` and `show <dir>`.

# Safety / permissions

MCP is experimental, unauthenticated, and same-machine only. Keep the official plugin, least-privilege allowlist, one Editor, and serialized writes. Do not expose the port or treat MCP output as build/test proof.
