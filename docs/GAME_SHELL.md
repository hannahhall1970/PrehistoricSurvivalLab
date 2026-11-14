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
