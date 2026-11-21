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

## Milestone autonomy (owner directive 2026-11-15)

Line 8 and lines 10-13 apply only to the human-approval boundary below. Within an owner-assigned PSL milestone, do routine work without asking: inspect, plan, edit in isolated scratch/task worktrees, run trusted generators, `harness start`, `gate build|test`, reviewers, fix in-scope findings, harness CHECKPOINT commits on task branches.

- Recoverable failure: read logs, fix the root cause in scope, retry at most 2 more times, each attempt in a new evidence dir; never overwrite earlier evidence. Still failing → stop that task, report the blocker, continue independent work.
- Never ask the owner to pick between equivalent technical options or to confirm a routine retry; never weaken tests to get a PASS.
- Human approval still required (one exact operation per request): push/publish; merge or fast-forward into `main`; reset, destructive cleanup or irreversible deletion; credentials/Keychain; billing; changes to guards, approval enforcement, `harness/src/*`, `.omp/roles/*`, protected build/test scripts; untrusted external code; operations on unrelated worktrees/assets; material game-design, scope or architecture changes.
- Hard gates (harness TTY `resume --approve`, `bash.patterns`, role denies) are never bypassed; when one blocks, report the exact gate once.
