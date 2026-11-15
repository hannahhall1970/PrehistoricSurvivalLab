#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Fonts/FontMeasure.h"
#include "GameFramework/GameUserSettings.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopeExit.h"
#include "PSLGameUserSettings.h"
#include "PSLHudWidget.h"
#include "PSLMainMenuWidget.h"
#include "PSLPauseWidget.h"
#include "PSLSettingsWidget.h"
#include "PSLSpeciesSelectWidget.h"
#include "PSLGameOverWidget.h"
#include "PSLUiButton.h"
#include "Engine/Font.h"
#include "Fonts/CompositeFont.h"
#include "Fonts/FontCache.h"
#include "Fonts/SlateFontInfo.h"
#include "Framework/Application/SlateApplication.h"
#include "PSLUiStyle.h"
#include "Rendering/SlateRenderer.h"

namespace
{
    const EPSLFontWeight FontUiWeights[] = {EPSLFontWeight::Regular, EPSLFontWeight::SemiBold, EPSLFontWeight::Bold};

    // vi msgstr values from Content/Localization/Game/vi/Game.po for the bounded labels (literal on purpose: a .po edit must be re-checked against the bounds).
    struct FFontUiBound { const TCHAR* What; const TCHAR* Vi; int32 Size; EPSLFontWeight Weight; float Bound; UTextBlock* Block; };

    template <typename T>
    T* FontUiMakeLive(FAutomationTestBase& Test, const TCHAR* What, TArray<TSharedRef<SWidget>>& Hold)
    {
        T* Widget = NewObject<T>(GetTransientPackage(), T::StaticClass());
        if (!Test.TestNotNull(FString::Printf(TEXT("%s created"), What), Widget))
        {
            return nullptr;
        }
        Widget->Initialize();
        if (!Test.TestNotNull(FString::Printf(TEXT("%s tree"), What), Widget->WidgetTree.Get()))
        {
            return nullptr;
        }
        Hold.Add(Widget->TakeWidget());
        return Widget;
    }

    // Widest line (labels wrap on explicit newlines only when they fit; every line must fit on its own).
    float FontUiWidestLine(const FSlateFontMeasure& Measure, const FString& Text, const FSlateFontInfo& Info)
    {
        TArray<FString> Lines;
        Text.ParseIntoArray(Lines, TEXT("\n"), false);
        float Widest = 0.0f;
        for (const FString& Line : Lines)
        {
            Widest = FMath::Max(Widest, Measure.Measure(Line, Info).X);
        }
        return Widest;
    }
}

// The generated composite font must hold exactly Regular, SemiBold, Bold (in that order), each backed by its own Inline face.
// FSlateFontInfo with an unknown typeface name silently resolves to the first entry (FontCacheCompositeFont.cpp:504-523),
// so besides the names this checks that each weight resolves to the matching face asset.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLUiProjectFontTest, "PSL.UI.ProjectFont", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLUiProjectFontTest::RunTest(const FString& Parameters)
{
    if (!TestTrue(TEXT("Project font /Game/UI/Fonts/F_BeVietnamPro is present"), FPSLUiStyle::HasProjectFont()))
    {
        return false;
    }
    const UFont* Project = LoadObject<UFont>(nullptr, FPSLUiStyle::FontPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
    if (!TestNotNull(TEXT("F_BeVietnamPro loads as a UFont"), Project))
    {
        return false;
    }
    TestTrue(TEXT("FontCacheType is Runtime"), Project->FontCacheType == EFontCacheType::Runtime);

    // UFont::GetInternalCompositeFont (Font.h:200), FFontData::GetFontFaceAsset (CompositeFont.h:197) / GetLoadingPolicy (:167).
    const TArray<FTypefaceEntry>& Entries = Project->GetInternalCompositeFont().DefaultTypeface.Fonts;
    if (!TestEqual(TEXT("DefaultTypeface has exactly three entries"), Entries.Num(), 3))
    {
        return false;
    }
    TArray<const UObject*> Faces;
    for (int32 Index = 0; Index < 3; ++Index)
    {
        const EPSLFontWeight Weight = FontUiWeights[Index];
        const FString Name = FPSLUiStyle::TypefaceName(Weight).ToString();
        TestEqual(FString::Printf(TEXT("Entry %d is named %s"), Index, *Name), Entries[Index].Name, FPSLUiStyle::TypefaceName(Weight));
        const UObject* Face = Entries[Index].Font.GetFontFaceAsset();
        Faces.Add(Face);
        if (TestNotNull(FString::Printf(TEXT("%s entry references a font face asset"), *Name), Face))
        {
            TestEqual(FString::Printf(TEXT("%s entry references FF_BeVietnamPro_%s"), *Name, *Name), Face->GetPathName(),
                FString::Printf(TEXT("/Game/UI/Fonts/FF_BeVietnamPro_%s.FF_BeVietnamPro_%s"), *Name, *Name));
        }
        TestTrue(FString::Printf(TEXT("%s face LoadingPolicy is Inline"), *Name), Entries[Index].Font.GetLoadingPolicy() == EFontLoadingPolicy::Inline);
    }

    if (!FSlateApplication::IsInitialized() || !FSlateApplication::Get().GetRenderer())
    {
        AddInfo(TEXT("Slate renderer unavailable: skipped resolving each weight through the font cache"));
        return true;
    }
    // FSlateFontCache::GetDefaultFontData (FontCache.h:887).
    const TSharedRef<FSlateFontCache> Cache = FSlateApplication::Get().GetRenderer()->GetFontCache();
    for (int32 Index = 0; Index < 3; ++Index)
    {
        const FString Name = FPSLUiStyle::TypefaceName(FontUiWeights[Index]).ToString();
        const FSlateFontInfo Info = FPSLUiStyle::Font(FPSLUiStyle::BodySize, FontUiWeights[Index]);
        const UObject* Resolved = Cache->GetDefaultFontData(Info).GetFontFaceAsset();
        TestTrue(FString::Printf(TEXT("%s resolves through the font cache to its own face"), *Name), Resolved != nullptr && Resolved == Faces[Index]);
    }
    return true;
}

// Vietnamese runs ~30% longer than English: check that every bounded label either fits its bound or wraps (AutoWrapText),
// with the real widgets' actual size/weight.
// (Measure().Y is the font's max character height by definition (FontMeasure.h:22-30), so vertical clipping is not measurable here.)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLUiVietnameseLayoutTest, "PSL.UI.VietnameseLayout", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLUiVietnameseLayoutTest::RunTest(const FString& Parameters)
{
    if (!FSlateApplication::IsInitialized() || !FSlateApplication::Get().GetRenderer())
    {
        AddInfo(TEXT("Slate renderer unavailable: skipped text measuring"));
        return true;
    }
    // FSlateRenderer::GetFontMeasureService (SlateRenderer.h:419), FSlateFontMeasure::Measure (FontMeasure.h:29) / GetMaxCharacterHeight (:172).
    const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();

    IConsoleVariable* Force = IConsoleManager::Get().FindConsoleVariable(TEXT("Localization.ForceLoadGameLocalizationInEditor"));
    if (!TestNotNull(TEXT("ForceLoadGameLocalizationInEditor CVar exists"), Force))
    {
        return false;
    }
    const FString OldCulture = FInternationalization::Get().GetCurrentCulture()->GetName();
    const bool bOldForce = Force->GetBool();
    TArray<TSharedRef<SWidget>> Hold;
    ON_SCOPE_EXIT
    {
        Hold.Reset();
        FInternationalization::Get().SetCurrentCulture(OldCulture);
        Force->Set(bOldForce ? TEXT("1") : TEXT("0"), ECVF_SetByCode);
    };
    Force->Set(TEXT("1"), ECVF_SetByCode);
    FInternationalization::Get().SetCurrentCulture(TEXT("vi"));

    UPSLMainMenuWidget* Menu = FontUiMakeLive<UPSLMainMenuWidget>(*this, TEXT("Main menu"), Hold);
    UPSLPauseWidget* Pause = FontUiMakeLive<UPSLPauseWidget>(*this, TEXT("Pause"), Hold);
    UPSLSettingsWidget* Settings = FontUiMakeLive<UPSLSettingsWidget>(*this, TEXT("Settings"), Hold);
    UPSLGameOverWidget* GameOver = FontUiMakeLive<UPSLGameOverWidget>(*this, TEXT("Game over"), Hold);
    UPSLSpeciesSelectWidget* Species = FontUiMakeLive<UPSLSpeciesSelectWidget>(*this, TEXT("Species select"), Hold);
    UPSLHudWidget* Hud = FontUiMakeLive<UPSLHudWidget>(*this, TEXT("HUD"), Hold);
    if (!Menu || !Pause || !Settings || !GameOver || !Species || !Hud)
    {
        return false;
    }

    // Bounds: Pause panel 480 wide with 24 padding (432 content), Settings status 560, GameOver / species 480, HUD panel 320 with 12 padding (296).
    const FFontUiBound Bounds[] = {
        {TEXT("Pause toast Pause_SaveRefused"), TEXT("Không thể lưu lúc này: khủng long đang đói lả hoặc bị thương nặng."), FPSLUiStyle::BodySize, EPSLFontWeight::Regular, 432.0f, Pause->GetToastText()},
        {TEXT("Pause warning Pause_UnsavedSince"), TEXT("Tiến trình chưa lưu kể từ 00:00."), FPSLUiStyle::BodySize, EPSLFontWeight::Regular, 432.0f, Pause->GetWarningText()},
        {TEXT("Pause warning Pause_NeverSaved"), TEXT("Lượt chơi này chưa từng được lưu."), FPSLUiStyle::BodySize, EPSLFontWeight::Regular, 432.0f, Pause->GetWarningText()},
        {TEXT("Settings status Settings_ConfirmDiscard"), TEXT("Bạn có thay đổi chưa áp dụng. Nhấn Quay lại lần nữa để bỏ chúng."), FPSLUiStyle::BodySize, EPSLFontWeight::Regular, 560.0f, Settings->GetStatusText()},
        {TEXT("GameOver stats GameOver_Stats"), TEXT("Sống sót 00:00\nCon mồi đã hạ: 0\nThức ăn đã ăn: 0"), FPSLUiStyle::BodySize, EPSLFontWeight::Regular, 480.0f, GameOver->GetStatsText()},
        {TEXT("Species description (Para)"), TEXT("Loài ăn cỏ nhanh nhẹn, linh hoạt. Gặm cây cỏ; vết cắn chỉ gây thương tích nhẹ."), FPSLUiStyle::BodySize, EPSLFontWeight::Regular, 480.0f, Species->GetDescriptionText()},
        {TEXT("Species diet badge Diet_Carnivore"), TEXT("ĂN THỊT"), FPSLUiStyle::BodySize, EPSLFontWeight::SemiBold, 480.0f, Species->GetDietText()},
        {TEXT("HUD state HudSprinting"), TEXT("ĐANG CHẠY NƯỚC RÚT"), 14, EPSLFontWeight::SemiBold, 296.0f, Hud->GetStateText()},
        {TEXT("HUD hint HintCannotEatPlants"), TEXT("Thú ăn thịt không ăn được cây cỏ. Hãy đi săn con mồi."), 14, EPSLFontWeight::Regular, 296.0f, Hud->GetHintText()}};
    for (const FFontUiBound& Bound : Bounds)
    {
        if (!TestNotNull(FString::Printf(TEXT("%s: text block exists"), Bound.What), Bound.Block))
        {
            continue;
        }
        const FSlateFontInfo& Actual = Bound.Block->GetFont();
        TestEqual(FString::Printf(TEXT("%s: font size"), Bound.What), Actual.Size, static_cast<float>(Bound.Size));
        if (FPSLUiStyle::HasProjectFont())
        {
            TestEqual(FString::Printf(TEXT("%s: typeface"), Bound.What), Actual.TypefaceFontName, FPSLUiStyle::TypefaceName(Bound.Weight));
        }
        const float Width = FontUiWidestLine(*Measure, Bound.Vi, Actual);
        const bool bWraps = Bound.Block->GetAutoWrapText();
        TestTrue(FString::Printf(TEXT("%s: fits %.0f px (%.1f) or AutoWrapText is on"), Bound.What, Bound.Bound, Width), Width <= Bound.Bound || bWraps);
        if (bWraps && Width > Bound.Bound)
        {
            TArray<FString> Words;
            FString(Bound.Vi).ParseIntoArrayWS(Words); // the delimiter of ParseIntoArray is a whole string, not a char set
            for (const FString& Word : Words)
            {
                TestTrue(FString::Printf(TEXT("%s: word '%s' fits the wrap width"), Bound.What, *Word), Measure->Measure(Word, Actual).X <= Bound.Bound);
            }
            const float WrapAt = Bound.Block->GetWrapTextAt();
            TestTrue(FString::Printf(TEXT("%s: WrapTextAt %.0f is set within the bound (0 = panel-bounded HUD labels)"), Bound.What, WrapAt), WrapAt <= Bound.Bound);
        }
    }

    // Button labels are SemiBold.
    for (UPSLUiButton* Button : {Menu->GetContinueButton(), Menu->GetSettingsButton()})
    {
        if (TestNotNull(TEXT("Main menu button exists"), Button) && TestNotNull(TEXT("Button label exists"), Button->GetLabel()) && FPSLUiStyle::HasProjectFont())
        {
            TestEqual(TEXT("Button label is SemiBold"), Button->GetLabel()->GetFont().TypefaceFontName, FPSLUiStyle::TypefaceName(EPSLFontWeight::SemiBold));
        }
    }
    return true;
}

// Live en -> vi -> en through UPSLGameUserSettings::ApplyCulture (OnCultureChanged -> RefreshTexts) on built widgets, then a settings reload:
// texts follow the culture and no TextBlock ever loses the project font.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSLUiLiveCultureSwitchTest, "PSL.UI.LiveCultureSwitch", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPSLUiLiveCultureSwitchTest::RunTest(const FString& Parameters)
{
    UPSLGameUserSettings* Settings = UPSLGameUserSettings::Get();
    if (!Settings)
    {
        AddError(TEXT("UPSLGameUserSettings::Get() is null: GameUserSettingsClassName is not the PSL class"));
        return false;
    }
    IConsoleVariable* Force = IConsoleManager::Get().FindConsoleVariable(TEXT("Localization.ForceLoadGameLocalizationInEditor"));
    if (!TestNotNull(TEXT("ForceLoadGameLocalizationInEditor CVar exists"), Force))
    {
        return false;
    }
    const FString OldName = Settings->CultureName;
    const FString OldCulture = FInternationalization::Get().GetCurrentCulture()->GetName();
    const bool bOldForce = Force->GetBool();
    // SaveSettings flushes the whole live object; put the exact on-disk bytes back (same pattern as PSL.Settings.CultureApplies).
    const FString IniPath = GGameUserSettingsIni;
    TArray<uint8> OldIni;
    const bool bHadIni = FFileHelper::LoadFileToArray(OldIni, *IniPath, FILEREAD_Silent);
    TArray<TSharedRef<SWidget>> Hold;
    ON_SCOPE_EXIT
    {
        Hold.Reset(); // NativeDestruct unbinds OnCultureChanged
        Settings->CultureName = OldName;
        Settings->SaveSettings();
        Settings->ApplyCulture();
        FInternationalization::Get().SetCurrentCulture(OldCulture);
        Force->Set(bOldForce ? TEXT("1") : TEXT("0"), ECVF_SetByCode);
        if (bHadIni)
        {
            FFileHelper::SaveArrayToFile(OldIni, *IniPath);
        }
        else
        {
            IFileManager::Get().Delete(*IniPath);
        }
    };
    Force->Set(TEXT("1"), ECVF_SetByCode);
    Settings->CultureName = TEXT("en");
    Settings->ApplyCulture();

    UPSLMainMenuWidget* Menu = FontUiMakeLive<UPSLMainMenuWidget>(*this, TEXT("Main menu"), Hold);
    UPSLSettingsWidget* SettingsWidget = FontUiMakeLive<UPSLSettingsWidget>(*this, TEXT("Settings"), Hold);
    UPSLPauseWidget* Pause = FontUiMakeLive<UPSLPauseWidget>(*this, TEXT("Pause"), Hold);
    UPSLSpeciesSelectWidget* Species = FontUiMakeLive<UPSLSpeciesSelectWidget>(*this, TEXT("Species select"), Hold);
    UPSLHudWidget* Hud = FontUiMakeLive<UPSLHudWidget>(*this, TEXT("HUD"), Hold);
    if (!Menu || !SettingsWidget || !Pause || !Species || !Hud)
    {
        return false;
    }
    UUserWidget* Widgets[] = {Menu, SettingsWidget, Pause, Species, Hud};

    auto FindBlock = [](UUserWidget* Widget, const TCHAR* Text) -> UTextBlock*
    {
        UTextBlock* Found = nullptr;
        Widget->WidgetTree->ForEachWidget([&](UWidget* Child)
        {
            UTextBlock* Block = Cast<UTextBlock>(Child);
            if (!Found && Block && Block->GetText().ToString() == Text)
            {
                Found = Block;
            }
        });
        return Found;
    };
    // Every TextBlock keeps the project font (or the engine fallback when the asset is absent) with a known weight.
    auto CheckFonts = [&](const TCHAR* Phase)
    {
        const UFont* Project = LoadObject<UFont>(nullptr, FPSLUiStyle::FontPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
        int32 Count = 0;
        for (UUserWidget* Widget : Widgets)
        {
            Widget->WidgetTree->ForEachWidget([&](UWidget* Child)
            {
                const UTextBlock* Block = Cast<UTextBlock>(Child);
                if (!Block)
                {
                    return;
                }
                ++Count;
                const FSlateFontInfo& Font = Block->GetFont();
                if (FPSLUiStyle::HasProjectFont())
                {
                    TestTrue(FString::Printf(TEXT("%s: '%s' uses F_BeVietnamPro"), Phase, *Block->GetText().ToString()), Project != nullptr && Font.FontObject == Project);
                    const FName Face = Font.TypefaceFontName;
                    TestTrue(FString::Printf(TEXT("%s: '%s' typeface %s is Regular/SemiBold/Bold"), Phase, *Block->GetText().ToString(), *Face.ToString()),
                        Face == FPSLUiStyle::TypefaceName(EPSLFontWeight::Regular) || Face == FPSLUiStyle::TypefaceName(EPSLFontWeight::SemiBold) || Face == FPSLUiStyle::TypefaceName(EPSLFontWeight::Bold));
                }
                else
                {
                    TestTrue(FString::Printf(TEXT("%s: '%s' keeps the engine fallback font"), Phase, *Block->GetText().ToString()), Font.FontObject == nullptr);
                }
            });
        }
        TestTrue(FString::Printf(TEXT("%s: text blocks were inspected (%d)"), Phase, Count), Count > 0);
    };
    // Known keys (en, vi): Continue / Settings buttons, HUD stamina caption, Pause title.
    struct FProbeKey { UUserWidget* Widget; const TCHAR* En; const TCHAR* Vi; };
    const FProbeKey Keys[] = {{Menu, TEXT("Continue"), TEXT("Tiếp tục")}, {Menu, TEXT("Settings"), TEXT("Cài đặt")},
        {Hud, TEXT("STAMINA"), TEXT("THỂ LỰC")}, {Pause, TEXT("PAUSED"), TEXT("TẠM DỪNG")}};
    TArray<UTextBlock*> Probes;
    for (const FProbeKey& Key : Keys)
    {
        UTextBlock* Block = FindBlock(Key.Widget, Key.En);
        if (!TestNotNull(FString::Printf(TEXT("'%s' starts in English"), Key.En), Block))
        {
            return false;
        }
        Probes.Add(Block);
    }
    CheckFonts(TEXT("en (initial)"));

    auto SwitchTo = [&](const TCHAR* Culture, bool bVietnamese)
    {
        Settings->CultureName = Culture;
        Settings->ApplyCulture();
        TestTrue(FString::Printf(TEXT("Culture is %s"), Culture), FInternationalization::Get().GetCurrentCulture()->GetName().StartsWith(Culture));
        for (int32 Index = 0; Index < Probes.Num(); ++Index)
        {
            TestEqual(FString::Printf(TEXT("'%s' refreshed to %s"), Keys[Index].En, Culture), Probes[Index]->GetText().ToString(), FString(bVietnamese ? Keys[Index].Vi : Keys[Index].En));
        }
        CheckFonts(Culture);
    };
    SwitchTo(TEXT("vi"), true);
    SwitchTo(TEXT("en"), false);
    SwitchTo(TEXT("vi"), true);

    // Settings reload: persist, load a fresh object, apply its culture, widgets keep vi texts and fonts.
    Settings->SaveSettings();
    UPSLGameUserSettings* Fresh = NewObject<UPSLGameUserSettings>(GetTransientPackage());
    Fresh->LoadSettings(true);
    TestEqual(TEXT("Reloaded CultureName is vi"), Fresh->CultureName, FString(TEXT("vi")));
    Fresh->ApplyCulture();
    for (int32 Index = 0; Index < Probes.Num(); ++Index)
    {
        TestEqual(FString::Printf(TEXT("'%s' still vi after settings reload"), Keys[Index].En), Probes[Index]->GetText().ToString(), FString(Keys[Index].Vi));
    }
    CheckFonts(TEXT("vi (settings reload)"));
    return true;
}

#endif
