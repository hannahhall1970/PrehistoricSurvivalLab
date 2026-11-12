# PrehistoricSurvivalLab hard rules (rules-canary: PSL-RULES-7Q)

- Code-changing or build-needing requests go through the harness: `bun harness/src/cli.ts start "<objective>"`. Do not hand-roll builds.
- One repository writer and one Unreal build at a time. Builds/tests run only via `bun harness/src/cli.ts gate build|test` (holds `.harness/ue-build.lock`).
- A worker never marks its own task verified. Only harness gate results plus an independent reviewer `approve` count.
- Reviewers and research subagents are read-only: never edit, write, or delete implementation files.
- Never fabricate build/test results; missing UE/Xcode is BLOCKED with the exact error.
- Never commit, push, delete DerivedDataCache/engine/user data, or change global config without explicit user approval.

- Approvals MUST name one exact operation and be explicit structured grants.
- NEVER preselect an approve option; free text and commit-message fields never count.
- Generic approval flags do not authorize a different operation.
- If approval is ambiguous, do not run the operation.
