# Multiplayer architecture

Status: PLAN. Today the game is single-player; no class replicates. Labels: **[VERIFIED-DOC]** read on an official Epic page; **[VERIFIED-ENGINE]** read in the installed UE 5.8.4 source (`/Users/Shared/Epic Games/UE_5.8`, CL 58902052); **[INFERENCE]**; **[PROPOSED]** design choice.

## 1. Principles

1. Server-authoritative. Clients send intent (move input, sprint pressed, ability requested); the server decides state. [VERIFIED-DOC: dedicated-server and RPC pages]
2. One unified *service* (accounts, friends, character records, server browser) over many *region instances*. No single physical server holds every player. [PROPOSED]
3. A live character is leased to one instance at a time. [PROPOSED; INFERENCE from industry pattern]
4. Build the smallest layer that the next milestone gate needs (see `ROADMAP.md`). No backend before M3.

## 2. Topology (target, M6)

```text
Client ──EOS Connect login──► Online service (EOS: auth, friends, presence, sessions; reports/sanctions)
   │                                   │
   │ server browser / join friend      ▼
   └──────UDP game traffic──────► Region instance (UE dedicated server, Linux)  ◄─► Character store + lease table
                                   EU-1, EU-2, NA-1, SEA-1, OCE-1 …                 (own backend; not EOS)
```

| Layer | Owns | Does NOT own | Milestone |
| --- | --- | --- | --- |
| Client | Input, prediction, presentation, UI | Any authoritative stat | M1 |
| Region instance (UE server) | Simulation: movement validation, Health/Stamina, damage, death, AI, nests | Account identity, cross-instance records | M1 (PIE), M2 (`-server`) |
| Character store | Character record, lease (`account → instance, expiry, generation`) | Simulation | M3 (local file/SQLite), M6 (hosted DB) |
| Online service (EOS) | Identity, friends, presence, sessions/server list, reports/sanctions | Gameplay simulation | M6 |

EOS facts: Online Services plugin is **Beta** in 5.8; Online Subsystem EOS is the older plugin; EOS Game Services use Connect and do not require an Epic account; EOS is advertised as free, hosting/moderation costs are separate; Trust & Safety includes Reports, Sanctions, Anti-Cheat. EOS sessions/lobbies are a service layer, not the authoritative simulation. [VERIFIED-DOC]

## 3. UE gameplay-framework mapping

| Concern | UE class / API | Rule |
| --- | --- | --- |
| Login, spawn, logout | `APSLGameMode : AGameModeBase` — `PreLogin`, `PostLogin`, `HandleStartingNewPlayer`, `RestartPlayer`, `ChoosePlayerStart`, `Logout` | Server only; GameMode does not exist on clients. [VERIFIED-DOC] |
| Match-wide replicated state | `AGameStateBase` | Instance phase, time of day (later). |
| Per-player replicated identity | `APlayerState` | Display name, party id (M4), character id (M3). |
| Player will | `APlayerController` | Owns the pawn → defines the owning connection for `Server` RPCs and `COND_OwnerOnly`. [VERIFIED-DOC] |
| Body | `APSLCharacter` (replicates by default as `ACharacter`) | Owns `UPSLStatsComponent`, `UPSLSurvivalComponent`. |
| Stats | `UPSLStatsComponent`, `UPSLSurvivalComponent` | `SetIsReplicatedByDefault(true)`; `UPROPERTY(ReplicatedUsing=OnRep_…)`; register in `GetLifetimeReplicatedProps` with `DOREPLIFETIME`/`DOREPLIFETIME_CONDITION`; include `Net/UnrealNetwork.h`. [VERIFIED-DOC] |
| Damage | `AActor::TakeDamage` → `ApplyDamage` | Mutate only when `HasAuthority()`. |
| Presentation-only events | `NetMulticast, Unreliable` | Never carries state; state comes from properties. |

RPC rules: `UFUNCTION(Server, Reliable|Unreliable)` with `_Implementation`; called by the owning client only. 5.8 docs examples omit `WithValidation`; the specifier still exists in `ObjectMacros.h` [VERIFIED-ENGINE]. Decision: validate (clamp/reject) inside `_Implementation`; do not use `WithValidation` (it disconnects the client on failure). [PROPOSED]

## 4. Movement and sprint

- `UCharacterMovementComponent` predicts on the autonomous client and the server corrects. [VERIFIED-DOC]
- Setting `MaxWalkSpeed` only on the server (current `HandleSprintStateChanged` path) makes client and server simulate different speeds → corrections/rubber-banding. [INFERENCE; observable with `p.NetShowCorrections 1`]
- Decision for M1: `UPSLCharacterMovementComponent` subclass. Sprint intent rides the saved move (`FLAG_Custom_0` via `GetCompressedFlags()` / `UpdateFromCompressedFlags(uint8)`; base `CanCombineWith` rejects differing flags, `CharacterMovementComponent.cpp:13161`). Stamina and the zero-stamina lock are simulated **per move** inside the CMC (`OnMovementUpdated` with the move's `DeltaSeconds`, skipped on `ROLE_SimulatedProxy` because `SimulateMovement` also calls it, `.cpp:2380`). Replay calls `PrepMoveFor` → `MoveAutonomous` → `PerformMovement` → `OnMovementUpdated` per saved move (`.cpp:8666-8691`, `10692`, `3105`), so client prediction, replay and server run the same rule. `PrepMoveFor` does not restore stamina; the stamina rule is a shared `PSLStamina::Step` (clears the lock when sprint is not wanted; `bMoving` = rounded move `Acceleration` non-zero); start values are saved in `SetInitialPosition`, and `CombineWith` first writes the old move's saved start values back into the CMC (base `CombineWith`, `.cpp:13268-13286`, does not rewind CMC stamina), end values are recorded in `PostUpdate(PostUpdate_Record)`; `ClientUpdatePositionAfterServerUpdate` saves/restores `bWantsToSprint` (`.cpp:8655,8718`). API names [VERIFIED-ENGINE: `CharacterMovementComponent.h` 1324, 2243, 2462, 3087, 3108, 3111, 3114, 3117, 3136, 3257; `Character.h:501`]. The design itself is [PROPOSED]. Mover 2.0 exists as a separate plugin; not adopted. [VERIFIED-DOC]
- Stamina correction: the client sends its end-of-move `SimStamina`/lock (server validates after `PerformMovement`, `.cpp:10062-10064`; filled in `ClientFillNetworkMoveData`, `.cpp:9735`) in a custom `FCharacterNetworkMoveData` (`SetNetworkMoveDataContainer`, `CharacterMovementComponent.h:2702`); the server's `ServerCheckClientError` (`h:2439`; no stamina parameter, so it reads the client values from `GetCurrentNetworkMoveData()` cast to the custom type) flags a stamina/lock mismatch as a client error, and a custom `FCharacterMoveResponseDataContainer` (`SetMoveResponseDataContainer`, `h:2728`; correction-only serialization at `.cpp:10912`) carries the authoritative values back before the client replays. Replicated `Stamina` on `UPSLSurvivalComponent` is for the HUD and other clients only; no threshold snap. A per-frame component tick would drift from per-move server simulation, so it is removed. [PROPOSED]

## 5. Replication scale choices

| Option | 5.8 status | Decision |
| --- | --- | --- |
| Legacy (generic) replication | Default | Use for M1–M5. |
| Iris | Experimental, opt-in (`bUseIris`, `net.Iris.UseIrisReplication`) [VERIFIED-DOC] | Re-evaluate at M5 with profiling data. |
| Replication Graph | Plugin for large actor counts [VERIFIED-DOC, 5.9 page] | Only if M5 profiling shows relevancy cost dominates. |

## 6. Hosting

| Stage | How | Constraint |
| --- | --- | --- |
| M1 | PIE, Net Mode *Play As Client* (dedicated server in PIE), 2 players; CQTest `FPIENetworkComponent` with `AsDedicatedServer()` | Editor only. |
| M2 | Uncooked editor: `UnrealEditor <uproject> /Game/Maps/L_TestArena -server -port=7777 -log` + clients `UnrealEditor <uproject> 127.0.0.1:7777 -game -log` [VERIFIED-DOC] | Dev Mac has 16 GB RAM [measured: `hw.memsize`]; server with `-nullrhi`; measure RSS. |
| M6 | Packaged `Server` target on Linux | Requires a **source-built engine** [VERIFIED-DOC]; Linux via cross-compile toolchain [VERIFIED-DOC]. Mac as production server target is not documented → not assumed. [INFERENCE] |

## 7. Persistence and reconnect

- `USaveGame` / `SaveGameToSlot` writes local device slots → not usable as online authority. [VERIFIED-DOC]
- Short reconnect: `AddInactivePlayer`, `FindInactivePlayer`, `OverridePlayerState`, `InactivePlayerStateLifeSpan` exist on **`AGameMode`**, not `AGameModeBase` [VERIFIED-ENGINE: `GameMode.h:144,183,187,190`]. `APSLGameMode` derives from `AGameModeBase`; M3 must either reparent to `AGameMode` or implement its own disconnect table. Decision deferred to M3 contract. [PROPOSED]
- M3 character record [PROPOSED]: `{character_id, account_id, species, growth, health, stamina, hunger, thirst, location, alive, generation}`; lease `{account_id, instance_id, expires_at, generation}`. Write-through on: death, logout, every 60 s, server shutdown. Death is final: a dead record never resurrects.
- Disconnect: body stays in world, server-owned, vulnerable for 20 s [PROPOSED; see `SOCIAL_MULTIPLAYER.md`]; reconnect within the lease window repossesses the same pawn.
- Seamless travel is for map transitions, not durable storage. [VERIFIED-DOC]

## 8. Security and anti-cheat baseline

| Threat | Mitigation | Milestone |
| --- | --- | --- |
| Client sets own Health/Stamina | Properties written only under `HasAuthority()`; clients have no setter RPC | M1 |
| Speed hack | CMC server checks + speed derived server-side from validated stamina state | M1 |
| RPC spam / out-of-range values | Clamp in `_Implementation`; rate-limit per connection [PROPOSED] | M1 (clamp), M2 (rate limit) |
| Combat logging / server hopping | Body linger + lease + switch cooldown | M3 |
| Secrets in client | EOS client secret / server credentials never shipped in client builds; `scripts/check_secrets.sh` [VERIFIED-DOC: OSS EOS page] | M6 |
| Cheating clients | EOS Anti-Cheat evaluation | M6 |

## 9. Technical risks

| ID | Risk | Likelihood | Mitigation |
| --- | --- | --- | --- |
| R1 | Sprint corrections with server-only `MaxWalkSpeed`, or stale stamina-lock on the client | High | §4 per-move sprint + stamina in CMC; Task 002 C3, N8–N10 |
| R2 | Per-frame stamina tick diverges from per-move server simulation | High | Remove component tick drain; simulate in CMC per move (Task 002 C2/C3) |
| R3 | `bDeathHandled`/`DisableInput` only on server → client keeps sending input | Medium | `OnRep` for death on owning client |
| R4 | Enhanced Input mapping added in `BeginPlay` before possession on clients | High | Add mapping in `NotifyControllerChanged` (`Pawn.h:397` [VERIFIED-ENGINE]); Task 002 C4 |
| R5 | CQTest network tests are editor-only (`ENABLE_PIE_NETWORK_TEST = WITH_EDITOR && …`) and need `UnrealEd`/`LevelEditor` | Certain | Guard test code and module deps with `Target.bBuildEditor` [VERIFIED-ENGINE] |
| R6 | `scripts/verify_test_report.py` only discovers `IMPLEMENT_*_AUTOMATION_TEST`/`DEFINE_SPEC`, not `NETWORK_TEST_CLASS` | Certain | Task 002 extends the discovery regex to emit `Dir.Class.Method` per `TEST_METHOD` (scripts, not harness) [VERIFIED: source read] |
| R7 | 16 GB host runs editor + PIE 2 clients + other agent's build | Medium | One Unreal job at a time via harness lock |
| R8 | Source engine needed for packaged server | Certain (M6) | Decision gate at M5 exit |

## 10. Sources

- Dedicated servers: <https://dev.epicgames.com/documentation/en-us/unreal-engine/setting-up-dedicated-servers-in-unreal-engine?application_version=5.8>
- Replicated properties: <https://dev.epicgames.com/documentation/en-us/unreal-engine/replicate-actor-properties-in-unreal-engine?application_version=5.8>
- RPCs: <https://dev.epicgames.com/documentation/en-us/unreal-engine/remote-procedure-calls-in-unreal-engine?application_version=5.8>
- Owner/owning connection: <https://dev.epicgames.com/documentation/en-us/unreal-engine/actor-owner-and-owning-connection-in-unreal-engine?application_version=5.8>
- GameMode/GameState: <https://dev.epicgames.com/documentation/en-us/unreal-engine/game-mode-and-game-state-in-unreal-engine?application_version=5.8>
- Testing multiplayer: <https://dev.epicgames.com/documentation/en-us/unreal-engine/testing-multiplayer-in-unreal-engine?application_version=5.8>
- Command line: <https://dev.epicgames.com/documentation/en-us/unreal-engine/command-line-arguments-in-unreal-engine?application_version=5.8>
- Network emulation: <https://dev.epicgames.com/documentation/en-us/unreal-engine/network-emulation-in-unreal-engine?application_version=5.8>
- Networking Insights: <https://dev.epicgames.com/documentation/en-us/unreal-engine/networking-insights-in-unreal-engine?application_version=5.8>
- Iris: <https://dev.epicgames.com/documentation/en-us/unreal-engine/introduction-to-iris-in-unreal-engine?application_version=5.8>
- Replication Graph (5.9 page): <https://dev.epicgames.com/documentation/en-us/unreal-engine/replication-graph-in-unreal-engine?application_version=5.9>
- Mover 2.0: <https://dev.epicgames.com/documentation/en-us/unreal-engine/mover-2-0-in-unreal-engine?application_version=5.8>
- SaveGame: <https://dev.epicgames.com/documentation/en-us/unreal-engine/saving-and-loading-your-game-in-unreal-engine?application_version=5.8>
- Online Services (Beta): <https://dev.epicgames.com/documentation/en-us/unreal-engine/online-services-in-unreal-engine?application_version=5.8>
- OSS EOS: <https://dev.epicgames.com/documentation/en-us/unreal-engine/online-subsystem-eos-plugin-in-unreal-engine?application_version=5.8>
- EOS overview / Connect / Trust & Safety: <https://dev.epicgames.com/docs/epic-online-services/eos-overview> ; <https://dev.epicgames.com/docs/epic-online-services/eos-fundamentals/connect-interface> ; <https://dev.epicgames.com/docs/epic-online-services/trust-and-safety>
- Linux cross-compile: <https://dev.epicgames.com/documentation/en-us/unreal-engine/cross-compiling-for-linux-in-unreal-engine?application_version=5.8>
- Engine source read locally: `Engine/Source/Runtime/Engine/Classes/GameFramework/{CharacterMovementComponent.h,GameMode.h,Pawn.h}`, `Engine/Source/Developer/CQTest/Public/Components/PIENetworkComponent.h`, `Engine/Source/Runtime/Engine/Private/Net/NetEmulationHelper.cpp`.

## 11. Open questions

- Reparent `APSLGameMode` to `AGameMode` at M3, or own the disconnect table?
- Character store backend at M6 (managed Postgres vs. key-value) — decide with M3 schema in hand.
- Instance size: start at 40, raise only with server tick ≥ 30 Hz measured at M5. [PROPOSED]
