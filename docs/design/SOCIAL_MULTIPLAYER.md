# Social multiplayer

## Authority and Friends First flow

Servers are authoritative dedicated instances; clients send intent only. A live character holds one server lease at a time. The unified online service is a shared identity, friends, party, character-record, and server-browser layer across many region instances—not one physical server for all players.

**Join-a-friend flow:** invite → authenticate → choose a compatible region → reserve or join the friend’s instance → spawn at a protected route near the party → confirm party state and server lease. The join UI must show region, population, age of the instance, and any cooldown before it accepts the move.

Safeguards: never spawn inside combat, a nest, a carcass contest, or an obstructed volume; [PROPOSED] use a [PROPOSED] 30-second protected arrival window that ends on attack or intentional damage; [PROPOSED] reject a join when the friend’s instance is at its measured cap; offer a safe fallback location in the same region.

## Party and pack rules

| Rule | Proposed policy | Milestone |
| --- | --- | --- |
| Party | [PROPOSED] A party has up to 8 accounts; it is social metadata, not shared damage immunity. | M4 |
| Species pack | Respect the role caps in docs/design/CORE_GAMEPLAY_LOOP.md; server validates membership and rejects excess combat bodies. | M4 |
| Mixed groups | Allowed: mixed groups share information and protection, but do not receive a universal pack bonus. | M4 |
| Combat contribution | [PROPOSED] A group-size advantage increases counter-pressure and attention, not raw damage. | M5 |
| Server lease | One live character is pinned to one instance; [PROPOSED] switching has a 10-minute cooldown outside a verified disconnect recovery. | M3 |

## Communication

- MVP: party text and contextual pings for danger, food, water, regroup, and nest.
- [PROPOSED] Calls are short-range signals with species-appropriate risk: nearby players and AI can hear them.
- Proximity voice is later, opt-in, push-to-talk by default, with mute, block, and report controls; it is not required for the MVP promise.
- Pings expire and cannot reveal an exact enemy location through terrain. Server validates sender, range, and cooldown.

## Nesting and friend continuity

Adults may create a nest alone or as a pair. Friends can hatch as babies if they accept the invite and the nest has capacity. A parent can guard, feed, distract, or abandon the nest; a nest is not an invulnerable team spawn.

[PROPOSED] A nest has a [PROPOSED] 5-minute preparation timer and [PROPOSED] 10-minute hatch window, tuned by species. The server records parent, invited accounts, and location. Hatchlings receive a protected exit route, not immunity across the whole biome.

Abuse limits: [PROPOSED] one account can hatch from the same nest once per [PROPOSED] 30 minutes; abandoned nests expire after [PROPOSED] 15 minutes; repeated hostile hatch-killing near one nest triggers server-side protection review. Parents cannot force a friend into a hatch, delete a friend’s character, or transfer account power through nesting.

## Every Life Matters social rules

A life earns meaning through choices and relationships, not a permanent combat ladder. Memorials can record the place, cause, and consenting allies of a death. Lineage history can show who guarded, rescued, nested, or shared a route. Account unlocks are presentation, knowledge prompts, or social affordances; they never grant extra health, damage, stamina, growth speed, or immunity.

## Anti-griefing and balance thresholds

| Rule | [PROPOSED] threshold / behavior | Milestone |
| --- | --- | --- |
| Combat logging | Body remains vulnerable and server-owned for [PROPOSED] 20 seconds after disconnect; reconnect returns to the same lease when possible. | M3 |
| Server hopping | [PROPOSED] 10-minute cooldown; combat or recent damage extends it to [PROPOSED] 20 minutes. | M3 |
| Spawn camping | Protected arrival route plus [PROPOSED] 200 m no-ambush buffer where terrain permits; repeated kills within [PROPOSED] 5 minutes are review signals, not automatic bans. | M2 |
| Nest griefing | Nest can be hidden from non-party UI; [PROPOSED] hostile deaths within [PROPOSED] 50 m during the hatch window trigger temporary relocation/protection. | M4 |
| KOS by superior groups | Group caps are enforced; [PROPOSED] groups at a [PROPOSED] 2:1 body-count advantage receive no kill progression unless the smaller group initiated damage within [PROPOSED] 60 seconds; repeated disproportionate kills create review evidence. | M4–M5 (needs combat + party) |
| Cannibalism | Disabled by default for MVP; if later enabled, species rules and consent-readable warnings precede it. | M5+ |
| Friendly fire | [PROPOSED] No direct friendly fire for party members; accidental collision and environmental danger remain possible and legible. | M4 |
| Kill stealing | Server credits participation by damage, pursuit, rescue, and carcass control; [PROPOSED] a [PROPOSED] 30-second contribution window prevents last-hit-only credit. | M5 (needs combat + carcasses) |
| Reports and moderation | Block, mute, report, evidence ID, and appeal status are required before public alpha; [PROPOSED] ≥95% of reports receive acknowledgement within [PROPOSED] 24 hours; automated action needs human-review escape. | M4–M6 |
| Every Life Matters record | [PROPOSED] 100% of deaths retain a server-side cause code; player-facing memorial details require consent and may be hidden. | M4 |

Thresholds are tuning instruments, not promises until measured in private playtests. Servers must explain a block, cooldown, death, and protection event in plain language.

## Milestone boundary

- **M0:** local social language only; no network claim.
- **M1:** two separate clients in one server-authoritative world: spawn/possession, replicated movement, Health/Stamina. No party, invite, or friendly-fire rules yet (see docs/tasks/002-two-player-network-proof.md).
- **M2:** dev dedicated server, spawn protection, and basic evidence IDs.
- **M3:** character persistence, reconnect, body-on-disconnect (20 s linger), server lease, and hopping cooldown.
- **M4:** friends/party flows, nesting prototype, memorial/lineage record, reports and moderation prototype.
- **M5:** ecosystem AI, second region, and measured group/cannibalism rules if justified.
- **M6:** closed alpha on official service with privacy, moderation, and operational review.

## Cross-links

- Vision and success metrics: docs/design/GAME_VISION.md
- Roles, growth, and death: docs/design/CORE_GAMEPLAY_LOOP.md
- Setting and landmarks: docs/design/WORLD_AND_LORE.md
- Future architecture: docs/design/MULTIPLAYER_ARCHITECTURE.md
- Future delivery sequence: docs/design/ROADMAP.md

## Sources

- Current authority/replication boundary: docs/ARCHITECTURE.md
- MVP and milestone boundary: docs/tasks/001-sprint-stamina.md
- Unreal Gameplay Framework reference: <https://dev.epicgames.com/documentation/en-us/unreal-engine/gameplay-framework-in-unreal-engine?application_version=5.8>
- Pattern references (not copied rules): <https://www.theisle-game.com/en/guides> ; <https://pathoftitans.com/blog> ; <https://store.steampowered.com/news/?appgroupname=Beasts+of+Bermuda&appids=719890&feed=steam_community_announcements>

## Open questions

- What region selection and server lease model best handles a friend in a full instance without encouraging hopping?
- Which evidence is sufficient for moderation while minimizing retained voice, location, and identity data?
- Should party members share only pings, or also a limited scent/sound interpretation channel?
