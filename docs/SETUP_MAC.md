# macOS Setup

## Toolchain

1. Install Epic Games Launcher and Unreal Engine **5.8**. Choose minimal components: skip Android, iOS, and Linux targets, and skip starter-content packs not needed by this project to save disk (download sizes vary; expect many GB).
2. Download Xcode **26.1.x** from [developer.apple.com](https://developer.apple.com/xcode/) and install the Metal Toolchain. Avoid Xcode 26.4+ with UE 5.8.2 because its hotfix notes report Mac compile issues.
3. Select and initialize Xcode:

```bash
sudo xcode-select -s /Applications/Xcode.app/Contents/Developer
sudo xcodebuild -runFirstLaunch
sudo xcodebuild -license
```

1. Check the environment and build one job at a time:

```bash
./scripts/audit_env.sh
./scripts/build.sh
./scripts/run_tests.sh
```

Set `UE_ROOT` if UE is elsewhere. Open `PrehistoricSurvivalLab.uproject` in the Editor after the build.

## Required Editor steps

1. Under `Content/PSL/Input` create Input Actions `IA_Move` (Axis2D), `IA_Look` (Axis2D), `IA_Jump` (Digital), `IA_Sprint` (Digital), and `IMC_Default` mapping: W/S/A/D → `IA_Move` (W: Swizzle YXZ; S: Swizzle YXZ + Negate; A: Negate; D: none), Mouse XY 2D → `IA_Look` (Negate Y if inverted), Space → `IA_Jump`, Left Shift → `IA_Sprint`.
2. Create `BP_PSLCharacter` from `APSLCharacter`; add the Mannequin from Third Person template content via **Add > Add Feature or Content Pack > Third Person**.
3. Assign the input assets on `BP_PSLCharacter`.
4. Create small `L_TestArena` with a floor and cubes, plus a damage volume using a Blueprint that calls `ApplyDamage` or a simple pain-causing actor (`APainCausingVolume` exists in the engine).
5. Set the GameMode `DefaultPawnClass` to `BP_PSLCharacter`.
6. Save all assets. Commit binary assets through Git LFS.

## Low-memory tips

Use Scalability Low/Medium, disable Lumen and Nanite in the test map, close other applications, and avoid running Editor, build, and tests concurrently. No Editor-created assets exist until these steps are completed.
