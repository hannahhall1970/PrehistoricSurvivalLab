# Test Plan

## Automated (`PSL.`)

Run `./scripts/run_tests.sh` after installing UE 5.8. Tests cover:

- Initial health/stamina values.
- Sprint transitions and delegate transitions.
- One second sprint drain: stamina `100 → 80`; one second regeneration: `80 → 92`.
- Clamping at zero and maximum; no drain while not moving.
- Empty stamina stops sprinting and requires releasing and re-pressing sprint.
- Positive damage, health clamp, no negative health, zero/negative damage ignored.
- Death broadcasts exactly once; damage after death is ignored.

## Manual Editor checklist

- Walk speed is `300`; sprint speed is `600`.
- Hold sprint while moving: stamina empties after `5` seconds from full.
- Full stamina regeneration takes about `8.33` seconds (`100 / 12`).
- Releasing and re-pressing sprint is required after empty stamina.
- Damage reaches StatsComponent; health reaches zero once; character disables movement/input.
- HUD displays health, stamina, sprinting state, and `DEAD`.
- Test map loads with `BP_PSLCharacter` as the default pawn and damage volume behaves as configured.

Record actual engine/editor version and results. Until the required toolchain is installed, all checks are unverified.
