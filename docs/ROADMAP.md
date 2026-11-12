# Roadmap

## P0 — make the slice runnable

- Install UE 5.8 and Xcode 26.1.x with Metal Toolchain.
- Compile with `scripts/build.sh`.
- Run `scripts/run_tests.sh` and resolve failures.
- Create and wire input assets, `BP_PSLCharacter`, `L_TestArena`, damage volume, and GameMode defaults in the Editor.

## P1 — player survival loop

- Add hunger and thirst components/state with clear drain/regen rules.
- Add a combat component; keep incoming damage routed through `TakeDamage`.
- Replace debug DrawHUD with a minimal UMG HUD.

## P2 — world and persistence

- Add dinosaur AI using Behavior Tree or StateTree.
- Add SaveGame persistence for player survival state and progression.

## P3 — networking

- Define server authority, replication, relevancy, and presentation-event rules, then add replication deliberately.
