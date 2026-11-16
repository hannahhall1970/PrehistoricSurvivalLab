# Game Shell

## Localization (EN / VI)

- Namespace `PSL` (`#define LOCTEXT_NAMESPACE "PSL"`), native culture `en`. Source of truth: `Content/Localization/Game/{en,vi}/Game.po`, entries `msgctxt "PSL,<Key>"`, `msgid` = English source string, `msgstr` = translation.
- Gather/compile (trusted parent, under the build lock, one Unreal process at a time):

```
"$UE_ROOT/Engine/Binaries/Mac/UnrealEditor-Cmd" PrehistoricSurvivalLab.uproject \
  -run=GatherText -config=Config/Localization/Game.ini -unattended -nullrhi -nosplash
```

  Output: `Game.manifest`, per-culture `Game.archive` and `Game.locres` next to the `.po` files. `PSL.Loc.PoComplete` fails when they are missing.
- Switching culture: `UPSLGameUserSettings::ApplyCulture()` resolves `CultureName` (`en`, `vi`, else OS default `vi*` -> `vi`, otherwise `en`) and calls `UKismetInternationalizationLibrary::SetCurrentCulture`. The Settings > Language buttons preview live and Apply persists. MainMenu, SpeciesSelect, Pause, GameOver, Hud and Settings widgets bind `FInternationalization::OnCultureChanged()` and call `RefreshTexts()`.
- Persistence: `UPSLGameUserSettings::CultureName` (config `GameUserSettings`, written by `SaveSettings`). Do not rely on `Internationalization.Culture` (see editor limitations in `TEST_PLAN.md`).
- Editor note: the editor loads non-native game locres only when `Localization.ForceLoadGameLocalizationInEditor=1` (or game localization preview is on). Tests that need Vietnamese text set the CVar and restore it.

## Fonts (Be Vietnam Pro)

- `scripts/build_game_shell.py` fonts step imports `SourceArt/Fonts/BeVietnamPro-{Regular,SemiBold,Bold}.ttf` (sha256-pinned in the script) as `UFontFace` assets `/Game/UI/Fonts/FF_BeVietnamPro_{Regular,SemiBold,Bold}` with LoadingPolicy **Inline** (the import factory leaves it unset, and only Inline embeds the bytes in cooked builds) and Hinting Default.
- `/Game/UI/Fonts/F_BeVietnamPro` is a Runtime-cache `UFont` whose default typeface entries `Regular`, `SemiBold`, `Bold` reference those faces. Python cannot set `FCompositeFont` or `FontCacheType`, so `UPSLFontBuildLibrary::SetRuntimeTypefaces` does it (test `PSL.Fonts.BuildLibrary`).
- `FPSLUiStyle` loads the font by path, which the cooker cannot follow, so `Config/DefaultGame.ini` lists `/Game/UI/Fonts` in `DirectoriesToAlwaysCook`.
- `PSL_GAMESHELL_STEPS=fonts` (comma list of `species`, `fonts`, `audio`; default all) limits the build phase; verify always checks everything.
- `SourceArt/Fonts/PROVENANCE.txt` pins the source: google/fonts commit `c13390277dfb1fcc7415d84ef7ef9cfc1e52f8c1` (`ofl/bevietnampro`, OFL in `OFL.txt`).

## Fonts (runtime use)

- Composite font `/Game/UI/Fonts/F_BeVietnamPro` (BeVietnamPro, OFL) with typefaces `Regular`, `SemiBold`, `Bold`. Never build `FSlateFontInfo` by hand: use `FPSLUiStyle::Font(Size, EPSLFontWeight)` or `FPSLUiStyle::MakeText(Tree, Text, Size, Color, Weight)` (HUD `MakeLabel` forwards to it). The weight defaults to Regular; there is no `bool bBold`.
- Weights per role: titles and headings (menu/game-over title 64, pause/settings/species headings 32, species name 26) Bold; button labels (`UPSLUiButton::Make`), HUD captions (HEALTH/STAMINA/HUNGER), HUD state text (EXHAUSTED/SPRINTING) and the species diet badge SemiBold; body text (stats, descriptions, toasts, settings rows, HUD values/hint) Regular.
- Fallback: when the font asset is missing the engine default font is used (Regular -> "Regular", SemiBold and Bold -> "Bold", since the engine default has no SemiBold).
- Long Vietnamese strings wrap instead of widening panels: Pause toast/warning (wrap at 432, the 480 panel minus padding), Settings status/confirm text (560), GameOver stats and species description (480), HUD state/hint labels (auto wrap inside the 296 px panel content). `PSL.UI.VietnameseLayout` guards this.

## Audio

- Source: nine project-original WAVs in `SourceArt/Audio` (44.1 kHz, sha256-pinned in `scripts/build_game_shell.py` and `SourceArt/Audio/PROVENANCE.txt`). Never regenerate them: the noise files were rendered without a seed, so a regeneration changes the bytes and the pinned hashes.
- Assets in `/Game/Audio`: SoundWaves `MUS_Menu`, `MUS_Valley`, `AMB_Wind`, `SFX_UI_Click`, `SFX_UI_Hover`, `SFX_Bite`, `SFX_Eat`, `SFX_GameOver`, `SFX_Starving`; SoundClasses `SC_Master`, `SC_Music`, `SC_SFX`; SoundMix `SM_PSL_Volume` (no class effects).
- Routing: `MUS_*` -> `SC_Music`; `AMB_Wind` and every `SFX_*` -> `SC_SFX`. `MUS_Menu`, `MUS_Valley` and `AMB_Wind` loop. Wave volume stays 1.0 and the import applies no processing.
- Hierarchy: `SC_Music` and `SC_SFX` are children of `SC_Master`. `USoundClass::ParentClass` has no `EditAnywhere` (python cannot set it) and the engine's `SetParentClass` does not add the child to the parent's `ChildClasses`, so the build uses `UPSLAudioBuildLibrary::SetSoundClassParent(Child, Parent)` (`unreal.PSLAudioBuildLibrary.set_sound_class_parent`), which updates both sides.
- Volume: `UPSLGameUserSettings::ApplyAudio` sets `SM_PSL_Volume` as the base mix (`SetBaseSoundMix`) with per-class overrides `SC_Master` = M, `SC_Music` = Mu, `SC_SFX` = S. The engine multiplies a child by its parent, so the effective volumes are Master = M, Music = M*Mu, SFX = M*S.
- Default mix: `Config/DefaultEngine.ini` `[/Script/Engine.AudioSettings] DefaultBaseSoundMix=/Game/Audio/SM_PSL_Volume.SM_PSL_Volume`, so every audio device starts with `SM_PSL_Volume` as its base mix before `ApplyAudio` sets the overrides.
- Playback sites: menu music in `APSLMenuGameMode::BeginPlay`; valley music and wind in `APSLGameMode::BeginPlay`; bite in `APSLCharacter::TryBite`; eat via `EatSound`; starving on the hunger 0 transition; game-over as a 2D cue; UI click/hover on the buttons.
- Build: `PSL_GAMESHELL_STEPS=audio` limits the build phase to this step; verify always checks everything (classes, routing, looping, channels, duration, volume, import source file).
- Cook: every sound is loaded by path (`PSLAudio::Load`, `FPSLUiStyle`) or through native-constructor soft refs (`UPSLGameUserSettings` SC_*/SM_*, `APSLCharacter::EatSound`), which the cooker does not follow, so `Config/DefaultGame.ini` lists `/Game/Audio` in `DirectoriesToAlwaysCook` (the M2c5b cook without it shipped only `SM_PSL_Volume`).

## Packaging

- `Config/DefaultGame.ini` `[/Script/UnrealEd.ProjectPackagingSettings]`: `MapsToCook` L_MainMenu + L_LostValley; `DirectoriesToAlwaysCook` `/Game/UI/Fonts` + `/Game/Audio`; `CulturesToStage` en + vi; `InternationalizationPreset=All` (ICU `vi`/`vi_VN` data).
- License: `Content/Licenses/BeVietnamPro/OFL.txt` is a byte-identical copy of `SourceArt/Fonts/OFL.txt`, staged loose (outside the pak) via `DirectoriesToAlwaysStageAsNonUFS=(Path="Licenses/BeVietnamPro")` (path relative to `Content/`). Update both copies together.
- `PSL.Config.Packaging` pins all of the above.

## Main menu scene (L_MainMenu)

- Built only by `scripts/create_main_menu.py` (trusted parent, under the build lock; `--selftest` checks placement and framing without the editor). It rebuilds the existing map in place (makes the LFS read-only `.umap` writable, clears its actors) and writes `Saved/PSL/main-menu.json`.
- Contents, all from existing project assets:
  - Ground plane with `M_PSL_Terrain`.
  - Golden-hour lighting, all movable: `PSL_Menu_Sun`, `PSL_Menu_SkyAtmosphere`, `PSL_Menu_SkyLight` (real-time capture), `PSL_Menu_Fog`.
  - `PSL_Menu_PostProcess`: unbound, with fixed exposure (min = max = 1) so the lit snout no longer blows out.
  - Seeded HISM backdrop `PSL_Menu_{Trees,Cliffs,Rocks,Bushes,Grass}` from `/Game/Environment/Nature`, capped by `BUDGET` (at most 538 instances).
- Framing at 16:9:
  - The UI panel covers screen x 0.05..0.50.
  - `MenuPreview` (`APSLMenuPreviewActor`) stands at (0, 350, 0) with yaw 205, so it faces the camera with the head angled at the panel. Its centre lands at screen x 0.77. Bushes, rocks and near trees keep out of its screen column (±0.2), so only grass is in front of it.
  - Distant cliffs are scaled 10..18 at 20..28 km, so the fog softens them instead of a wall of grey rock.
  - `MenuCameraRig` stands at (-1400, 0, 200) with pitch -4, horizontal FOV 50, no aspect constraint, and a 60 uu dolly over 30 s.
- The preview no longer spins 360 degrees, which used to turn its back or tail into the panel. It sways ±20 degrees around its placed yaw over a 16 s period (`SwayYawOffset`). The species screen still swaps the mesh through `SetSpecies`.
- Startup maps (`Config/DefaultEngine.ini`):
  - `GameDefaultMap` is `L_MainMenu`, so a game build boots into the menu.
  - `EditorStartupMap` stays `L_LostValley` on purpose: the editor opens on gameplay, and PIE on `L_MainMenu` exercises the menu.
  - `PSL.Config.StartupMaps` pins both.
