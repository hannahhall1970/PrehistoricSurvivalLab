# Roadmap — Single-player slice to online dinosaur survival

Status: PLAN. M0 is implemented on branch `harness/2de78d4a-31f4-4f7b-8425-1bc0d87c0bb5` (HEAD `0926409`) but **not integrated into `main`** (`main` = `origin/main` = `db6d2c2`). M1+ are not implemented. `docs/ROADMAP.md` remains the single-player slice roadmap (P0–P3); this file supersedes its P3 "networking" bullet.

### M0 progress (verified 2026-11-12 from git + TestReports; see Task 001 A1–A8)

| Item | State | Evidence |
| --- | --- | --- |
| W1 input defaults + sprint speed | Committed `24e2f23` | harness run `f72902c1…`, reviewer approve |
| W2 HUD bars, W3 `L_TestArena` (LFS) | Committed `e5b600d`, `0926409` | harness run `2de78d4a…` stage DONE, W2/W3 PASSED |
| A1/A2/A3/A4/A5 | AUTOMATED-PASS | 7/7 `PSL.*` in `TestReports/20261112-182627.NV6wqV` (gameplay worktree) |
| A6 map opens | Map committed via LFS; open-without-warnings check not in a validated report | treat as MANUAL-PASS only with user log |
| A7 PIE play | User entered PIE and saw HUD (user report); full checklist (5 s drain, release lock, 8.3 s regen) not recorded | MANUAL-PARTIAL |
| Automated PIE (`PSL.PIE.*`, 8 tests) | **Uncommitted, unreviewed** work in the gameplay worktree; one local run 8/8 success-with-warnings (`20261112-203349.ocxhH3`, AudioMixer warning) | OUTSTANDING — owned by another agent; not part of `0926409` |
| A8 independent review | W1/W2/W3 approved per task; no review of the integrated M0 diff on `main` | OUTSTANDING |

M0 gate closes when `0926409` (plus any approved PIE-test commit) is integrated into `main` and gate build/test pass there.

Each milestone has one gate. A milestone is DONE only with harness `gate build|test` SUCCESS, the listed evidence, and an independent reviewer `approve`. No milestone starts while the previous one's gate is open, except design/research.

## Milestones

| ID | Name | Scope (MVP) | Gate (measurable) | Explicitly NOT in scope |
| --- | --- | --- | --- | --- |
| M0 | Single-player slice playable | Task 001: input defaults, HUD bars, `L_TestArena` | Task 001 A1–A8 | Networking |
| M1 | Two-player network proof | Task 002: replicated pawn, movement, Health/Stamina, server authority | `docs/tasks/002-two-player-network-proof.md` N1–N12 | Dedicated server binary, EOS, persistence |
| M2 | Dev dedicated server + customer join demo | **M2a (first deliverable): real-network join demo** — uncooked `-server` on the dev Mac, a second machine on LAN (or same Mac second process) joins by `IP:7777`, both dinosaurs visible, sprint/stamina/HUD working; screen recording + server log with 2× `Join succeeded`. M2b: 2–4 `-game` clients, headless log evidence, 150 ms / 2 % loss emulation pass | M2a: 10-min recorded session, 0 crashes, no persistent corrections. M2b: 30-min soak, 4 clients, 0 crashes, 0 ensure in our module, server tick ≥ 30 Hz | Cloud hosting, Linux cross-compile, internet NAT traversal, accounts |
| M3 | Character persistence + reconnect | Server-side character record (species, growth, Health, Stamina, hunger/thirst, location) behind a `ICharacterStore` with a local file/SQLite backend; reconnect window | Kill client → reconnect ≤ 120 s restores same pawn state (tolerance: position ≤ 1 m, stats exact); record survives server restart | Global DB, cross-region transfer |
| M4 | Friends/party + nesting prototype | Party in-instance (join by code on dev server), spawn-near-party, 1 nest type, baby hatch for party member | 2 friends: invite → same instance → spawn ≤ 150 m from host in ≤ 2 min (wall clock); nest hatch end to end | EOS friends, voice |
| M5 | Ecosystem AI + 2nd region | Hunger/thirst, carcass/scavenging, 2 AI species (StateTree), second dev server instance with same character store | AI food chain runs 2 h unattended with population bounds; character moves A→B with cooldown enforced | Large roster, seasons |
| M6 | Closed alpha on official service | EOS Connect login, server browser/sessions, region instances, sanctions/report flow, telemetry | ≤ 100 testers, crash-free sessions ≥ 98 %, join-friend ≤ 2 min p90 | Monetization, mods, consoles |

## Dependencies

```text
M0 ──► M1 ──► M2 ──► M3 ──► M4 ──► M6
                     └────► M5 ─────┘
```

- M2 needs a dedicated-server *build* only when hosting off-Mac. Uncooked editor `-server` is enough for M2 on the dev Mac (see MULTIPLAYER_ARCHITECTURE.md §Hosting).
- A packaged `Server` target requires a source-built engine (see MULTIPLAYER_ARCHITECTURE.md Sources). That cost is first paid at M6, not earlier.
- EOS integration starts in M6. Before that, identity = dev-only player name; never ship that path.

## Design-doc ownership

| Area | Doc |
| --- | --- |
| Vision, pillars, metrics | `GAME_VISION.md` |
| Loops, roles, ecosystem, growth/death | `CORE_GAMEPLAY_LOOP.md` |
| World, biomes, lore | `WORLD_AND_LORE.md` |
| Friends, parties, nesting, anti-griefing | `SOCIAL_MULTIPLAYER.md` |
| Authority, replication, hosting, persistence, EOS | `MULTIPLAYER_ARCHITECTURE.md` |
| First implementable task | `../tasks/002-two-player-network-proof.md` |

## Long-term content (non-MVP, unordered)

- Species roster growth: +1 archetype per content drop only after the previous one has balance telemetry.
- Seasons/migrations, weather, disease, territory marking.
- Lineage/legacy system and memorial landmarks.
- Proximity voice, in-game report with replay snippet.
- Mod support and community servers (needs server binary distribution and a content-validation story).
- Consoles: out of scope until PC alpha retention is measured.

## Technical risks (roadmap-level)

| Risk | Impact | Mitigation | Milestone |
| --- | --- | --- | --- |
| Memory-constrained dev Mac cannot run editor server + 2 clients | M1/M2 evidence blocked | CQTest PIE for automated M1 proof (N3–N8); multi-process manual runs (N10/N11) are memory-gated with recorded RSS; `-nullrhi` server | M1 |
| Sprint speed set server-only causes movement corrections | Rubber-banding | Sprint + stamina simulated per move in CharacterMovement (Task 002 C3; N8–N10) | M1 |
| Launcher engine cannot build `Server` target | No packaged server | Uncooked `-server` until M6; source engine decision gate at M5 exit | M5 |
| Persistence model chosen too late | Rewrites of Health/Stamina/growth | M3 defines record schema before hunger/thirst land (M5) | M3 |
| Griefing patterns (combat logging, server hopping) baked in | Pillar "Every Life Matters" fails | Disconnect body linger + server lease in M3, not M6 | M3 |
| Scope creep toward MMO backend | Nothing ships | This roadmap; any backend work before M3 gate is rejected in review | all |
