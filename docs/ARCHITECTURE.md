# Architecture

## Module

`PrehistoricSurvivalLab` depends on Core, CoreUObject, Engine, InputCore, and EnhancedInput. No GAS, Mass, plugin, or multiplayer infrastructure is required for this slice.

## Responsibilities

| Class | Responsibility |
| --- | --- |
| `UPSLStatsComponent` | Owns MaxHealth/Health; applies clamped positive damage; broadcasts health changes and one death delegate. No tick. |
| `UPSLSurvivalComponent` | Owns stamina and sprint request state; pure `UpdateStamina` drains while requested and moving, regenerates otherwise, clamps values, and broadcasts state/value transitions. |
| `APSLCharacter` | Owns stats, survival, spring arm, and camera; consumes Enhanced Input; maps sprint state to movement speed; routes `TakeDamage`; disables movement/input on death. |
| `APSLGameMode` | Uses `APSLHUD` and falls back to `APSLCharacter`; Blueprint configuration supplies `BP_PSLCharacter` as the default pawn. |
| `APSLHUD` | Minimal debug DrawHUD display for health, stamina, sprinting, and death. |

## Data flow

- Input → `SetSprintRequested` → `UPSLSurvivalComponent::UpdateStamina` → `OnSprintStateChanged` → character movement `MaxWalkSpeed` (`300` walk / `600` sprint).
- `AActor::TakeDamage` override → `UPSLStatsComponent::ApplyDamage` → `OnHealthChanged`; reaching zero broadcasts `OnDeath` once → character disables input and movement and logs once.
- Survival tick derives movement from owner velocity (`Size2D`) but the pure update method remains directly testable.

## Extension points

- Add hunger/thirst as separate survival state and delegates; do not overload stamina semantics.
- Add a combat component whose damage output enters through `TakeDamage`.
- Add dinosaur AI with Behavior Trees or StateTree after the player loop is stable.
- For replication, make authoritative health/stamina changes server-owned, replicate state or attributes, and multicast only presentation events; first establish a networking contract before adding infrastructure.
