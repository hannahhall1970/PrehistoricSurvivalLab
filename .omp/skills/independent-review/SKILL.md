---
name: independent-review
description: Use for read-only evidence review, fingerprints, verify.json, verdict JSON, or acceptance rejection.
---

# Purpose / trigger

Use as the fresh-context, read-only reviewer after implementation. Review the task contract, diff, source fingerprint, `verify.json`, and complete evidence bundle; do not edit or execute.

# Review inputs and method

- Read the contract, `git diff` artifact, declared `reviewedFingerprint`, gate results, `verify.json`, manifest, screenshots/review JSON, and manual notes.
- The reviewer role is limited to `read`, `grep`, and `glob` by `.omp/roles/readonly.yml`. Reproduce `bun scripts/evidence.ts verify` reasoning by reading artifact fields and comparing fingerprints; never run the command or claim OS isolation.
- Fingerprint equality must cover the contract’s sorted tracked + untracked-not-ignored files under `Source/`, `Config/`, `Content/`, `Plugins/`, `*.uproject`, and `scripts/`; excluded runtime/cache paths do not count.
- Treat `STALE` as invalid evidence. A screenshot can support visual state only; it cannot prove input, damage, or stamina mechanics.

# Reject when

Reject for missing required tests, stale/unvalidated reports, fingerprint mismatch, unexplained build failure, unverifiable evidence, missing manual acceptance (especially A7), missing reviewer disposition, or a verdict that treats compilation/screenshot as gameplay proof. Also reject any claimed automated pass without fresh gate/report artifacts.

# Output

Return only JSON shaped as:
`{"verdict":"approve|reject","reviewedFingerprint":"<value>","findings":[{"id":"F1","severity":"blocker|major|minor|info","evidence":"<artifact path/field>","disposition":"<action or accepted rationale>"}]}`.
Use `approve` only when all required criteria are evidenced and every finding has a disposition; otherwise `reject` with concrete artifact evidence.

# Official refs

Verified installed `/Users/Shared/Epic Games/UE_5.8/Engine/Source/Runtime/Core/Public/Misc/AutomationTest.h` and `/Users/Shared/Epic Games/UE_5.8/Engine/Plugins/Experimental/ModelContextProtocol/Source/ModelContextProtocolEngine/Public/ModelContextProtocolSettings.h`. Official guide: <https://dev.epicgames.com/documentation/unreal-engine/unreal-mcp-in-unreal-editor?application_version=5.8>

## Evidence / safety

Statuses are `AUTOMATED_PASS`, `VISUAL_VERIFIED`, `MANUAL_PENDING`, `MANUAL_PASS`, `FAIL`, `BLOCKED`, `STALE`. Read bundles created by `bun scripts/evidence.ts init --run <runId> --task <taskId>`, `gate <dir> <preflight|build|test>`, `add-review <dir> <review.json>`, `manual <dir> <criterion> <MANUAL_PASS|FAIL> --by <name> --note <text>`, and `verify <dir>`; do not alter them. Reviewer evidence is independent only when it is traceable to files and does not claim OS-level sandboxing.
