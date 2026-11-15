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
- `PSL_GAMESHELL_STEPS=fonts` (comma list of `species`, `fonts`; default all) limits the build phase; verify always checks everything.
- `SourceArt/Fonts/PROVENANCE.txt` pins the source: google/fonts commit `c13390277dfb1fcc7415d84ef7ef9cfc1e52f8c1` (`ofl/bevietnampro`, OFL in `OFL.txt`).
