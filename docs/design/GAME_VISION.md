# Game vision

## Elevator pitch

[PROPOSED] **Namarra Basin** is a social-first dinosaur survival game where friends share one dangerous ecosystem, grow a lineage through meaningful lives, and turn ordinary survival decisions into stories worth retelling. Dedicated servers are authoritative; clients send intent, while a unified online service later connects identity, friends, character records, and region-based server instances. It is not one physical server for every player.

The smallest promise is a reliable friend session: invite a friend, join the same region instance, find one another, and survive long enough for the world—not a quest marker—to create the next objective.

## Audience and session shape

| Audience | Session expectation |
| --- | --- |
| Friends who already use voice chat | [PROPOSED] 1–3 hour sessions with an easy join-a-friend path |
| Players who enjoy social survival, readable risk, and emergent stories | A life has a clear arc; a death changes the next plan rather than deleting the account |
| Solo players inside a social world | Solo nesting and viable escape routes; no requirement to join a large group |

## Four pillars and design tests

| Pillar | Design test: a feature passes this pillar if… |
| --- | --- |
| **Friends First** | …a friend can understand how to join, reach the party, and contribute without studying a wiki; the feature creates a shared decision rather than parallel chores. |
| **Every Life Matters** | …risk, growth, and death produce a memorable consequence, while recovery from an unfair loss is possible and account progress never makes a new life disposable. |
| **Living Ecosystem** | …player choices change where food, danger, and information are found; AI, carcasses, weather, and resource pressure form a readable chain rather than disconnected spawn tables. |
| **Emergent Stories** | …players can explain what happened using specific choices, relationships, places, and consequences—not just a score or checklist. |

## What we are not

- Not a theme-park quest MMO, lobby shooter, or solo power fantasy.
- Not a single global shard holding all players; the service coordinates many region instances.
- Not a promise of every dinosaur, biome, cosmetic, or feature at launch.
- Not a copied setting, species roster, map, codebase, story, or branded presentation from The Isle, Path of Titans, or Beasts of Bermuda.
- Not a system where account unlocks remove hunger, danger, growth, or the need to cooperate.

## Differentiation by pattern

These are pattern references, not implementation or content sources:

| Reference pattern | Our deliberate answer |
| --- | --- |
| The Isle presents a harsh, non-forgiving survival framing in its official guides. [Observed; source below] | Keep danger legible, but add friend recovery, anti-griefing, and death-as-story so a loss is meaningful rather than merely punitive. |
| Path of Titans’ official development posts highlight social play patterns such as friendly carrying and nest-related features. [Observed; source below] | Make joining, party formation, nesting, and parent protection first-class flows with explicit safeguards and measurable thresholds. |
| Beasts of Bermuda’s official Steam announcements discuss movement between food and water locations as an environmental pattern. [Observed; source below] | Build a readable ecosystem where scent, sound, carcasses, and seasonal routes create reasons to move together; do not copy names, maps, or rules. |

## MVP boundary

| In MVP | Explicitly non-MVP |
| --- | --- |
| Two-player network proof; then a small private dedicated playtest for [PROPOSED] 10 or fewer friends | EOS/live service, matchmaking, global database, mod support, monetization |
| Persistence of one character per account; one live character has one server lease | Large public population, many regions, transfer economy, broad roster |
| Existing health/death and stamina/sprint slice extended only as needed to prove the loop | Full AI ecosystem, nesting, lineage service, polished voice, account-wide progression |
| M0 single-player slice → M1 network proof → M2 dev dedicated server → M3 persistence/reconnect | M4 friends/party plus nesting, M5 ecosystem AI plus second region, M6 closed alpha on official service |

The canonical architecture and sequencing references are docs/design/MULTIPLAYER_ARCHITECTURE.md and docs/design/ROADMAP.md.

## Success metrics

All targets below are [PROPOSED] and should be measured in telemetry only after consent and privacy review.

| Metric | Target / definition |
| --- | --- |
| Friend co-presence | [PROPOSED] ≥60% of sessions include at least two friends in the same instance. |
| Time to join | [PROPOSED] Median invite-to-same-instance time under 2 minutes. |
| Life length | [PROPOSED] Median adult-capable life reaches at least 45 minutes, excluding disconnected bodies. |
| Storyful deaths | [PROPOSED] ≥50% of recorded deaths have a preceding story event: rescue, chase, nest, pack decision, environmental hazard, or contested carcass. |
| Fairness escape | [PROPOSED] ≥90% of new-player sessions avoid spawn camping or nest griefing incidents. |
| Return intent | [PROPOSED] ≥40% of first-session players return within 7 days, without treating retention as permission to increase punishment. |

## Cross-links

- Loop and roles: docs/design/CORE_GAMEPLAY_LOOP.md
- Setting and tone: docs/design/WORLD_AND_LORE.md
- Friends, nesting, and safety: docs/design/SOCIAL_MULTIPLAYER.md
- Future architecture: docs/design/MULTIPLAYER_ARCHITECTURE.md
- Future delivery sequence: docs/design/ROADMAP.md

## Sources

- The Isle official guides: <https://www.theisle-game.com/en/guides>
- Path of Titans official development blog: <https://pathoftitans.com/blog>
- Beasts of Bermuda official Steam announcements: <https://store.steampowered.com/news/?appgroupname=Beasts+of+Bermuda&appids=719890&feed=steam_community_announcements>
- Project architecture and current implementation boundary: docs/ARCHITECTURE.md; docs/tasks/001-sprint-stamina.md

## Open questions

- Which consent model and retention window are acceptable for session telemetry?
- What server population and region list can be supported after measurement rather than assumption?
- Which story events are important enough to memorialize without turning play into a checklist?
