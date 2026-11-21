# Core gameplay loop

## Loop summary

[PROPOSED] Namarra Basin asks a player to read the world, choose a risk, and bring a living body back to friends. Every mechanic is a proposed design unless the mapping table says it exists in the current slice.

### Moment-to-moment loop

1. **Move:** travel, stalk, flee, climb around hazards, or hold a defensible route.
2. **Sense:** read sightlines, scent, sound, tracks, calls, weather, and carcass activity.
3. **Eat / drink:** spend time at a resource while exposing the body to interruption.
4. **Rest:** recover in cover; trade mobility for stamina and safety.
5. **Fight / flee:** commit only when health, stamina, terrain, and allies support the choice.
6. **Regroup:** share information, relocate the party, tend a nest, or leave a memorable trace.

## Three nested loops

| Loop | Shape | Outcome |
| --- | --- | --- |
| Session | Join friend → establish a safe route → hunt/forage → respond to a threat → recover or mourn → choose the next objective | A shared story in [PROPOSED] 1–3 hours |
| Life | Hatch → juvenile → sub-adult → adult → elder → death | A readable arc with changing risk and social value |
| Meta | Remember a lineage → earn non-power account unlocks → start another life with knowledge, not a shortcut | Continuity without trivializing survival |

## MVP playable roles

Names are original working names and [PROPOSED]. Group caps are [PROPOSED]. Mixed groups are allowed, but a group cap never grants a free combat multiplier.

| Archetype | Role; strengths | Weaknesses; counters | Diet | Group cap |
| --- | --- | --- | --- | --- |
| **Rillik** | Small fast omnivore/scavenger; acceleration, hiding, early warning, can use narrow routes | Low health and reach; exposed by scent sweeps, area denial, and patient ambushes | Omnivore: insects, fruit, small carcass portions | [PROPOSED] 6 |
| **Varkadon** | Mid-size pack predator; coordinated pressure, strong burst, reliable carcass defense | Loud approach, stamina debt, poor solo recovery; countered by terrain, group separation, and large-body threat displays | Carnivore: AI prey, carcasses, player kills | [PROPOSED] 4 |
| **Thalorun** | Large herbivore; threat display, endurance, body-blocking, route creation for allies | Slow turn and high resource demand; countered by exhaustion, ambush angles, and isolation | Herbivore: browse, seasonal plants, mineral sources | [PROPOSED] 5 |

Non-MVP roster candidates: a shoreline ambush specialist, a burrowing small omnivore, a long-distance migrator, a gliding canopy feeder, and an apex territorial herbivore. Each must earn a distinct social and ecological job before production.

## Predator / prey ecosystem

- Food chain: plants and small AI critters support herbivores; herbivores and carcasses support predators; scavengers convert leftovers into information and pressure.
- Carcass decay: [PROPOSED] a carcass moves through fresh, opened, stripped, and denuded states; scent and visible scavengers intensify, then the body becomes habitat evidence rather than loot.
- AI critters and players share readable food pressure but have different predictability: AI follows habitat needs; players can bait, escort, or abandon a route.
- Hunger and thirst create urgency; stamina determines whether urgency can become movement; health determines how many mistakes remain. Hunger/thirst must not silently replace health damage.
- Scent and sound are information, not wallhacks: wind, cover, speed, calls, blood, and wet ground alter reliability. The UI should not reveal an exact player location by default.

## Growth and meaningful death

| Stage | Design intent |
| --- | --- |
| Hatchling → juvenile | Learn routes and signals with strong friend proximity and protected exits. |
| Juvenile → sub-adult | Earn mobility and role expression; mistakes remain recoverable. |
| Sub-adult → adult | [PROPOSED] Reach adult in roughly 2–4 hours of active play, tuned by role and testing. |
| Adult → elder | Trade peak physical capability for lineage value, memory, and social leadership. |
| Death | The body ends; the account and remembered lineage do not become a combat advantage. |

On death, lose the current body, carried resources, local position, and current growth. Keep account identity, unlocked cosmetic/record presentation, approved memorial entries, and lineage history. [PROPOSED] A short grace/recovery path may preserve one non-trivial social token after an independently verified unfair disconnect; it must not refund ordinary combat deaths.

Permadeath-lite rules: no resurrection of the body; no permanent account wipe; no paid power recovery; clear cause-of-death messaging; a safe reconnect path; and a memorial/lineage record that names allies, place, and event when consent allows. Anti-frustration requires readable telegraphs, protected new-player starts, and a route back to friends.

## Existing code mapping

| Mechanic | Current code evidence | Status |
| --- | --- | --- |
| Health, positive damage, clamping, one death delegate | `UPSLStatsComponent`; `APSLCharacter::TakeDamage` and death handling | Implemented in the slice |
| Stamina drain/regen and sprint lock at zero | `UPSLSurvivalComponent`; sprint speed state in `APSLCharacter` | Implemented in the slice |
| Basic Character camera and movement handlers | `APSLCharacter` owns Character, spring arm, camera, and Enhanced Input slots | C++ plumbing exists; input assets and open map are not implemented |
| Hunger, thirst, food, water, rest, scent, sound, combat, flee | No corresponding component or system in current public headers | Not implemented |
| AI critters, carcasses, growth, lineage, nesting, memorials, persistence, replication | No corresponding system in current public headers | Not implemented |

Do not claim any row marked not implemented as playable. The current extension guidance keeps hunger/thirst separate from stamina, routes future damage through `TakeDamage`, and establishes a networking contract before replication.

## Cross-links

- Product promise and metrics: docs/design/GAME_VISION.md
- Original setting and biome purposes: docs/design/WORLD_AND_LORE.md
- Party, nesting, and protection rules: docs/design/SOCIAL_MULTIPLAYER.md
- Future architecture: docs/design/MULTIPLAYER_ARCHITECTURE.md
- Future delivery sequence: docs/design/ROADMAP.md

## Sources

- Current responsibilities and extension points: docs/ARCHITECTURE.md
- Current sprint/stamina boundary: docs/tasks/001-sprint-stamina.md
- UE Gameplay Framework reference used by the project: <https://dev.epicgames.com/documentation/en-us/unreal-engine/gameplay-framework-in-unreal-engine?application_version=5.8>

## Open questions

- Should adult timing vary by role, and what telemetry proves it is social rather than grindy?
- Which scent and sound cues remain learnable without turning stealth into an exact radar?
- What memorial data is safe to retain and share by default?
