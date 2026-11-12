# Coding Harness

A resumable state machine that turns one objective into verified, reviewed tasks. Agents run as Paseo agents. Builds and tests run as deterministic gates. All state lives in SQLite, so any stage can be killed and resumed.

## Flow

```
INTAKE → PREFLIGHT → PLAN → TASK_CONTRACT → EXECUTE → BUILD → TEST → INDEPENDENT_REVIEW → CHECKPOINT → NEXT_TASK ─┬→ TASK_CONTRACT
                                               ▲          │       │            │                                  └→ DONE
                                               └── RETRY ─┴───────┴────────────┘  (attempt < maxAttempts, failure fed into next context)
PREFLIGHT/BUILD TOOLCHAIN_MISSING|DISK_LOW → BLOCKED         permission request → HUMAN_APPROVAL         retries exhausted → FAILED
stop request (between steps or aborting in-flight agent/gate) → STOPPED
```

## Approval and worktree safety

The harness parks gated operations in `HUMAN_APPROVAL`. Only `resume <runId> --approve` run from a human's terminal can release them: it prints the run, operation, and target, plus a one-time code that must be typed back on the controlling terminal (`/dev/tty`). The resulting grant is held in memory only, bound to the run id, run version, operation, and target, and is consumed on first use. Paseo/OMP agent shells have no controlling terminal, so they cannot mint a grant and the run stays parked. **Limit:** an agent that has bash can still allocate a pty (`script`) or import `TtyApprovalSource` with its own `ask()`. The challenge stops accidental and prompt-injected self-approval, not a deliberately malicious same-uid worker. The implementer overlay denies `script`, `harness/src/cli.ts`, `.harness/*` and `sqlite3` as defense in depth. Hard isolation needs a separate uid. `--approve-op`/`--approve-target` are rejected, and forged grant objects, JSON, free text, and replays are refused (`harness/test/approval.test.ts`). With `useWorktrees: true`, each run uses `.harness/worktrees/<runId>` on `harness/<runId>` from `origin/main`; implementers and task gates use that directory, and clean worktrees are removed without force while dirty or committed work is retained.

- **Truth source.** A task is `PASSED` only when the BUILD and TEST gates recorded in the DB return SUCCESS (when a build is required) and the independent reviewer returns `approve` with no blocker findings. A worker's own claims are ignored. The planner's `needsBuild` can only add verification. `requiresBuild()` in `graph.ts` forces BUILD+TEST whenever the actual changed files (`git status --porcelain`, untracked included) or the declared task files touch `Source/`, `Plugins/`, `Config/`, `*.uproject`/`*.uplugin`, `*.Build.cs`/`*.Target.cs`, C/C++/C# sources, or the build/test scripts. If git fails, it fails closed. The check runs again at review approval.
- **Bounded.** Each task has `maxAttempts` (default 3) and a per-attempt `budgetMs`. A run is capped at `limits.maxTasksPerRun`. Gates have their own timeouts and kill the whole process group. Nothing loops without a bound, and there are no schedules or heartbeats.
- **Crash-safe.** Every step checkpoints to SQLite and appends an event. A lease stops two runners from driving the same run. Agent calls use `idempotencyKey = runId:taskId:role:attempt`, so a resume reuses the existing Paseo agent instead of spawning a duplicate.
- **Single writer.** UE builds and tests take `.harness/ue-build.lock`, an atomic lock file recording the owner PID, its start time (`ps -o lstart`), and the build child's process group. The lock is held while the owner is the same live, non-zombie process, or while any process in the build group is alive. That covers a harness crash during a UE build. A zombie, a reused PID, or a dead group lets the lock be reclaimed. A second build gets `BUILD_LOCKED`.
- **Parked states.** `BLOCKED` and `HUMAN_APPROVAL` wait for a person. Run `resume <runId>` after fixing the toolchain, or `resume <runId> --approve` after granting a permission.

## Modules (`harness/src/`)

| File | Role |
| --- | --- |
| `types.ts` | Shared contract: stages, outcomes, failure classes, task/run state, agent port |
| `store.ts` | `bun:sqlite` (WAL): runs, tasks, events, leases; optimistic `version` check |
| `graph.ts` | Pure transition table plus the `Runner` that drives it |
| `gates.ts` / `classify.ts` | Spawn preflight/build/test with timeouts, write full logs to disk, extract ≤60-line excerpts, classify failures |
| `lock.ts` | Single-writer UE build lock |
| `context.ts` | Assembles per-role context: AGENTS.md, task contract, `git status`, the task's files and header signatures, the last failure excerpt (never full logs), and handoff notes. Task files come only from `git ls-files --cached --others --exclude-standard`, so ignored files never appear. Symlinks, paths that escape the repo, and sensitive paths (`.env*`, keys, credentials, `Saved/`, `Intermediate/`, …) are skipped. Every section passes through `redactSecrets()` before it is truncated, which covers `SecurityToken=`, key/token assignments, private keys, and provider tokens |
| `paseo.ts` / `config.ts` | `AgentPort` over `@getpaseo/client`; role → model routing from `harness/harness.config.json` |
| `cli.ts` | `start`, `status`, `resume`, `stop`, `logs`, `gate` |

Runtime files go in `.harness/` (gitignored): `state.db`, gate logs, `runs/<runId>/<taskId>/handoff-*.md`, and `ue-build.lock`.

## Commands

```bash
bun harness/src/cli.ts start "Add stamina regen delay"   # exit 0 DONE, 1 FAILED, 2 BLOCKED/HUMAN_APPROVAL, 3 STOPPED
bun harness/src/cli.ts status [runId]
bun harness/src/cli.ts resume <runId> [--approve]
bun harness/src/cli.ts stop <runId>
bun harness/src/cli.ts logs <runId> [--task <id>]
bun harness/src/cli.ts gate preflight|build|test        # deterministic, takes the build lock
cd harness && bun test                                  # harness tests, independent of Unreal
cd harness && HARNESS_LIVE=1 bun test test/paseo.live.test.ts   # real Paseo daemon (Bun)
# demo without LLM/UE: add --fake (scripted worker + fake gates); HARNESS_FAKE_DELAY_MS=8000 makes kill -9 mid-EXECUTE easy
```

Paseo workspace scripts in `paseo.json` (`paseo script start <name>`) wrap the argument-free commands.

## Roles and models

- **Planner:** a strong reasoning model. Its output must be `PlanOutput` JSON, which is validated for unique ids, existing dependencies, no cycles, and non-empty acceptance.
- **Implementer:** a strong coding model in mode `full` so it can run unattended. It edits code only; the harness runs the UE build.
- **Reviewer:** a model from a different family than the implementer, also in mode `full`. It receives the task contract, `git diff HEAD`, the task files, and this attempt's gate evidence inline, and returns `ReviewVerdict` JSON. The verdict counts only alongside the deterministic gates.
- **Role tool policy:** each role's `configOverlay` is an OMP settings file passed as `PI_CONFIG_FILES`. A missing overlay fails closed.
  - `.omp/roles/readonly.yml` (planner and reviewer) sets `approvalMode: always-ask`. Only `read`/`grep`/`glob`/`todo` are allowed, so a headless agent's other calls are refused. A live probe showed all 11 write paths denied: write, edit, bash, eval, ast_edit, lsp, terminal, workspace script, ssh, chrome-devtools, and task.
  - `.omp/roles/implementer.yml` denies git commit/push/merge, UE builds, DerivedDataCache, `task`, and the terminal/agent host tools. Before this, `create_terminal` + `send_terminal_keys` bypassed the bash deny.
  - These overlays are approval policy, not an OS sandbox: the agent runs as the same uid.
- The exact model ids are in `harness/harness.config.json`. Only the `omp` provider is available on this machine.

## Why not LangGraph

LangGraph's checkpointers (`SqliteSaver`, `PostgresSaver`) and durable execution solve the same persistence problem. It was not used here:

- It would add a Python runtime and framework next to a Bun toolchain that already ships `bun:sqlite`.
- The graph is about 15 stages with a fixed edge table. A pure `transition()` function is easy to test exhaustively.
- Agents are already durable Paseo processes, so the harness only needs to persist its own pointer (stage, attempt, agent id).

Revisit LangGraph if you need dynamic graphs, many concurrent branches, or time-travel debugging.

## Design sources

- OpenAI, *Harness engineering: leveraging Codex in an agent-first world*. Use AGENTS.md as a map rather than a manual, enforce constraints mechanically, and treat the environment and feedback loops as the product. <https://openai.com/index/harness-engineering/>
- Anthropic, *Effective harnesses for long-running agents* and *Harness design for long-running apps*. Work one feature per session, keep progress artifacts on disk, default to FAIL until evidence proves otherwise, and evaluate with a fresh-context reviewer. <https://www.anthropic.com/research/effective-harnesses-for-long-running-agents> and <https://www.anthropic.com/research/harness-design-long-running-apps>
- Anthropic, *Effective context engineering for AI agents*. Give the model a minimal high-signal context, retrieve just in time, and keep structured notes and handoffs. <https://www.anthropic.com/research/effective-context-engineering-for-ai-agents>
- Geoffrey Huntley, *Ralph*. Do one task per loop with a fresh context, keep state on disk, and let tests apply backpressure. This harness keeps those ideas and adds what the plain bash loop lacks: an iteration cap, deterministic gates, and an independent reviewer. <https://ghuntley.com/ralph/>
- LangGraph persistence and durable execution, used as the reference for checkpoint and resume semantics. <https://docs.langchain.com/oss/python/langgraph/persistence>

## Request routing (OMP context)

- Root `AGENTS.md` is the short repository map. Task subagents do not inherit files named `AGENTS.md`.
- `.omp/RULES.md` is an always-apply sticky rule that task subagents also receive. It carries only the hard rules: route through the harness, one writer/one build, no self-verification, read-only reviewers, no fabrication, no commit/push.
- Discovery is verified, not assumed: `omp -p --no-session "...reply with ONLY the rules-canary token..."` returned `PSL-RULES-7Q`.
- No Paseo prompt-rewriting hooks (such as `agent.turn.before`) are used. Routing relies only on project instructions, the CLI, and `paseo.json` scripts.

## Limits

- `useWorktrees` is `false` until the repo has a first commit, because worktrees need a Git HEAD and committing requires the user's approval. Until then, workers edit the main checkout one task at a time.
- Reviewer/implementer limits are OMP approval policy, not an OS sandbox. A tool added to OMP/Paseo later is refused for reviewers by `always-ask`, but the implementer's deny list must be extended by hand.
- Low disk is `DISK_LOW` (BLOCKED, exit 2), not a toolchain error. The 20 GB preflight floor stays, and caches are never cleaned automatically.
- `scripts/check_secrets.sh` (repo-local `.git/hooks/pre-commit`) refuses staged secrets, including Unreal's generated `AndroidFileServerRuntimeSettings` `SecurityToken`. `--worktree` scans the unstaged diff.
- **R1/R2 are open, not fixed.** Prompt-level and tool-level denials (OMP `always-ask`, `bash.patterns`, role overlays) are approval policy, not OS isolation. A same-UID worker can spawn a pty, import harness code, or reach the state DB, so it must not be treated as safe against hostile code. Privileged git and system operations (commit, push, merge, delete, cache or worktree cleanup, global config) need a trusted human approval boundary outside the worker. A separate-UID worker is the only real fix and is not implemented.
- **Storage.** `du` overstates `~/.omp/wt`: OMP clone-first (APFS clonefile) worktrees share blocks. Use `getattrlist ATTR_CMNEXT_PRIVATESIZE` (private bytes) for a lower bound on reclaim; the upper bound is not measurable without deleting. Cleanup is proposal-only: `omp worktree clear --dry-run`, then a path-specific approved manifest. Dead owner PID does not mean empty worktree; clone-first copies inherit the parent's dirty state, so "dirty" alone is not proof of unique work (compare diff hashes). Never run `omp worktree clear` without an approved manifest.
- **AndroidFileServer.** Mac-only prototype: keep `AndroidFileServer` `Enabled:false` in the `.uproject` so the editor plugin (which writes `SecurityToken` to `DefaultEngine.ini`) is not loaded. Keep `check_secrets.sh` enabled regardless.
- `scripts/run_tests.sh` passes only when `scripts/verify_test_report.py` accepts the exported `index.json`. Every `PSL.*` test declared in `Source/` must be present exactly once with state `Success`, `failed`/`notRun` must be 0, and the header counters must match the entries. An editor exit code of 0 alone never passes.
