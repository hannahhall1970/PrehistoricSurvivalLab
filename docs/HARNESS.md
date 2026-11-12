# Coding Harness

A resumable state machine that turns one objective into verified, reviewed tasks. Agents run as Paseo agents. Builds and tests run as deterministic gates. All state lives in SQLite, so any stage can be killed and resumed.

## Flow

```
INTAKE → PREFLIGHT → PLAN → TASK_CONTRACT → EXECUTE → BUILD → TEST → INDEPENDENT_REVIEW → CHECKPOINT → NEXT_TASK ─┬→ TASK_CONTRACT
                                               ▲          │       │            │                                  └→ DONE
                                               └── RETRY ─┴───────┴────────────┘  (attempt < maxAttempts, failure fed into next context)
PREFLIGHT/BUILD TOOLCHAIN_MISSING → BLOCKED         permission request → HUMAN_APPROVAL         retries exhausted → FAILED
stop request (between steps or aborting in-flight agent/gate) → STOPPED
```

- **Truth source.** A task is `PASSED` only when the BUILD and TEST gates recorded in the DB return SUCCESS (if `needsBuild`) and the independent reviewer returns `approve` with no blocker findings. A worker's own claims are ignored.
- **Bounded.** Each task has `maxAttempts` (default 3) and a per-attempt `budgetMs`. A run is capped at `limits.maxTasksPerRun`. Gates have their own timeouts and kill the whole process group. Nothing loops without a bound, and there are no schedules or heartbeats.
- **Crash-safe.** Every step checkpoints to SQLite and appends an event. A lease stops two runners from driving the same run. Agent calls use `idempotencyKey = runId:taskId:role:attempt`, so a resume reuses the existing Paseo agent instead of spawning a duplicate.
- **Single writer.** UE builds and tests take `.harness/ue-build.lock`, an atomic `O_EXCL` lock file that holds the owner's PID. A lock whose PID is dead is reclaimed. A second build gets `BUILD_LOCKED`.
- **Parked states.** `BLOCKED` and `HUMAN_APPROVAL` wait for a person. Run `resume <runId>` after fixing the toolchain, or `resume <runId> --approve` after granting a permission.

## Modules (`harness/src/`)

| File | Role |
| --- | --- |
| `types.ts` | Shared contract: stages, outcomes, failure classes, task/run state, agent port |
| `store.ts` | `bun:sqlite` (WAL): runs, tasks, events, leases; optimistic `version` check |
| `graph.ts` | Pure transition table plus the `Runner` that drives it |
| `gates.ts` / `classify.ts` | Spawn preflight/build/test with timeouts, write full logs to disk, extract ≤60-line excerpts, classify failures |
| `lock.ts` | Single-writer UE build lock |
| `context.ts` | Assembles per-role context: AGENTS.md, task contract, `git status`, the task's files and header signatures, the last failure excerpt (never full logs), and handoff notes |
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
- **Reviewer:** a model from a different family than the implementer, also in mode `full`. It is read-only by instruction and returns `ReviewVerdict` JSON. Trade-off: the `ask` mode would block on every tool call. Read-only is not enforced by the sandbox, but the verdict counts only alongside the deterministic gates.
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
- Read-only reviewers are enforced by prompt only, not by the sandbox.
