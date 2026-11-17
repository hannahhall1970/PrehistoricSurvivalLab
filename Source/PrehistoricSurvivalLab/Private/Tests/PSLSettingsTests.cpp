#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Scalability.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Slider.h"
#include "HAL/IConsoleManager.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "PSLCharacter.h"
#include "PSLGameUserSettings.h"
#include "PSLPauseWidget.h"
#include "PSLSettingsWidget.h"
#include "PSLUiButton.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"
#include "UObject/UObjectGlobals.h"

namespace
{
    // Captures everything the settings tests touch and puts it back (config included) on scope exit.
    struct FSettingsSnapshot
    {
        UPSLGameUserSettings* Settings = nullptr;
        float Sensitivity = 1.0f;
        bool bInvertY = false;
        float Master = 1.0f, Music = 1.0f, Sfx = 1.0f;
        FString Culture;
        int32 Overall = -1; // informational: restore goes through Levels
        float FrameRate = 0.0f;
        bool bVSync = false;
        EWindowMode::Type Mode = EWindowMode::Windowed;
        float ResolutionScale = 1.0f;
        FString CurrentCulture;
        Scalability::FQualityLevels Levels; // per-group levels, so a Custom (-1) overall level survives too

        FSettingsSnapshot()
        {
            CurrentCulture = FInternationalization::Get().GetCurrentCulture()->GetName();
            Settings = UPSLGameUserSettings::Get();
            if (!Settings)
            {
                return;
            }
            Sensitivity = Settings->MouseSensitivity;
            bInvertY = Settings->bInvertY;
            Master = Settings->MasterVolume;
            Music = Settings->MusicVolume;
            Sfx = Settings->SfxVolume;
            Culture = Settings->CultureName;
            Overall = Settings->GetOverallScalabilityLevel();
            FrameRate = Settings->GetFrameRateLimit();
            bVSync = Settings->IsVSyncEnabled();
            Mode = Settings->GetFullscreenMode();
            ResolutionScale = Settings->GetResolutionScaleNormalized();
            Levels = Scalability::GetQualityLevels(); // live cvar state
        }

        ~FSettingsSnapshot()
        {
            if (Settings)
            {
                Settings->MouseSensitivity = Sensitivity;
                Settings->bInvertY = bInvertY;
                Settings->MasterVolume = Master;
                Settings->MusicVolume = Music;
                Settings->SfxVolume = Sfx;
                Settings->CultureName = Culture;
                Settings->ScalabilityQuality = Levels;
                Scalability::SetQualityLevels(Levels, true);
                Settings->SetResolutionScaleNormalized(ResolutionScale);
                Settings->SetFrameRateLimit(FrameRate);
                Settings->SetVSyncEnabled(bVSync);
                Settings->SetFullscreenMode(Mode);
                Settings->ApplySettings(false);
                Settings->SaveSettings();
            }
            FInternationalization::Get().SetCurrentCulture(CurrentCulture);
        }
    };

    UPSLGameUserSettings* RequireSettings(FAutomationTestBase& Test)
    {
        UPSLGameUserSettings* Settings = UPSLGameUserSettings::Get();
        if (!Settings)
        {
            Test.AddError(TEXT("UPSLGameUserSettings::Get() is null: GameUserSettingsClassName is not the PSL class"));
        }
        return Settings;
    }

    void SetNonDefaults(UPSLGameUserSettings& Settings)
    {
        Settings.SetMouseSensitivity(2.25f);
        Settings.bInvertY = true;
        Settings.SetMasterVolume(0.35f);
        Settings.SetMusicVolume(0.45f);
        Settings.SetSfxVolume(0.55f);
        Settings.CultureName = TEXT("vi");
        Settings.SetFrameRateChoice(2);
        Settings.SetVSyncEnabled(true);
    }

    void ExpectNonDefaults(FAutomationTestBase& Test, const UPSLGameUserSettings& Loaded, const TCHAR* What)
    {
        const FString P(What);
        Test.TestEqual(P + TEXT(" sensitivity"), Loaded.MouseSensitivity, 2.25f, 1.0e-3f);
        Test.TestTrue(P + TEXT(" invertY"), Loaded.bInvertY);
        Test.TestEqual(P + TEXT(" master"), Loaded.MasterVolume, 0.35f, 1.0e-3f);
        Test.TestEqual(P + TEXT(" music"), Loaded.MusicVolume, 0.45f, 1.0e-3f);
        Test.TestEqual(P + TEXT(" sfx"), Loaded.SfxVolume, 0.55f, 1.0e-3f);
        Test.TestEqual(P + TEXT(" culture"), Loaded.CultureName, FString(TEXT("vi")));
        Test.TestEqual(P + TEXT(" frame choice"), Loaded.GetFrameRateChoice(), 2);
        Test.TestTrue(P + TEXT(" vsync"), Loaded.IsVSyncEnabled());
    }
}

// Save, then a brand-new settings object reads the ini back.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLSettingsRoundTripTest, "PSL.Settings.RoundTrip", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLSettingsRoundTripTest::RunTest(const FString& Parameters)
{
    UPSLGameUserSettings* Settings = RequireSettings(*this);
    if (!Settings)
    {
        return false;
    }
    FSettingsSnapshot Snapshot;
    SetNonDefaults(*Settings);
    Settings->ApplySettings(false);
    Settings->SaveSettings();

    UPSLGameUserSettings* Fresh = NewObject<UPSLGameUserSettings>(GetTransientPackage());
    Fresh->LoadSettings(true);
    ExpectNonDefaults(*this, *Fresh, TEXT("Fresh instance"));

    // Mutating the live object and force-reloading it restores the saved values too.
    Settings->SetMouseSensitivity(0.5f);
    Settings->SetMasterVolume(0.0f);
    Settings->bInvertY = false;
    Settings->LoadSettings(true);
    ExpectNonDefaults(*this, *Settings, TEXT("Reloaded live object"));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLSettingsPersistTest, "PSL.Settings.Persist", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLSettingsPersistTest::RunTest(const FString& Parameters)
{
    UPSLGameUserSettings* Settings = RequireSettings(*this);
    if (!Settings)
    {
        return false;
    }
    FSettingsSnapshot Snapshot;
    SetNonDefaults(*Settings);
    Settings->SaveSettings(); // no Apply: SaveSettings alone must write every field.

    UPSLGameUserSettings* Fresh = NewObject<UPSLGameUserSettings>(GetTransientPackage());
    Fresh->LoadSettings(true);
    ExpectNonDefaults(*this, *Fresh, TEXT("Saved"));

    // Out-of-range values in the ini are clamped on load (LoadSettings).
    Settings->MouseSensitivity = 99.0f;
    Settings->MasterVolume = 7.0f;
    Settings->SaveSettings();
    UPSLGameUserSettings* Clamped = NewObject<UPSLGameUserSettings>(GetTransientPackage());
    Clamped->LoadSettings(true);
    TestEqual(TEXT("Loaded sensitivity is clamped"), Clamped->MouseSensitivity, UPSLGameUserSettings::MaxSensitivity, 1.0e-3f);
    TestEqual(TEXT("Loaded volume is clamped"), Clamped->MasterVolume, 1.0f, 1.0e-3f);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLSettingsGraphicsAppliesTest, "PSL.Settings.GraphicsApplies", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLSettingsGraphicsAppliesTest::RunTest(const FString& Parameters)
{
    UPSLGameUserSettings* Settings = RequireSettings(*this);
    if (!Settings)
    {
        return false;
    }
    FSettingsSnapshot Snapshot;
    IConsoleVariable* Shadow = IConsoleManager::Get().FindConsoleVariable(TEXT("sg.ShadowQuality"));
    IConsoleVariable* Screen = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"));
    if (!TestNotNull(TEXT("sg.ShadowQuality exists"), Shadow) || !TestNotNull(TEXT("r.ScreenPercentage exists"), Screen))
    {
        return false;
    }
    // Expected: BaseScalability.ini [ShadowQuality@N] level N; PerfIndexValues_ResolutionQuality="50 71 87 100 100" (line 37)
    // maps level 0 -> 50 and 3 -> 100 (Scalability.cpp:1047 SetFromSingleQualityLevel, :532 SetResolutionQualityLevel -> r.ScreenPercentage).
    Settings->SetOverallScalabilityLevel(0);
    Settings->ApplySettings(false);
    TestEqual(TEXT("Low: sg.ShadowQuality"), Shadow->GetInt(), 0);
    TestEqual(TEXT("Low: r.ScreenPercentage"), Screen->GetFloat(), 50.0f, 0.01f);
    TestEqual(TEXT("Low: overall level"), Settings->GetOverallScalabilityLevel(), 0);

    Settings->SetOverallScalabilityLevel(3);
    Settings->ApplySettings(false);
    TestEqual(TEXT("Epic: sg.ShadowQuality"), Shadow->GetInt(), 3);
    TestEqual(TEXT("Epic: r.ScreenPercentage"), Screen->GetFloat(), 100.0f, 0.01f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLSettingsAudioAppliesTest, "PSL.Settings.AudioApplies", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLSettingsAudioAppliesTest::RunTest(const FString& Parameters)
{
    UPSLGameUserSettings* Settings = RequireSettings(*this);
    if (!Settings)
    {
        return false;
    }
    FSettingsSnapshot Snapshot;
    const TSoftObjectPtr<USoundClass> OldMaster = Settings->SC_Master, OldMusic = Settings->SC_Music, OldSfx = Settings->SC_SFX;
    const TSoftObjectPtr<USoundMix> OldMix = Settings->SM_PSL_Volume;

    USoundClass* Master = NewObject<USoundClass>(GetTransientPackage());
    USoundClass* Music = NewObject<USoundClass>(GetTransientPackage());
    USoundClass* Sfx = NewObject<USoundClass>(GetTransientPackage());
    USoundMix* Mix = NewObject<USoundMix>(GetTransientPackage());
    Settings->SC_Master = Master;
    Settings->SC_Music = Music;
    Settings->SC_SFX = Sfx;
    Settings->SM_PSL_Volume = Mix;

    Settings->SetMasterVolume(0.3f);
    Settings->SetMusicVolume(0.6f);
    Settings->SetSfxVolume(0.9f);
    Settings->ApplyAudio(nullptr); // no world: nothing is pushed to a device, which is the -nullrhi-safe path
    TestEqual(TEXT("Applied master"), Settings->GetAppliedMasterOverride(), 0.3f, 1.0e-4f);
    TestEqual(TEXT("Applied music"), Settings->GetAppliedMusicOverride(), 0.6f, 1.0e-4f);
    TestEqual(TEXT("Applied sfx"), Settings->GetAppliedSfxOverride(), 0.9f, 1.0e-4f);
    // Per-class values are recorded before the world check (the device override map is private, AudioDevice.cpp:3839).
    TestEqual(TEXT("Master class override"), Settings->GetAppliedClassOverride(Master), 0.3f, 1.0e-4f);
    TestEqual(TEXT("Music class override"), Settings->GetAppliedClassOverride(Music), 0.6f, 1.0e-4f);
    TestEqual(TEXT("Sfx class override"), Settings->GetAppliedClassOverride(Sfx), 0.9f, 1.0e-4f);
    USoundClass* Other = NewObject<USoundClass>(GetTransientPackage());
    TestEqual(TEXT("Non-injected class is not recorded"), Settings->GetAppliedClassOverride(Other), -1.0f, 1.0e-6f);
    TestEqual(TEXT("SoundMix asset is not mutated"), Mix->SoundClassEffects.Num(), 0);

    Settings->SC_Master = OldMaster;
    Settings->SC_Music = OldMusic;
    Settings->SC_SFX = OldSfx;
    Settings->SM_PSL_Volume = OldMix;
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLSettingsSensitivityTest, "PSL.Settings.Sensitivity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLSettingsSensitivityTest::RunTest(const FString& Parameters)
{
    const FVector2D Plain = APSLCharacter::ScaleLookInput(FVector2D(2.0, 4.0), 1.5f, false);
    TestEqual(TEXT("X scaled"), Plain.X, 3.0, 1.0e-6);
    TestEqual(TEXT("Y scaled"), Plain.Y, 6.0, 1.0e-6);
    const FVector2D Inverted = APSLCharacter::ScaleLookInput(FVector2D(2.0, 4.0), 0.5f, true);
    TestEqual(TEXT("Inverted X unchanged sign"), Inverted.X, 1.0, 1.0e-6);
    TestEqual(TEXT("Inverted Y negated"), Inverted.Y, -2.0, 1.0e-6);

    // Setter clamps feed the same transform.
    UPSLGameUserSettings* Settings = RequireSettings(*this);
    if (!Settings)
    {
        return false;
    }
    FSettingsSnapshot Snapshot;
    Settings->SetMouseSensitivity(100.0f);
    TestEqual(TEXT("Sensitivity clamps high"), Settings->MouseSensitivity, UPSLGameUserSettings::MaxSensitivity, 1.0e-6f);
    Settings->SetMouseSensitivity(-1.0f);
    TestEqual(TEXT("Sensitivity clamps low"), Settings->MouseSensitivity, UPSLGameUserSettings::MinSensitivity, 1.0e-6f);
    Settings->SetFrameRateChoice(0);
    TestEqual(TEXT("Choice 0 = 30"), Settings->GetFrameRateLimit(), 30.0f);
    Settings->SetFrameRateChoice(3);
    TestEqual(TEXT("Choice 3 = unlimited"), Settings->GetFrameRateLimit(), 0.0f);
    return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLSettingsWidgetTest, "PSL.UI.SettingsWidget", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLSettingsWidgetTest::RunTest(const FString& Parameters)
{
    UPSLGameUserSettings* Settings = RequireSettings(*this);
    if (!Settings)
    {
        return false;
    }
    FSettingsSnapshot Snapshot;
    Settings->SetMasterVolume(0.7f);
    Settings->SaveSettings();

    // Same construction as PSLUiTests: NewObject + Initialize, no PIE/world.
    UPSLSettingsWidget* Widget = NewObject<UPSLSettingsWidget>(GetTransientPackage(), UPSLSettingsWidget::StaticClass());
    if (!TestNotNull(TEXT("Widget created"), Widget))
    {
        return false;
    }
    Widget->Initialize();
    if (!TestNotNull(TEXT("Widget tree built"), Widget->WidgetTree.Get()) || !Widget->WidgetTree->RootWidget)
    {
        return false;
    }
    TestNotNull(TEXT("Apply"), Widget->GetApplyButton());
    TestNotNull(TEXT("Revert"), Widget->GetRevertButton());
    TestNotNull(TEXT("Back"), Widget->GetBackButton());
    if (!TestNotNull(TEXT("Master slider"), Widget->GetMasterSlider()))
    {
        return false;
    }
    TestTrue(TEXT("Default tab is Graphics"), Widget->GetActiveTab() == UPSLSettingsWidget::ETab::Graphics);
    TestFalse(TEXT("Clean after construction"), Widget->HasUnappliedChanges());

    // Tabs: SelectTab and the tab buttons' OnClicked (the PSLUiTests mechanism; UButton click needs no Slate input).
    const UPSLSettingsWidget::ETab Tabs[] = {UPSLSettingsWidget::ETab::Graphics, UPSLSettingsWidget::ETab::Audio, UPSLSettingsWidget::ETab::Controls, UPSLSettingsWidget::ETab::Language};
    for (int32 Index = 3; Index >= 0; --Index)
    {
        Widget->SelectTab(Tabs[(Index + 1) % 4]);
        if (TestNotNull(FString::Printf(TEXT("Tab button %d"), Index), Widget->GetTabButton(Tabs[Index])))
        {
            Widget->GetTabButton(Tabs[Index])->OnClicked.Broadcast();
            TestTrue(FString::Printf(TEXT("Clicking tab %d selects it"), Index), Widget->GetActiveTab() == Tabs[Index]);
        }
        Widget->SelectTab(Tabs[Index]);
        TestTrue(FString::Printf(TEXT("SelectTab %d"), Index), Widget->GetActiveTab() == Tabs[Index]);
    }

    // Slider change -> OnValueChanged -> dirty + live preview.
    Widget->GetMasterSlider()->SetValue(40.0f);
    TestTrue(TEXT("Dirty after slider change"), Widget->HasUnappliedChanges());
    TestEqual(TEXT("Live preview reached settings"), Settings->MasterVolume, 0.4f, 1.0e-3f);
    Widget->Revert();
    TestFalse(TEXT("Clean after Revert"), Widget->HasUnappliedChanges());
    TestEqual(TEXT("Revert restores saved volume"), Settings->MasterVolume, 0.7f, 1.0e-3f);
    TestEqual(TEXT("Revert restores slider"), Widget->GetMasterSlider()->GetValue(), 70.0f, 0.01f);

    // Language: Vietnamese click previews the culture live and marks dirty; Revert restores the saved culture.
    Settings->CultureName = TEXT("en");
    Settings->ApplyCulture();
    Widget->LoadFromSettings();
    if (TestNotNull(TEXT("Vietnamese button"), Widget->GetVietnameseButton()))
    {
        Widget->GetVietnameseButton()->OnClicked.Broadcast();
        TestTrue(TEXT("Dirty after language change"), Widget->HasUnappliedChanges());
        TestTrue(TEXT("Current culture is vi"), FInternationalization::Get().GetCurrentCulture()->GetName().StartsWith(TEXT("vi")));
        Widget->Revert();
        TestFalse(TEXT("Clean after language Revert"), Widget->HasUnappliedChanges());
        TestTrue(TEXT("Revert restores culture en"), FInternationalization::Get().GetCurrentCulture()->GetName().StartsWith(TEXT("en")));
    }

    // ResolutionQuality 0 is the engine default (display-based screen percentage): an untouched slider keeps 0 on Apply; a moved slider writes its value.
    Settings->SetResolutionScaleValueEx(0.0f);
    Widget->LoadFromSettings();
    if (TestNotNull(TEXT("Resolution scale slider"), Widget->GetResolutionScaleSlider()))
    {
        TestEqual(TEXT("Default scale shows 100%"), Widget->GetResolutionScaleSlider()->GetValue(), 100.0f, 0.01f);
        TestTrue(TEXT("Apply with untouched slider succeeds"), Widget->Apply());
        float Normalized, Scale, MinScale, MaxScale;
        Settings->GetResolutionScaleInformationEx(Normalized, Scale, MinScale, MaxScale);
        TestEqual(TEXT("Untouched slider keeps the engine default (0)"), Scale, 0.0f, 0.01f);
        Widget->GetResolutionScaleSlider()->SetValue(80.0f);
        TestTrue(TEXT("Apply with moved slider succeeds"), Widget->Apply());
        Settings->GetResolutionScaleInformationEx(Normalized, Scale, MinScale, MaxScale);
        TestEqual(TEXT("Moved slider is applied"), Scale, 80.0f, 0.01f);
    }

    // Apply persists.
    Widget->GetMasterSlider()->SetValue(25.0f);
    TestTrue(TEXT("Apply succeeds"), Widget->Apply());
    TestFalse(TEXT("Clean after Apply"), Widget->HasUnappliedChanges());
    UPSLGameUserSettings* Fresh = NewObject<UPSLGameUserSettings>(GetTransientPackage());
    Fresh->LoadSettings(true);
    TestEqual(TEXT("Applied volume persisted"), Fresh->MasterVolume, 0.25f, 1.0e-3f);

    // Back through the Pause screen (public API, no PIE): its OnClosed handler collapses Settings, which is how closing is observed.
    UPSLPauseWidget* Pause = NewObject<UPSLPauseWidget>(GetTransientPackage(), UPSLPauseWidget::StaticClass());
    Pause->Initialize();
    UPSLSettingsWidget* Nested = Pause->GetSettingsWidget();
    if (!TestNotNull(TEXT("Pause nests Settings"), Nested) || !TestNotNull(TEXT("Pause Settings button"), Pause->GetSettingsButton()))
    {
        return false;
    }
    Pause->GetSettingsButton()->OnClicked.Broadcast();
    TestTrue(TEXT("Pause -> Settings opens"), Pause->IsSettingsOpen());
    Nested->GetMasterSlider()->SetValue(90.0f);
    TestTrue(TEXT("Nested dirty"), Nested->HasUnappliedChanges());
    Pause->HandleCancel(); // same path as P / Escape
    TestTrue(TEXT("First Back arms the confirmation"), Nested->IsConfirmingDiscard());
    TestTrue(TEXT("First Back does not close"), Pause->IsSettingsOpen());
    Pause->HandleCancel();
    TestFalse(TEXT("Second Back closes Settings"), Pause->IsSettingsOpen());
    TestTrue(TEXT("Nested Settings is Collapsed"), Nested->GetVisibility() == ESlateVisibility::Collapsed);
    TestTrue(TEXT("Pause screen itself stays visible (no public menu-panel accessor)"), Pause->GetVisibility() != ESlateVisibility::Collapsed);
    TestFalse(TEXT("Clean after discard"), Nested->HasUnappliedChanges());
    TestEqual(TEXT("Discard restores saved volume"), Settings->MasterVolume, Fresh->MasterVolume, 1.0e-3f);
    // Direct RequestBack on a clean standalone widget closes without arming the prompt.
    Widget->RequestBack();
    TestFalse(TEXT("Clean Back does not arm the prompt"), Widget->IsConfirmingDiscard());
    return true;
}

#endif
