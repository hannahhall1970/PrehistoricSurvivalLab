#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Blueprint/UserWidget.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "PSLHUD.h"
#include "PSLHudWidget.h"
#include "PSLSaveSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Fonts/SlateFontInfo.h"
#include "Components/WidgetSwitcher.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "PSLGameUserSettings.h"
#include "PSLMainMenuWidget.h"
#include "PSLMenuGameMode.h"
#include "PSLSettingsWidget.h"
#include "PSLPieTestSupport.h"
#include "PSLUiButton.h"
#include "HAL/IConsoleManager.h"
#include "PSLUiStyle.h"

// PSL.Render.FontEvidence: four real-renderer screenshots (A en menu, B vi menu, C vi settings, D vi HUD) plus
// machine-readable PSL_RENDER lines and widget-tree assertions. Under -nullrhi screenshots are skipped, the rest runs.
namespace
{
using namespace PSLPie;

constexpr const TCHAR* RenderEvTestName = TEXT("Render_FontEvidence");
constexpr const TCHAR* RenderEvMenuMap = TEXT("L_MainMenu");
// Never PSL_Slot0: the developer's own save must survive.
constexpr const TCHAR* RenderEvSlot = TEXT("PSL_Test_RenderEvidence");

// Culture + GameUserSettings ini snapshot taken before PIE starts; Restore() is idempotent and also runs from the
// destructor, because a lost PIE context skips step bodies.
struct FRenderEvState
{
    TWeakObjectPtr<UPSLGameUserSettings> Settings;
    FString OldName;
    FString OldCulture;
    bool bOldForceLoc = false;
    FString IniPath;
    TArray<uint8> OldIni;
    bool bHadIni = false;
    bool bSnapshot = false;
    int32 Frames = 0;
    bool bSlotSwapped = false;

    void Snapshot()
    {
        UPSLGameUserSettings* S = UPSLGameUserSettings::Get();
        if (!S)
        {
            return;
        }
        Settings = S;
        OldName = S->CultureName;
        OldCulture = FInternationalization::Get().GetCurrentCulture()->GetName();
        // SaveSettings flushes the whole live object; put the exact on-disk bytes back (pattern: PSLLocalizationTests.cpp CultureApplies).
        IniPath = GGameUserSettingsIni;
        bHadIni = FFileHelper::LoadFileToArray(OldIni, *IniPath, FILEREAD_Silent);
        // PIE turns game localization preview off (native culture), so vi locres load only with this CVar (as PSL.UI.Localization).
        if (IConsoleVariable* Force = IConsoleManager::Get().FindConsoleVariable(TEXT("Localization.ForceLoadGameLocalizationInEditor")))
        {
            bOldForceLoc = Force->GetBool();
            Force->Set(TEXT("1"), ECVF_SetByCode);
        }
        bSnapshot = true;
    }

    // Points the save subsystem at a test slot (never PSL_Slot0) so the valley's autosave cannot touch the developer's save.
    void SwapSaveSlot(UWorld* World)
    {
        UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
        UPSLSaveSubsystem* Subsystem = GameInstance ? GameInstance->GetSubsystem<UPSLSaveSubsystem>() : nullptr;
        if (Subsystem && !bSlotSwapped)
        {
            bSlotSwapped = true;
            Subsystem->SlotName = RenderEvSlot;
            Subsystem->DeleteSave();
        }
    }

    // The test slot is deleted only after PIE ends (DeleteTestSlot); the subsystem dies with the PIE GameInstance,
    // so its SlotName is never pointed back at PSL_Slot0 while the valley can still save.
    void DeleteTestSlot()
    {
        if (bSlotSwapped)
        {
            bSlotSwapped = false;
            UGameplayStatics::DeleteGameInSlot(RenderEvSlot, 0);
        }
    }

    void Restore()
    {
        if (!bSnapshot)
        {
            return;
        }
        bSnapshot = false;
        if (UPSLGameUserSettings* S = Settings.Get())
        {
            S->CultureName = OldName;
            S->SaveSettings();
            S->ApplyCulture();
        }
        FInternationalization::Get().SetCurrentCulture(OldCulture);
        if (IConsoleVariable* Force = IConsoleManager::Get().FindConsoleVariable(TEXT("Localization.ForceLoadGameLocalizationInEditor")))
        {
            Force->Set(bOldForceLoc ? TEXT("1") : TEXT("0"), ECVF_SetByCode);
        }
        if (bHadIni)
        {
            FFileHelper::SaveArrayToFile(OldIni, *IniPath);
        }
        else
        {
            IFileManager::Get().Delete(*IniPath);
        }
    }

    ~FRenderEvState()
    {
        Restore();
        DeleteTestSlot();
    }
};

// The L_MainMenu PIE world's menu widget (the menu map has no pawn, so FPSLPieCtx cannot carry it). Never sets Ctx.bReady.
UPSLMainMenuWidget* RenderEvFindMenu()
{
    if (!GEditor)
    {
        return nullptr;
    }
    for (const FWorldContext& WorldContext : GEditor->GetWorldContexts())
    {
        UWorld* World = WorldContext.World();
        if (WorldContext.WorldType != EWorldType::PIE || !World || !World->GetMapName().Contains(RenderEvMenuMap))
        {
            continue;
        }
        const APSLMenuGameMode* GameMode = World->GetAuthGameMode<APSLMenuGameMode>();
        if (GameMode && GameMode->GetMenuWidget())
        {
            return GameMode->GetMenuWidget();
        }
    }
    return nullptr;
}

FString RenderEvViewportSize()
{
    if (GEditor)
    {
        for (const FWorldContext& WorldContext : GEditor->GetWorldContexts())
        {
            if (WorldContext.WorldType == EWorldType::PIE && WorldContext.GameViewport)
            {
                FVector2D Size = FVector2D::ZeroVector;
                WorldContext.GameViewport->GetViewportSize(Size);
                return FString::Printf(TEXT("%dx%d"), FMath::RoundToInt(Size.X), FMath::RoundToInt(Size.Y));
            }
        }
    }
    return TEXT("0x0");
}

FString RenderEvPngPath(const TCHAR* Id)
{
    // Same path RequestScreenshot builds.
    return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("PIE"), FString(RenderEvTestName) + TEXT("_") + Id + TEXT(".png"));
}

// Delivers a click (real Slate input under a renderer, OnClicked broadcast otherwise); a failed delivery is an error.
void RenderEvClick(FPSLPieCtx& Ctx, UWidget* Widget, const TCHAR* What)
{
    bool bRealInput = false;
    Ctx.Test->TestTrue(FString::Printf(TEXT("Click %s delivered"), What), ClickWidget(Ctx, Widget, &bRealInput));
    if (!bRealInput)
    {
        Ctx.Test->AddInfo(FString::Printf(TEXT("[fallback] click %s used OnClicked broadcast (no renderer/geometry)"), What));
    }
}

// A widget is shown when it and every ancestor (through panels and the owning user widgets) is not Collapsed/Hidden.
bool RenderEvShown(const UWidget* Widget)
{
    while (Widget)
    {
        const ESlateVisibility Visibility = Widget->GetVisibility();
        if (Visibility == ESlateVisibility::Collapsed || Visibility == ESlateVisibility::Hidden)
        {
            return false;
        }
        const UWidget* Parent = Widget->GetParent();
        // A WidgetSwitcher keeps its inactive pages Visible but never draws them.
        if (const UWidgetSwitcher* Switcher = Cast<UWidgetSwitcher>(Parent))
        {
            if (Switcher->GetActiveWidget() != Widget)
            {
                return false;
            }
        }
        Widget = Parent ? Parent : static_cast<const UWidget*>(Widget->GetTypedOuter<UUserWidget>());
    }
    return true;
}

TArray<UTextBlock*> RenderEvVisibleTexts(const UUserWidget& Root)
{
    TArray<UTextBlock*> Result;
    if (Root.WidgetTree)
    {
        // Includes the nested species/settings user widgets.
        Root.WidgetTree->ForEachWidgetAndDescendants([&Result](UWidget* Widget)
        {
            UTextBlock* Text = Cast<UTextBlock>(Widget);
            if (Text && RenderEvShown(Text))
            {
                Result.Add(Text);
            }
        });
    }
    return Result;
}

// Every visible text block uses the project font object (only when the font asset exists).
void RenderEvCheckFont(FAutomationTestBase& Test, const TArray<UTextBlock*>& Texts, const TCHAR* Id)
{
    Test.TestTrue(FString::Printf(TEXT("%s: visible text blocks found"), Id), Texts.Num() > 0);
    if (!FPSLUiStyle::HasProjectFont())
    {
        Test.AddInfo(FString::Printf(TEXT("%s: project font asset absent, font check skipped"), Id));
        return;
    }
    const UObject* Project = LoadObject<UObject>(nullptr, FPSLUiStyle::FontPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
    for (const UTextBlock* Text : Texts)
    {
        Test.TestTrue(FString::Printf(TEXT("%s: '%s' uses the project font"), Id, *Text->GetText().ToString()), Text->GetFont().FontObject == Project);
    }
}

// Some visible text block shows Expected (the widget's own LOCTEXT string, resolved through the same namespace/key/source).
// For vi (bVi) the string must also differ from the English source, so an untranslated fallback fails.
void RenderEvExpect(FAutomationTestBase& Test, const TArray<UTextBlock*>& Texts, const TCHAR* Id, const FText& Expected, const FString& EnglishSource, bool bVi)
{
    const FString Want = Expected.ToString();
    bool bFound = false;
    for (const UTextBlock* Text : Texts)
    {
        bFound |= Text->GetText().ToString() == Want;
    }
    Test.TestTrue(FString::Printf(TEXT("%s: visible text '%s' present"), Id, *Want), bFound);
    if (bVi)
    {
        Test.TestTrue(FString::Printf(TEXT("%s: '%s' is translated (differs from English '%s')"), Id, *Want, *EnglishSource), Want != EnglishSource);
    }
}

#define PSL_RENDER_EXPECT(Test, Texts, Id, Key, Source, bVi) RenderEvExpect(Test, Texts, Id, NSLOCTEXT("PSL", Key, Source), TEXT(Source), bVi)

void RenderEvAssertCulture(FAutomationTestBase& Test, const TCHAR* Id, const TCHAR* Culture)
{
    Test.TestTrue(FString::Printf(TEXT("%s: current culture is %s"), Id, Culture), FInternationalization::Get().GetCurrentCulture()->GetName().StartsWith(Culture));
}

// Screenshot request (skipped without a renderer) plus the machine-readable evidence line.
void RenderEvCapture(FPSLPieCtx& Ctx, const TCHAR* Id)
{
    RequestScreenshot(Ctx, Id);
    Ctx.Test->AddInfo(FString::Printf(TEXT("PSL_RENDER step=%s culture=%s viewport=%s font=%s png=%s captured=%d"),
        Id, *FInternationalization::Get().GetCurrentCulture()->GetName(), *RenderEvViewportSize(),
        FPSLUiStyle::HasProjectFont() ? FPSLUiStyle::FontPath : TEXT("none"), *RenderEvPngPath(Id), FApp::CanEverRender() ? 1 : 0));
}

// QueueAwaitScreenshot (PSLPieTestSupport.h) returns at once without a pawn (IsUsable), so the menu steps wait on the request themselves.
void RenderEvQueueAwait(const TSharedRef<FPSLPieCtx>& Context)
{
    AddPieStep(Context, TEXT("AwaitScreenshot"), 5.0f, [](FPSLPieCtx& Ctx)
    {
        return Ctx.bInvalid || !FApp::CanEverRender() || !FScreenshotRequest::IsScreenshotRequested();
    });
}

// Lets Slate lay the freshly shown screen out and draw before it is asserted/captured.
void RenderEvQueueSettle(const TSharedRef<FPSLPieCtx>& Context, const TSharedRef<FRenderEvState>& State)
{
    AddPieStep(Context, TEXT("Settle"), 3.0f, [State](FPSLPieCtx& Ctx)
    {
        if (Ctx.bInvalid)
        {
            return true;
        }
        if (++State->Frames < 10)
        {
            return false;
        }
        State->Frames = 0;
        return true;
    });
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLRenderFontEvidenceTest, "PSL.Render.FontEvidence", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLRenderFontEvidenceTest::RunTest(const FString& Parameters)
{
    UPSLGameUserSettings* Settings = UPSLGameUserSettings::Get();
    if (!Settings)
    {
        AddError(TEXT("UPSLGameUserSettings::Get() is null: GameUserSettingsClassName is not the PSL class"));
        return false;
    }
    const TSharedRef<FPSLPieCtx> Context = MakeShared<FPSLPieCtx>(this, RenderEvTestName);
    const TSharedRef<FRenderEvState> State = MakeShared<FRenderEvState>();
    Context->RequiredMap = RenderEvMenuMap;

    // A) culture en before PIE starts (the menu GameInstance re-applies CultureName on world init).
    State->Snapshot();
    Settings->CultureName = TEXT("en");
    Settings->ApplyCulture();

    // AutomationOpenMap already starts PIE under GIsEditor: no FStartPIEForAutomationCommand (L_MainMenu has no pawn, so no WaitForPlayer either).
    if (!TestTrue(TEXT("AutomationOpenMap returned true"), AutomationOpenMap(TEXT("/Game/Maps/L_MainMenu"))))
    {
        State->Restore();
        return false;
    }
    AddPieStep(Context, TEXT("WaitForMenu"), 30.0f, [](FPSLPieCtx& Ctx)
    {
        return Ctx.bInvalid || RenderEvFindMenu() != nullptr;
    });
    RenderEvQueueSettle(Context, State);
    AddPieStep(Context, TEXT("A_MainMenu_en"), 4.0f, [](FPSLPieCtx& Ctx)
    {
        const UPSLMainMenuWidget* Menu = Ctx.bInvalid ? nullptr : RenderEvFindMenu();
        if (!Ctx.Test->TestNotNull(TEXT("A: main menu widget"), Menu))
        {
            Ctx.bInvalid = true;
            return true;
        }
        RenderEvAssertCulture(*Ctx.Test, TEXT("A"), TEXT("en"));
        const TArray<UTextBlock*> Texts = RenderEvVisibleTexts(*Menu);
        PSL_RENDER_EXPECT(*Ctx.Test, Texts, TEXT("A"), "Menu_Title", "PREHISTORIC SURVIVAL", false);
        PSL_RENDER_EXPECT(*Ctx.Test, Texts, TEXT("A"), "Menu_Continue", "Continue", false);
        PSL_RENDER_EXPECT(*Ctx.Test, Texts, TEXT("A"), "Menu_NewGame", "New Game", false);
        PSL_RENDER_EXPECT(*Ctx.Test, Texts, TEXT("A"), "Menu_Settings", "Settings", false);
        PSL_RENDER_EXPECT(*Ctx.Test, Texts, TEXT("A"), "Menu_Quit", "Quit", false);
        RenderEvCheckFont(*Ctx.Test, Texts, TEXT("A"));
        RenderEvCapture(Ctx, TEXT("A_MainMenu_en"));
        return true;
    });
    RenderEvQueueAwait(Context);

    // B) live switch to vi through the settings object: the open menu refreshes via FInternationalization::OnCultureChanged.
    AddPieStep(Context, TEXT("SwitchToVi"), 4.0f, [State](FPSLPieCtx& Ctx)
    {
        UPSLGameUserSettings* Live = State->Settings.Get();
        if (Ctx.bInvalid || !Ctx.Test->TestNotNull(TEXT("B: game user settings"), Live))
        {
            Ctx.bInvalid = true;
            return true;
        }
        Live->CultureName = TEXT("vi");
        Live->ApplyCulture();
        return true;
    });
    RenderEvQueueSettle(Context, State);
    AddPieStep(Context, TEXT("B_MainMenu_vi"), 4.0f, [](FPSLPieCtx& Ctx)
    {
        const UPSLMainMenuWidget* Menu = Ctx.bInvalid ? nullptr : RenderEvFindMenu();
        if (!Ctx.Test->TestNotNull(TEXT("B: main menu widget"), Menu))
        {
            Ctx.bInvalid = true;
            return true;
        }
        RenderEvAssertCulture(*Ctx.Test, TEXT("B"), TEXT("vi"));
        const TArray<UTextBlock*> Texts = RenderEvVisibleTexts(*Menu);
        PSL_RENDER_EXPECT(*Ctx.Test, Texts, TEXT("B"), "Menu_Continue", "Continue", true);
        PSL_RENDER_EXPECT(*Ctx.Test, Texts, TEXT("B"), "Menu_NewGame", "New Game", true);
        PSL_RENDER_EXPECT(*Ctx.Test, Texts, TEXT("B"), "Menu_Settings", "Settings", true);
        PSL_RENDER_EXPECT(*Ctx.Test, Texts, TEXT("B"), "Menu_Quit", "Quit", true);
        RenderEvCheckFont(*Ctx.Test, Texts, TEXT("B"));
        RenderEvCapture(Ctx, TEXT("B_MainMenu_vi"));
        return true;
    });
    RenderEvQueueAwait(Context);

    // C) Settings via the menu's Settings button, then the Language tab so the Vietnamese-labelled page is on screen.
    AddPieStep(Context, TEXT("ClickSettings"), 4.0f, [](FPSLPieCtx& Ctx)
    {
        UPSLMainMenuWidget* Menu = Ctx.bInvalid ? nullptr : RenderEvFindMenu();
        if (!Ctx.Test->TestNotNull(TEXT("C: main menu widget"), Menu))
        {
            Ctx.bInvalid = true;
            return true;
        }
        RenderEvClick(Ctx, Menu->GetSettingsButton(), TEXT("Settings"));
        Ctx.Test->TestTrue(TEXT("C: settings screen shown"), RenderEvShown(Menu->GetSettingsWidget()));
        return true;
    });
    RenderEvQueueSettle(Context, State);
    AddPieStep(Context, TEXT("ClickLanguageTab"), 4.0f, [](FPSLPieCtx& Ctx)
    {
        const UPSLMainMenuWidget* Menu = Ctx.bInvalid ? nullptr : RenderEvFindMenu();
        if (!Ctx.Test->TestNotNull(TEXT("C: main menu widget"), Menu))
        {
            Ctx.bInvalid = true;
            return true;
        }
        UPSLSettingsWidget* SettingsScreen = Menu->GetSettingsWidget();
        RenderEvClick(Ctx, SettingsScreen->GetTabButton(UPSLSettingsWidget::ETab::Language), TEXT("LanguageTab"));
        Ctx.Test->TestTrue(TEXT("C: Language tab active"), SettingsScreen->GetActiveTab() == UPSLSettingsWidget::ETab::Language);
        return true;
    });
    RenderEvQueueSettle(Context, State);
    AddPieStep(Context, TEXT("C_Settings_vi"), 4.0f, [](FPSLPieCtx& Ctx)
    {
        const UPSLMainMenuWidget* Menu = Ctx.bInvalid ? nullptr : RenderEvFindMenu();
        if (!Ctx.Test->TestNotNull(TEXT("C: main menu widget"), Menu))
        {
            Ctx.bInvalid = true;
            return true;
        }
        RenderEvAssertCulture(*Ctx.Test, TEXT("C"), TEXT("vi"));
        const TArray<UTextBlock*> Texts = RenderEvVisibleTexts(*Menu);
        PSL_RENDER_EXPECT(*Ctx.Test, Texts, TEXT("C"), "Settings_Title", "SETTINGS", true);
        PSL_RENDER_EXPECT(*Ctx.Test, Texts, TEXT("C"), "Settings_TabLanguage", "Language", true);
        PSL_RENDER_EXPECT(*Ctx.Test, Texts, TEXT("C"), "Settings_Apply", "Apply", true);
        PSL_RENDER_EXPECT(*Ctx.Test, Texts, TEXT("C"), "Settings_Back", "Back", true);
        // Endonym is the same in every culture, so only presence is checked.
        PSL_RENDER_EXPECT(*Ctx.Test, Texts, TEXT("C"), "Settings_Vietnamese", "Tiếng Việt", false);
        RenderEvCheckFont(*Ctx.Test, Texts, TEXT("C"));
        RenderEvCapture(Ctx, TEXT("C_Settings_vi"));
        return true;
    });
    RenderEvQueueAwait(Context);

    // D) Back (Settings is clean, so RequestBack closes without the discard prompt), then New Game -> species select -> Confirm -> HUD in the valley.
    AddPieStep(Context, TEXT("ClickSettingsBack"), 4.0f, [](FPSLPieCtx& Ctx)
    {
        const UPSLMainMenuWidget* Menu = Ctx.bInvalid ? nullptr : RenderEvFindMenu();
        if (!Ctx.Test->TestNotNull(TEXT("D: main menu widget"), Menu))
        {
            Ctx.bInvalid = true;
            return true;
        }
        RenderEvClick(Ctx, Menu->GetSettingsWidget()->GetBackButton(), TEXT("SettingsBack"));
        Ctx.Test->TestFalse(TEXT("D: settings screen closed"), RenderEvShown(Menu->GetSettingsWidget()));
        return true;
    });
    AddPieStep(Context, TEXT("UseTestSlot"), 4.0f, [State](FPSLPieCtx& Ctx)
    {
        // Never PSL_Slot0: the valley autosaves, and the developer's own save must survive.
        const UPSLMainMenuWidget* Menu = Ctx.bInvalid ? nullptr : RenderEvFindMenu();
        if (Menu)
        {
            State->SwapSaveSlot(Menu->GetWorld());
        }
        // Without the test slot the valley would autosave over the developer's slot: stop the flow instead.
        if (!Ctx.bInvalid && !Ctx.Test->TestTrue(TEXT("D: save subsystem uses the test slot"), State->bSlotSwapped))
        {
            Ctx.bInvalid = true;
        }
        return true;
    });
    RenderEvQueueSettle(Context, State);
    AddPieStep(Context, TEXT("ClickNewGame"), 4.0f, [](FPSLPieCtx& Ctx)
    {
        const UPSLMainMenuWidget* Menu = Ctx.bInvalid ? nullptr : RenderEvFindMenu();
        if (!Ctx.Test->TestNotNull(TEXT("D: main menu widget"), Menu))
        {
            Ctx.bInvalid = true;
            return true;
        }
        RenderEvClick(Ctx, Menu->GetNewGameButton(), TEXT("NewGame"));
        Ctx.Test->TestTrue(TEXT("D: species screen shown"), RenderEvShown(Menu->GetSpeciesWidget()));
        return true;
    });
    RenderEvQueueSettle(Context, State);
    AddPieStep(Context, TEXT("ClickConfirm"), 4.0f, [](FPSLPieCtx& Ctx)
    {
        const UPSLMainMenuWidget* Menu = Ctx.bInvalid ? nullptr : RenderEvFindMenu();
        if (!Ctx.Test->TestNotNull(TEXT("D: main menu widget"), Menu))
        {
            Ctx.bInvalid = true;
            return true;
        }
        // Confirm opens the valley and replaces the PIE world: click first, then drop the old handles so WaitForPlayer binds the new one.
        RenderEvClick(Ctx, Menu->GetSpeciesWidget()->GetConfirmButton(), TEXT("Confirm"));
        Ctx.RequiredMap = TEXT("L_LostValley");
        Ctx.bReady = false;
        Ctx.World = nullptr;
        Ctx.GameViewport = nullptr;
        Ctx.PC = nullptr;
        Ctx.Character = nullptr;
        return true;
    });
    AddPieStep(Context, TEXT("WaitForValleyPawn"), 30.0f, [](FPSLPieCtx& Ctx)
    {
        return Ctx.bInvalid || WaitForPlayer(Ctx);
    });
    RenderEvQueueSettle(Context, State);
    AddPieStep(Context, TEXT("D_Hud_vi"), 4.0f, [](FPSLPieCtx& Ctx)
    {
        if (!Ctx.IsUsable())
        {
            return true;
        }
        Ctx.Test->TestTrue(TEXT("D: valley map loaded"), Ctx.World->GetMapName().Contains(TEXT("L_LostValley")));
        const APSLHUD* Hud = Cast<APSLHUD>(Ctx.PC->GetHUD());
        const UPSLHudWidget* HudWidget = Hud ? Hud->GetHudWidget() : nullptr;
        if (!Ctx.Test->TestNotNull(TEXT("D: HUD widget"), HudWidget))
        {
            Ctx.bInvalid = true;
            return true;
        }
        Ctx.Test->TestTrue(TEXT("D: HUD in viewport"), HudWidget->IsInViewport());
        RenderEvAssertCulture(*Ctx.Test, TEXT("D"), TEXT("vi"));
        const TArray<UTextBlock*> Texts = RenderEvVisibleTexts(*HudWidget);
        PSL_RENDER_EXPECT(*Ctx.Test, Texts, TEXT("D"), "HudHealth", "HEALTH", true);
        PSL_RENDER_EXPECT(*Ctx.Test, Texts, TEXT("D"), "HudStamina", "STAMINA", true);
        PSL_RENDER_EXPECT(*Ctx.Test, Texts, TEXT("D"), "HudHunger", "HUNGER", true);
        RenderEvCheckFont(*Ctx.Test, Texts, TEXT("D"));
        RenderEvCapture(Ctx, TEXT("D_Hud_vi"));
        return true;
    });
    // Pawn exists here, so the shared QueueAwaitScreenshot (PSLPieTestSupport.h) works as is.
    QueueAwaitScreenshot(Context, TEXT("D_Hud_vi"));

    // Restore culture + ini bytes before PIE ends; ~FRenderEvState repeats it (idempotent) when a step body never runs.
    AddPieStep(Context, TEXT("RestoreSettings"), 2.0f, [State](FPSLPieCtx&)
    {
        State->Restore();
        return true;
    });
    QueuePieEnd(Context);
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([State]() { State->DeleteTestSlot(); return true; }));
    return true;
}

#undef PSL_RENDER_EXPECT

#endif
