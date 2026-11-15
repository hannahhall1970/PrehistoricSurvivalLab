#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Blueprint/WidgetTree.h"
#include "Components/ProgressBar.h"
#include "Engine/Font.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Components/TextBlock.h"
#include "PSLDinoSpecies.h"
#include "PSLGameOverWidget.h"
#include "PSLMainMenuWidget.h"
#include "PSLPauseWidget.h"
#include "PSLSettingsWidget.h"
#include "PSLSpeciesSelectWidget.h"
#include "PSLUiButton.h"
#include "PSLUiStyle.h"

namespace
{
    template <typename T>
    T* MakeScreen(FAutomationTestBase& Test, const TCHAR* What)
    {
        T* Widget = NewObject<T>(GetTransientPackage(), T::StaticClass());
        if (!Test.TestNotNull(FString::Printf(TEXT("%s created"), What), Widget))
        {
            return nullptr;
        }
        Widget->Initialize();
        if (!Test.TestNotNull(FString::Printf(TEXT("%s tree built"), What), Widget->WidgetTree.Get()) || !Widget->WidgetTree->RootWidget)
        {
            return nullptr;
        }
        return Widget;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLUiStyleTest, "PSL.UI.Style", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLUiStyleTest::RunTest(const FString& Parameters)
{
    TestEqual(TEXT("Accent #E3A63B"), FPSLUiStyle::Accent().ToFColor(true), FColor(0xE3, 0xA6, 0x3B, 0xFF));
    TestEqual(TEXT("Text #EDE3CC"), FPSLUiStyle::Text().ToFColor(true), FColor(0xED, 0xE3, 0xCC, 0xFF));
    TestEqual(TEXT("Danger #B5462E"), FPSLUiStyle::Danger().ToFColor(true), FColor(0xB5, 0x46, 0x2E, 0xFF));
    TestEqual(TEXT("Disabled #6E6A5E"), FPSLUiStyle::Disabled().ToFColor(true), FColor(0x6E, 0x6A, 0x5E, 0xFF));
    const FColor Panel = FPSLUiStyle::Panel().ToFColor(true);
    TestEqual(TEXT("Panel #14201A (RGB)"), FColor(Panel.R, Panel.G, Panel.B, 0xFF), FColor(0x14, 0x20, 0x1A, 0xFF));
    TestEqual(TEXT("Panel is translucent"), FPSLUiStyle::Panel().A, 0.86f, 1.0e-4f);

    // The project font resolves, or Font() falls back to the engine default; either way a usable font comes back.
    const FSlateFontInfo Regular = FPSLUiStyle::Font(FPSLUiStyle::BodySize, EPSLFontWeight::Regular);
    const FSlateFontInfo SemiBold = FPSLUiStyle::Font(FPSLUiStyle::BodySize, EPSLFontWeight::SemiBold);
    const FSlateFontInfo Bold = FPSLUiStyle::Font(FPSLUiStyle::BodySize, EPSLFontWeight::Bold);
    TestEqual(TEXT("Font size is honoured"), Regular.Size, static_cast<float>(FPSLUiStyle::BodySize));
    TestEqual(TEXT("SemiBold font size is honoured"), SemiBold.Size, static_cast<float>(FPSLUiStyle::BodySize));
    TestEqual(TEXT("Bold font size is honoured"), Bold.Size, static_cast<float>(FPSLUiStyle::BodySize));
    TestEqual(TEXT("Regular typeface name"), FPSLUiStyle::TypefaceName(EPSLFontWeight::Regular), FName(TEXT("Regular")));
    TestEqual(TEXT("SemiBold typeface name"), FPSLUiStyle::TypefaceName(EPSLFontWeight::SemiBold), FName(TEXT("SemiBold")));
    TestEqual(TEXT("Bold typeface name"), FPSLUiStyle::TypefaceName(EPSLFontWeight::Bold), FName(TEXT("Bold")));
    if (FPSLUiStyle::HasProjectFont())
    {
        TestTrue(TEXT("Project font used when present"), Regular.FontObject != nullptr);
        const UFont* Project = LoadObject<UFont>(nullptr, FPSLUiStyle::FontPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
        if (TestNotNull(TEXT("F_BeVietnamPro loads as a UFont"), Project))
        {
            for (const EPSLFontWeight Weight : {EPSLFontWeight::Regular, EPSLFontWeight::SemiBold, EPSLFontWeight::Bold})
            {
                const FSlateFontInfo Info = FPSLUiStyle::Font(FPSLUiStyle::BodySize, Weight);
                const FString Name = FPSLUiStyle::TypefaceName(Weight).ToString();
                TestTrue(FString::Printf(TEXT("%s uses F_BeVietnamPro"), *Name), Info.FontObject == Project);
                TestEqual(FString::Printf(TEXT("%s typeface font name"), *Name), Info.TypefaceFontName, FPSLUiStyle::TypefaceName(Weight));
            }
        }
    }
    else
    {
        TestTrue(TEXT("Fallback font used when the project font is missing"), Regular.FontObject == nullptr);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLUiScreensTest, "PSL.UI.Screens", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLUiScreensTest::RunTest(const FString& Parameters)
{
    // Main menu.
    if (UPSLMainMenuWidget* Menu = MakeScreen<UPSLMainMenuWidget>(*this, TEXT("Main menu")))
    {
        TestNotNull(TEXT("Continue"), Menu->GetContinueButton());
        TestNotNull(TEXT("New Game"), Menu->GetNewGameButton());
        TestNotNull(TEXT("Settings"), Menu->GetSettingsButton());
        TestNotNull(TEXT("Quit"), Menu->GetQuitButton());
        if (TestNotNull(TEXT("Version text"), Menu->GetVersionText()))
        {
            TestFalse(TEXT("Version text is filled in"), Menu->GetVersionText()->GetText().IsEmpty());
        }
        TestTrue(TEXT("Settings is visible (M2b2)"), Menu->GetSettingsButton()->GetVisibility() == ESlateVisibility::Visible);
        if (TestNotNull(TEXT("Settings screen is nested"), Menu->GetSettingsWidget()))
        {
            TestTrue(TEXT("Settings screen starts hidden"), Menu->GetSettingsWidget()->GetVisibility() == ESlateVisibility::Collapsed);
            Menu->GetSettingsButton()->OnClicked.Broadcast();
            TestTrue(TEXT("Settings button opens the settings screen"), Menu->GetSettingsWidget()->GetVisibility() == ESlateVisibility::Visible);
            Menu->GetSettingsWidget()->RequestBack();
            TestTrue(TEXT("Settings Back returns to the main menu"), Menu->GetSettingsWidget()->GetVisibility() == ESlateVisibility::Collapsed);
        }
        if (TestNotNull(TEXT("Species screen is nested"), Menu->GetSpeciesWidget()))
        {
            TestTrue(TEXT("Species screen starts hidden"), Menu->GetSpeciesWidget()->GetVisibility() == ESlateVisibility::Collapsed);
        }
    }

    // Species select.
    if (UPSLSpeciesSelectWidget* Select = MakeScreen<UPSLSpeciesSelectWidget>(*this, TEXT("Species select")))
    {
        TestNotNull(TEXT("Confirm"), Select->GetConfirmButton());
        TestNotNull(TEXT("Back"), Select->GetBackButton());
        TestNotNull(TEXT("Prev"), Select->GetPrevButton());
        TestNotNull(TEXT("Next"), Select->GetNextButton());

        TArray<TArray<float>> Fractions;
        for (int32 Pass = 0; Pass < 2; ++Pass)
        {
            TArray<float>& Row = Fractions.AddDefaulted_GetRef();
            for (int32 Index = 0; Index < UPSLSpeciesSelectWidget::StatCount; ++Index)
            {
                const UProgressBar* Bar = Select->GetStatBar(Index);
                if (!TestNotNull(FString::Printf(TEXT("Stat bar %d"), Index), Bar))
                {
                    return false;
                }
                const float Value = Bar->GetPercent();
                TestTrue(FString::Printf(TEXT("Stat bar %d in [0,1] (%.3f)"), Index, Value), Value >= 0.0f && Value <= 1.0f);
                Row.Add(Value);
            }
            if (Pass == 0)
            {
                TestTrue(TEXT("Starts on the T-Rex"), Select->GetCurrentSpeciesId() == UPSLDinoSpecies::TRexId);
                const FString FirstName = Select->GetNameText()->GetText().ToString();
                Select->SelectNext();
                TestTrue(TEXT("Next selects the Parasaurolophus"), Select->GetCurrentSpeciesId() == UPSLDinoSpecies::ParaId);
                TestNotEqual(TEXT("Name changes with the selection"), Select->GetNameText()->GetText().ToString(), FirstName);
            }
        }
        // Normalized across the two species: every stat is 1.0 for at least one of them, and the two differ somewhere.
        bool bAnyDifference = false;
        for (int32 Index = 0; Index < UPSLSpeciesSelectWidget::StatCount; ++Index)
        {
            TestEqual(FString::Printf(TEXT("Stat %d: the better species fills the bar"), Index), FMath::Max(Fractions[0][Index], Fractions[1][Index]), 1.0f, 1.0e-4f);
            bAnyDifference |= !FMath::IsNearlyEqual(Fractions[0][Index], Fractions[1][Index]);
        }
        TestTrue(TEXT("The species differ on at least one stat"), bAnyDifference);

        Select->SelectNext();
        TestTrue(TEXT("Next wraps to the T-Rex"), Select->GetCurrentSpeciesId() == UPSLDinoSpecies::TRexId);
        Select->SelectPrevious();
        TestTrue(TEXT("Previous wraps to the Parasaurolophus"), Select->GetCurrentSpeciesId() == UPSLDinoSpecies::ParaId);
    }

    // Pause.
    if (UPSLPauseWidget* Pause = MakeScreen<UPSLPauseWidget>(*this, TEXT("Pause")))
    {
        TestNotNull(TEXT("Resume"), Pause->GetResumeButton());
        TestNotNull(TEXT("Save"), Pause->GetSaveButton());
        TestNotNull(TEXT("Main Menu"), Pause->GetMainMenuButton());
        TestNotNull(TEXT("Quit"), Pause->GetQuitButton());
        if (TestNotNull(TEXT("Settings"), Pause->GetSettingsButton()))
        {
            TestTrue(TEXT("Settings is visible (M2b2)"), Pause->GetSettingsButton()->GetVisibility() == ESlateVisibility::Visible);
            if (TestNotNull(TEXT("Pause settings screen is nested"), Pause->GetSettingsWidget()))
            {
                TestFalse(TEXT("Pause settings starts closed"), Pause->IsSettingsOpen());
                Pause->GetSettingsButton()->OnClicked.Broadcast();
                TestTrue(TEXT("Pause Settings button opens the settings screen"), Pause->IsSettingsOpen());
                Pause->HandleCancel();
                TestFalse(TEXT("Cancel inside Settings closes Settings, not the pause menu"), Pause->IsSettingsOpen());
            }
        }
    }

    // Game over.
    if (UPSLGameOverWidget* GameOver = MakeScreen<UPSLGameOverWidget>(*this, TEXT("Game over")))
    {
        TestNotNull(TEXT("Retry"), GameOver->GetRetryButton());
        TestNotNull(TEXT("Continue"), GameOver->GetContinueButton());
        TestNotNull(TEXT("Main Menu"), GameOver->GetMainMenuButton());
        GameOver->SetStarved(true);
        TestEqual(TEXT("Starvation title"), GameOver->GetTitleText()->GetText().ToString(), FString(TEXT("YOU STARVED")));
        GameOver->SetStarved(false);
        TestEqual(TEXT("Generic title"), GameOver->GetTitleText()->GetText().ToString(), FString(TEXT("YOU DIED")));
    }
    TestEqual(TEXT("Survival time 125 s"), UPSLGameOverWidget::FormatSurvivalTime(125.9f), FString(TEXT("02:05")));
    TestEqual(TEXT("Survival time clamps negatives"), UPSLGameOverWidget::FormatSurvivalTime(-3.0f), FString(TEXT("00:00")));
    TestEqual(TEXT("Survival time past an hour keeps minutes"), UPSLGameOverWidget::FormatSurvivalTime(3600.0f), FString(TEXT("60:00")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLUiRefreshTextsUnbuiltTest, "PSL.UI.RefreshTextsUnbuilt", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLUiRefreshTextsUnbuiltTest::RunTest(const FString& Parameters)
{
    // H-F2: RefreshTexts (also reached from OnCultureChanged) must be a no-op before Initialize builds the tree.
    UPSLMainMenuWidget* Menu = NewObject<UPSLMainMenuWidget>(GetTransientPackage());
    UPSLPauseWidget* Pause = NewObject<UPSLPauseWidget>(GetTransientPackage());
    if (!TestNotNull(TEXT("Main menu created"), Menu) || !TestNotNull(TEXT("Pause created"), Pause))
    {
        return false;
    }
    TestNull(TEXT("Main menu tree not built"), Menu->GetNewGameButton());
    TestNull(TEXT("Pause tree not built"), Pause->GetResumeButton());

    const FString OldCulture = FInternationalization::Get().GetCurrentCulture()->GetName();
    for (const TCHAR* Culture : {TEXT("vi"), TEXT("en")})
    {
        FInternationalization::Get().SetCurrentCulture(Culture);
        Menu->RefreshTexts();
        Pause->RefreshTexts();
    }
    FInternationalization::Get().SetCurrentCulture(OldCulture);

    // Once built, RefreshTexts must fill the labels again (a sentinel proves it ran).
    const FText Sentinel = FText::AsCultureInvariant(TEXT("__psl_unbuilt__"));
    Menu->Initialize();
    Pause->Initialize();
    if (!TestNotNull(TEXT("Main menu tree built"), Menu->GetNewGameButton()) || !TestNotNull(TEXT("Pause tree built"), Pause->GetResumeButton()))
    {
        return false;
    }
    Menu->GetNewGameButton()->GetLabel()->SetText(Sentinel);
    Pause->GetResumeButton()->GetLabel()->SetText(Sentinel);
    Pause->GetWarningText()->SetText(Sentinel);
    Menu->RefreshTexts();
    Pause->RefreshTexts();
    TestFalse(TEXT("Main menu New Game label refreshed"), Menu->GetNewGameButton()->GetLabel()->GetText().ToString() == Sentinel.ToString());
    TestFalse(TEXT("Main menu version filled"), Menu->GetVersionText()->GetText().IsEmpty());
    TestFalse(TEXT("Pause Resume label refreshed"), Pause->GetResumeButton()->GetLabel()->GetText().ToString() == Sentinel.ToString());
    TestFalse(TEXT("Pause warning refreshed"), Pause->GetWarningText()->GetText().ToString() == Sentinel.ToString());
    return true;
}

#endif
