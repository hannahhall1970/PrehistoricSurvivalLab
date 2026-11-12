# Research Notes

**Scope.** Primary-source inspection for the UE 5.8 vertical slice. Claims are labeled **[Observed]** (directly present in a source) or **[Inference]** (application to this project). The local UE toolchain is unavailable, so this document records source research, not a compile/run result.

## Sources

### Official Unreal Engine 5.8 documentation

- [Observed] [Gameplay Framework](https://dev.epicgames.com/documentation/en-us/unreal-engine/gameplay-framework-in-unreal-engine?application_version=5.8): GameMode is server-side; GameState and PlayerState replicate; Controllers possess Pawns; Characters include movement, mesh, and capsule components; Actors contain Components; HUD and Camera are framework classes; C++/Blueprint combinations are supported.
- [Observed] [Enhanced Input](https://dev.epicgames.com/documentation/en-us/unreal-engine/enhanced-input-in-unreal-engine?application_version=5.8): Input Actions, Mapping Contexts, Modifiers, Triggers; contexts can be added/removed/prioritized through the local-player subsystem; C++ bindings use `UEnhancedInputComponent` and `ETriggerEvent`.
- [Observed] [Gameplay Tags](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-gameplay-tags-in-unreal-engine?application_version=5.8): hierarchical tags represent state/events; tags are defined in Project Settings, Data Tables, or C++ (`UE_DECLARE_GAMEPLAY_TAG_EXTERN`/`UE_DEFINE_GAMEPLAY_TAG*`); `FGameplayTagContainer` supports add/remove/query; `GameplayTags` is required in `Build.cs` for C++.
- [Observed] [Automation Test Framework](https://dev.epicgames.com/documentation/en-us/unreal-engine/automation-test-framework-in-unreal-engine?application_version=5.8): C++ unit/feature/content-stress tests run through Unreal systems; tests are not `UObject` classes; tests should not assume order/state and should clean up generated files.
- [Observed] [Asset Management](https://dev.epicgames.com/documentation/en-us/unreal-engine/asset-management-in-unreal-engine?application_version=5.8): `UAssetManager` manages Primary Assets; `UPrimaryDataAsset`, soft references, bundles, and streamable loading support controlled discovery/loading/unloading.
- [Observed] [Lyra Sample Game](https://dev.epicgames.com/documentation/en-us/unreal-engine/lyra-sample-game-in-unreal-engine?application_version=5.8): Lyra uses modular Gameplay Feature plugins and `LyraExperienceDefinition`-based Experiences, with separate core/game-mode/map content.

### Inspected GitHub repositories and exact paths

- [Observed] [ActionRoguelike README](https://github.com/tomlooman/ActionRoguelike/blob/master/README.md): current README says main branch is UE 5.8; lists Enhanced Input, a GAS-like action system, attributes, event-driven UI, Gameplay Tags, multiplayer, Behavior Trees/EQS, DataAssets, Asset Manager, and async loading. It also warns the main branch is experimental and some systems are not always multiplayer-ready. The repository did not expose a license in metadata; reading `LICENSE` returned 404. **Code license: unverified; do not copy code.**
- [Observed] [ActionRoguelike `RogueActionComponent.h`](https://github.com/tomlooman/ActionRoguelike/blob/master/Source/ActionRoguelike/ActionSystem/RogueActionComponent.h) and [`RogueActionComponent.cpp`](https://github.com/tomlooman/ActionRoguelike/blob/master/Source/ActionRoguelike/ActionSystem/RogueActionComponent.cpp): reusable `UActorComponent`, no tick, replicated action/attribute state, authority checks, server RPCs, Gameplay Tags, and native/Blueprint attribute delegates.
- [Observed] [ActionRoguelike `RogueAttributeSet.h`](https://github.com/tomlooman/ActionRoguelike/blob/master/Source/ActionRoguelike/ActionSystem/RogueAttributeSet.h): tagged attributes expose base plus transient modifier, clamp calculated values, and broadcast structured modifications; health and movement speed are attribute-set examples.
- [Observed] [ActionRoguelike `RoguePlayerCharacter.h`](https://github.com/tomlooman/ActionRoguelike/blob/master/Source/ActionRoguelike/Player/RoguePlayerCharacter.h): Character owns Spring Arm, Camera, and Action components; input is Enhanced Input; configuration is exposed through a data asset.
- [Observed] [EpicSurvivalGame README](https://github.com/tomlooman/EpicSurvivalGame/blob/master/README.md): project started in 2015 and explicitly uses “very old standards and conventions”; it recommends ActionRoguelike for current work.
- [Observed] [EpicSurvivalGame `SCharacter.h`](https://github.com/tomlooman/EpicSurvivalGame/blob/master/SurvivalGame/Source/SurvivalGame/Public/Player/SCharacter.h), [`SCharacter.cpp`](https://github.com/tomlooman/EpicSurvivalGame/blob/master/SurvivalGame/Source/SurvivalGame/Private/Player/SCharacter.cpp): Spring Arm/Camera, timer-driven hunger, replicated hunger, health restoration, and server-authoritative use/damage flow; input uses legacy `BindAxis`/`BindAction`.
- [Observed] [EpicSurvivalGame `SConsumableActor.h`](https://github.com/tomlooman/EpicSurvivalGame/blob/master/SurvivalGame/Source/SurvivalGame/Public/Items/SConsumableActor.h), [`SConsumableActor.cpp`](https://github.com/tomlooman/EpicSurvivalGame/blob/master/SurvivalGame/Source/SurvivalGame/Private/Items/SConsumableActor.cpp): consumable Actor restores health and hunger through a reusable `Nutrition` property.
- [Observed] [EpicSurvivalGame `LICENSE`](https://github.com/tomlooman/EpicSurvivalGame/blob/master/LICENSE): **MIT License**, copyright Tom Looman (2020).
- [Observed] [GASDocumentation README](https://github.com/tranek/GASDocumentation/blob/master/README.md): unofficial GAS guide/sample, current there at UE 5.3; covers ASC placement, replicated Attributes, Gameplay Effects/Abilities/Cues/Tags, prediction, damage, sprint stamina, and Blueprint/C++ ability mixtures. It explicitly says the best documentation is plugin source.
- [Observed] [GASDocumentation `LICENSE`](https://github.com/tranek/GASDocumentation/blob/master/LICENSE): **MIT License**, copyright Dan Kestranek (2020).

## Patterns adopted (mapped to the contract)

- [Observed] UE Gameplay Framework makes Character/Pawn, Controller, GameMode, HUD, Camera, and Actor Components composable. **[Inference]** `APSLCharacter` owns camera components plus `UPSLStatsComponent` and `UPSLSurvivalComponent`; `APSLGameMode` selects the pawn/HUD; `APSLHUD` is intentionally minimal.
- [Observed] ActionRoguelike keeps reusable, non-visual state in an Actor Component and uses delegates for changes. **[Inference]** Stats and stamina remain separate components with no combat component; delegates are the event boundary for HUD/tests.
- [Observed] Enhanced Input uses data assets and contexts rather than legacy string axis/action bindings. **[Inference]** `APSLCharacter` exposes `DefaultMappingContext`, Move/Look/Jump/Sprint actions for `BP_PSLCharacter`; do not adopt EpicSurvivalGame's `BindAxis`/`BindAction` pattern.
- [Observed] The inspected samples use Spring Arm + Camera for third-person view. **[Inference]** the contract's Spring Arm/Camera setup is the smallest matching pattern; no camera framework abstraction is needed.
- [Observed] Rogue attributes support clamping and change listeners; EpicSurvivalGame models hunger as a bounded value and applies self-damage at a timer threshold. **[Inference]** `UPSLStatsComponent` owns bounded health/death semantics and `UPSLSurvivalComponent` owns pure-logic stamina drain/regen. Hunger/thirst remain explicitly out of this slice.
- [Observed] EpicSurvivalGame routes damage through `TakeDamage`; Rogue action state is authority-gated and replicated. **[Inference]** damage enters through `APSLCharacter::TakeDamage` and calls Stats; no separate combat component or GAS dependency is justified yet.
- [Observed] Gameplay Tags are hierarchical labels suitable for state/events; Rogue uses tags for actions and active state. **[Inference]** keep current health/stamina delegates simple; introduce native tags only when a second system needs shared event/state vocabulary.
- [Observed] Automation tests are C++ tests that should be isolated from mutable Editor state. **[Inference]** `Private/Tests/PSLComponentTests.cpp` should exercise Stats/Survival pure logic, clamping, transitions, and death-once behavior without requiring a map or asset.
- [Observed] Asset Manager/Primary Assets are for controlled discovery and async loading. **[Inference]** current Blueprint input/character references can stay ordinary editor properties; add soft references/Asset Manager only when content size or loading needs make it useful.

## Patterns too complex for now

- [Observed] GAS adds ASC, AttributeSets, Gameplay Effects, Abilities, Cues, Tags, replication, and prediction; the inspected guide requires C++ setup and is UE 5.3-specific. **[Inference]** defer GAS: the vertical slice needs only two local components, direct `TakeDamage`, and deterministic tests.
- [Observed] Lyra uses Experiences plus Gameplay Feature plugins to activate modular game-mode/map systems. **[Inference]** defer Lyra Experiences/GameFeatures; one GameMode, one map, and one character are enough for this slice.
- [Observed] ActionRoguelike lists Behavior Trees, EQS, custom BT nodes, DataAssets, and async Asset Manager loading. **[Inference]** defer AI controller/BT/EQS, custom action framework, pooling, and async content until a concrete dinosaur AI or loading requirement exists.
- [Inference] Mass, multiplayer session infrastructure, and broad inventory/equipment architecture are outside the contract; do not add them as scaffolding.

## Engine-version concerns

- [Observed] ActionRoguelike's README identifies the main branch as UE 5.8, but also calls it an experimental playground and warns some systems may not yet support multiplayer. Treat it as a pattern source, not a stability guarantee.
- [Observed] EpicSurvivalGame's README calls itself old (started in 2015); its observed character code uses legacy input binding and older RPC validation style. **[Inference]** do not port those APIs into UE 5.8; use Enhanced Input and verify exact UE 5.8 signatures when compiling.
- [Observed] The official pages above were opened with `application_version=5.8`; current documentation can still evolve. **[Inference]** re-check API signatures after the UE 5.8 installation is available, especially Automation flags and Enhanced Input delegates.
- [Observed] No Unreal Editor/Engine is installed in the current environment. **[Inference]** C++ compilation, Automation tests, and Blueprint asset references remain unverified until UE 5.8 is installed.

## License & attribution

- [Observed] EpicSurvivalGame: MIT. If any code is ever copied (not merely used as a pattern), retain its copyright/license notice and attribute Tom Looman.
- [Observed] GASDocumentation: MIT. If any code is ever copied, retain Dan Kestranek's copyright/license notice. Its README is explicitly unofficial and not Epic documentation.
- [Observed] ActionRoguelike: no `LICENSE` file was found at the inspected repository path and no license was exposed by repository metadata. **License is unverified; do not copy its code.** Its README separately says included Paragon-referencing game assets are under Unreal Engine EULA restrictions; do not copy sample assets, trademarks, or names.
- [Observed] Lyra: the official documentation page describes access through Fab/Epic tooling but does not provide a code license text on the page. **License terms for Lyra code/assets are unverified here; use it as architectural reference only and obtain/verify the applicable Epic terms before reuse.**
- [Inference] This project will implement its own C++ and Blueprint code and use only assets legally available under the project's Unreal Engine terms; no third-party source or binary assets are copied by this research.
