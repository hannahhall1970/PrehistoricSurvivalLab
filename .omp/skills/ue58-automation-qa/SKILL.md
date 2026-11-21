---
name: ue58-automation-qa
description: Use for Unreal automation tests, Functional Testing, Automation Driver, reports, or screenshot baselines.
---

# Purpose / trigger

Use for deterministic UE 5.8 QA: C++ Automation Test Framework, Functional Testing, Automation Driver, and screenshot comparison. Use `ue58-pie-evidence` for human PIE acceptance.

# UE 5.8 workflow

- Put C++ tests under `Source/PrehistoricSurvivalLab`; names MUST start `PSL.`.
- Use `IMPLEMENT_SIMPLE_AUTOMATION_TEST` for a focused `RunTest`, or `BEGIN_DEFINE_SPEC`/`END_DEFINE_SPEC` for grouped specs. Use `ADD_LATENT_AUTOMATION_COMMAND` for latent work and `FAutomationTestBase::TestEqual` (with tolerance for floats) for assertions.
- Functional tests derive from `AFunctionalTest` in `/Users/Shared/Epic Games/UE_5.8/Engine/Source/Developer/FunctionalTesting/Classes/FunctionalTest.h`; finish explicitly with `FinishTest`.
- Automation Driver APIs are in `/Users/Shared/Epic Games/UE_5.8/Engine/Source/Developer/AutomationDriver/Public/IAutomationDriver.h`; use it for UI/input automation, not as a substitute for gameplay assertions.
- Screenshot calls use `/Users/Shared/Epic Games/UE_5.8/Engine/Source/Developer/FunctionalTesting/Public/AutomationBlueprintFunctionLibrary.h` (`TakeAutomationScreenshot`); compare only against reviewed baselines.

# Reports and baselines

Harness owner runs `bun harness/src/cli.ts gate test`; implementer agents do not run it directly, and raw `scripts/run_tests.sh` is not an alternative. Validate the exported report with `python3 scripts/verify_test_report.py <index.json> Source`; every declared `PSL.*` appears exactly once with `Success`, and counters match. A baseline is evidence, not an approval: never auto-approve or overwrite it after a mismatch.

# Official refs

`/Users/Shared/Epic Games/UE_5.8/Engine/Source/Runtime/Core/Public/Misc/AutomationTest.h` was verified for the macros, latent command macro, and `TestEqual`; the paths above were verified by glob. Official MCP guide: <https://dev.epicgames.com/documentation/unreal-engine/unreal-mcp-in-unreal-editor?application_version=5.8>

# Failure / verification

A zero exit code without a fresh validated report is failure. Distinguish `notRun`, `failed`, stale report, missing test, and screenshot mismatch. Verify test output, report timestamp, source test list, and baseline identity before marking a gate.

# Evidence / safety

Use only `AUTOMATED_PASS`, `VISUAL_VERIFIED`, `MANUAL_PENDING`, `MANUAL_PASS`, `FAIL`, `BLOCKED`, `STALE`. Bundle with `bun scripts/evidence.ts init --run <runId> --task <taskId>`, `gate <dir> test`, `add-screenshot <dir> <png> --source <text>`, and `verify <dir>`. Keep one Editor/test job, no fabricated screenshots, and no baseline changes without human review.
